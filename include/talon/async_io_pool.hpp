#ifndef TALON_ASYNC_IO_POOL_HPP_
#define TALON_ASYNC_IO_POOL_HPP_

#include "async_io_constants.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <type_traits>
#include <vector>

namespace talon {
inline namespace v2_2_0 {
namespace memory {

struct alignas(64) PoolNode {
    std::atomic<PoolNode*> next{nullptr};
};

// ============================================================================
// TaggedPointer — ABA-proof 128-bit tagged pointer for TreiberStack.
//
// On x86_64 (cmpxchg16b) and ARMv8.1+ (LSE), 128-bit DWCAS is lock-free.
// On older ARM64 or 32-bit platforms, we fall back to a mutex-protected
// stack.  The tagged pointer eliminates the classic ABA problem: even if
// the same PoolNode* reappears at head_, the tag is guaranteed different.
// ============================================================================

struct alignas(16) TaggedPointer {
    PoolNode* ptr;
    uint64_t   tag;
};

// The lock-free path is selected when the platform reports 16-byte
// atomics as always lock-free.  Otherwise we use a plain mutex stack.
static constexpr bool kHasLockFreeTaggedPtr =
    std::atomic<TaggedPointer>::is_always_lock_free;

namespace detail {

// ---- Lock-free (ABA-safe) stack -------------------------------------------
class LockFreeStack {
public:
    void Push(PoolNode* node) noexcept {
        TaggedPointer old_head = head_.load(std::memory_order_relaxed);
        TaggedPointer new_head;
        do {
            node->next.store(old_head.ptr, std::memory_order_relaxed);
            new_head = {node, old_head.tag + 1};
        } while (!head_.compare_exchange_weak(old_head, new_head,
                     std::memory_order_release, std::memory_order_relaxed));
    }

    [[nodiscard]] PoolNode* Pop() noexcept {
        TaggedPointer old_head = head_.load(std::memory_order_acquire);
        TaggedPointer new_head;
        while (old_head.ptr != nullptr) {
            PoolNode* next = old_head.ptr->next.load(std::memory_order_acquire);
            new_head = {next, old_head.tag + 1};
            if (head_.compare_exchange_weak(old_head, new_head,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return old_head.ptr;
        }
        return nullptr;
    }

    void PushChain(PoolNode* chain_head, PoolNode* chain_tail) noexcept {
        TaggedPointer old_head = head_.load(std::memory_order_relaxed);
        TaggedPointer new_head;
        do {
            chain_tail->next.store(old_head.ptr, std::memory_order_relaxed);
            new_head = {chain_head, old_head.tag + 1};
        } while (!head_.compare_exchange_weak(old_head, new_head,
                     std::memory_order_release, std::memory_order_relaxed));
    }

    [[nodiscard]] bool Empty() const noexcept {
        return head_.load(std::memory_order_acquire).ptr == nullptr;
    }

    // Returns a snapshot of the head pointer for diagnostics (racy by nature).
    [[nodiscard]] PoolNode* UnsafeHeadForDiagnostics() const noexcept {
        return head_.load(std::memory_order_acquire).ptr;
    }

    alignas(64) std::atomic<TaggedPointer> head_{{nullptr, 0}};
};

// ---- Mutex-based fallback stack --------------------------------------------
class MutexStack {
public:
    void Push(PoolNode* node) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        node->next.store(head_, std::memory_order_relaxed);
        head_ = node;
    }

    [[nodiscard]] PoolNode* Pop() noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        if (head_ == nullptr) return nullptr;
        PoolNode* node = head_;
        head_ = node->next.load(std::memory_order_relaxed);
        return node;
    }

    void PushChain(PoolNode* chain_head, PoolNode* chain_tail) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        chain_tail->next.store(head_, std::memory_order_relaxed);
        head_ = chain_head;
    }

    [[nodiscard]] bool Empty() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return head_ == nullptr;
    }

    // Returns a snapshot of the head pointer for diagnostics (racy by nature).
    [[nodiscard]] PoolNode* UnsafeHeadForDiagnostics() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return head_;
    }

private:
    mutable std::mutex mtx_;
    PoolNode* head_{nullptr};
};

}  // namespace detail

// Public type alias: picks the best implementation for this platform.
using TreiberStack = std::conditional_t<kHasLockFreeTaggedPtr,
                                        detail::LockFreeStack,
                                        detail::MutexStack>;

template <typename T>
class ObjectPool {
    static_assert(std::is_class_v<T>, "ObjectPool requires a class type");
    static_assert(alignof(T) <= 64, "");

public:
    explicit ObjectPool(size_t capacity = kObjectPoolDefaultCapacity,
                        size_t grow_batch = kObjectPoolGrowBatch)
        : capacity_(capacity), grow_batch_(grow_batch) {
        // Reset the TLS cache for this thread to avoid stale pointers from a
        // previous ObjectPool<T> instance that was destroyed and recreated.
        tls_cache_.Reset();
        GrowPool(capacity_);
    }

    ~ObjectPool() { for (auto* block : storage_blocks_) std::free(block); }
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    [[nodiscard]] T* Acquire() noexcept {
        PoolNode* node = tls_cache_.Pop();
        if (node != nullptr) return reinterpret_cast<T*>(node);
        node = free_list_.Pop();
        if (node != nullptr) return reinterpret_cast<T*>(node);
        GrowPool(grow_batch_);
        node = free_list_.Pop();
        if (node != nullptr) return reinterpret_cast<T*>(node);
        // Last-resort allocation.  std::aligned_alloc may return nullptr
        // (it does not throw), satisfying the zero-exception design goal.
        constexpr size_t kSlotAlign = 64;
        size_t obj_size = ((sizeof(T) > sizeof(PoolNode)
                            ? sizeof(T) : sizeof(PoolNode)) + (kSlotAlign - 1))
                           & ~size_t(kSlotAlign - 1);
        void* mem = std::aligned_alloc(kSlotAlign, obj_size);
        return reinterpret_cast<T*>(mem);
    }

    void Release(T* obj) {
        if (obj == nullptr) return;
        obj->~T();
        ReleaseMemory(obj);
    }

    // ReleaseRaw recycles the memory without calling the destructor.
    // Used from operator delete, which is called AFTER the delete-expression
    // has already invoked the destructor via the virtual dispatch mechanism.
    void ReleaseMemory(T* obj) {
        if (obj == nullptr) return;
        PoolNode* node = reinterpret_cast<PoolNode*>(obj);
        if (tls_cache_.Push(node)) return;
        auto* batch = tls_cache_.FlushHalf();
        if (batch != nullptr) {
            PoolNode* tail = batch;
            while (tail->next.load(std::memory_order_relaxed) != nullptr)
                tail = tail->next.load(std::memory_order_relaxed);
            free_list_.PushChain(batch, tail);
        }
        tls_cache_.Push(node);
    }

    [[nodiscard]] size_t ApproximateFreeCount() const noexcept {
        size_t count = 0;
        for (PoolNode* n = free_list_.UnsafeHeadForDiagnostics();
             n != nullptr && count < 1000000;
             n = n->next.load(std::memory_order_acquire)) count++;
        return count;
    }

private:
    struct alignas(64) ThreadCache {
        PoolNode* nodes[kObjectPoolTlsCacheSize]{};
        size_t count{0};

        void Reset() noexcept { count = 0; }

        PoolNode* Pop() noexcept { return count == 0 ? nullptr : nodes[--count]; }
        bool Push(PoolNode* node) noexcept {
            if (count >= kObjectPoolTlsCacheSize) return false;
            nodes[count++] = node;
            return true;
        }
        PoolNode* FlushHalf() noexcept {
            if (count <= kObjectPoolTlsCacheSize / 4) return nullptr;
            size_t n = count / 2;
            count -= n;
            PoolNode* head = nodes[count];
            PoolNode* tail = head;
            for (size_t i = count + 1; i < count + n; i++) {
                tail->next.store(nodes[i], std::memory_order_relaxed);
                tail = nodes[i];
            }
            tail->next.store(nullptr, std::memory_order_relaxed);
            return head;
        }
    };

    void GrowPool(size_t count) {
        // T and PoolNode overlay the same memory (union pattern).  The slot
        // must be large enough for the bigger of the two, aligned to 64.
        constexpr size_t kSlotAlign = 64;
        size_t obj_size = ((sizeof(T) > sizeof(PoolNode) ? sizeof(T) : sizeof(PoolNode)) + (kSlotAlign - 1)) & ~size_t(kSlotAlign - 1);
        void* block = std::aligned_alloc(kSlotAlign, obj_size * count);
        if (block == nullptr) return;
        storage_blocks_.push_back(block);

        PoolNode *chain_head = nullptr, *chain_tail = nullptr;
        for (size_t i = 0; i < count; i++) {
            char* ptr = static_cast<char*>(block) + i * obj_size;
            // PoolNode overlays the same start address as T (not after T).
            PoolNode* node = reinterpret_cast<PoolNode*>(ptr);
            new (node) PoolNode();
            if (chain_head == nullptr) { chain_head = chain_tail = node; }
            else { chain_tail->next.store(node, std::memory_order_relaxed); chain_tail = node; }
        }
        if (chain_head != nullptr) {
            chain_tail->next.store(nullptr, std::memory_order_relaxed);
            free_list_.PushChain(chain_head, chain_tail);
        }
    }

    TreiberStack free_list_;
    std::vector<void*> storage_blocks_;
    size_t capacity_, grow_batch_;
    static thread_local ThreadCache tls_cache_;
};

template <typename T>
thread_local typename ObjectPool<T>::ThreadCache ObjectPool<T>::tls_cache_;

// ============================================================================
// BufferPool
// ============================================================================

class BufferPool {
public:
    static constexpr size_t kNumClasses = kBufferPoolNumClasses;
    static constexpr size_t kMaxPooledSize = kBufferPoolMaxPooledSize;

    [[nodiscard]] static size_t ClassForSize(size_t bytes) noexcept {
        for (size_t i = 0; i < kNumClasses; i++)
            if (bytes <= kBufferPoolClassSizes[i]) return i;
        return kNumClasses;
    }

    [[nodiscard]] void* Allocate(size_t bytes) {
        size_t idx = ClassForSize(bytes);
        if (idx >= kNumClasses) return nullptr;
        void* buf = tls_cache_[idx].Pop();
        if (buf != nullptr) return buf;
        PoolNode* node = free_lists_[idx].Pop();
        if (node != nullptr) return reinterpret_cast<void*>(node);
        EnsureClassPopulated(idx);
        node = free_lists_[idx].Pop();
        return (node != nullptr) ? reinterpret_cast<void*>(node) : AllocateFromSystem(idx);
    }

    void Deallocate(void* ptr, size_t bytes) {
        if (ptr == nullptr) return;
        size_t idx = ClassForSize(bytes);
        if (idx >= kNumClasses) { std::free(ptr); return; }
        PoolNode* node = reinterpret_cast<PoolNode*>(ptr);
        if (tls_cache_[idx].Push(node)) return;
        auto* batch = tls_cache_[idx].FlushHalf();
        if (batch != nullptr) {
            PoolNode* tail = batch;
            while (tail->next.load(std::memory_order_relaxed) != nullptr)
                tail = tail->next.load(std::memory_order_relaxed);
            free_lists_[idx].PushChain(batch, tail);
        }
        tls_cache_[idx].Push(node);
    }

    [[nodiscard]] static BufferPool& Instance() { static BufferPool pool; return pool; }

private:
    BufferPool() { for (size_t i = 0; i < kNumClasses; i++) class_initialized_[i].store(false, std::memory_order_relaxed); }
    ~BufferPool() { for (size_t i = 0; i < kNumClasses; i++) for (auto* s : slabs_[i]) std::free(s); }
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    void EnsureClassPopulated(size_t idx) {
        bool expected = false;
        if (!class_initialized_[idx].compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
            return;
        size_t block_size = kBufferPoolClassSizes[idx];
        size_t alloc_size = block_size + sizeof(PoolNode);
        size_t slab_total = (block_size >= 4096) ? (4 * 1024 * 1024) : (1 * 1024 * 1024);
        size_t num_blocks = slab_total / alloc_size;
        void* slab = std::aligned_alloc(64, num_blocks * alloc_size);
        if (slab == nullptr) return;
        slabs_[idx].push_back(slab);
        PoolNode *chain_head = nullptr, *chain_tail = nullptr;
        for (size_t i = 0; i < num_blocks; i++) {
            char* ptr = static_cast<char*>(slab) + i * alloc_size;
            PoolNode* node = reinterpret_cast<PoolNode*>(ptr);
            new (node) PoolNode();
            if (chain_head == nullptr) { chain_head = chain_tail = node; }
            else { chain_tail->next.store(node, std::memory_order_relaxed); chain_tail = node; }
        }
        if (chain_head != nullptr) { chain_tail->next.store(nullptr, std::memory_order_relaxed); free_lists_[idx].PushChain(chain_head, chain_tail); }
    }

    static void* AllocateFromSystem(size_t idx) { return std::aligned_alloc(64, kBufferPoolClassSizes[idx] + sizeof(PoolNode)); }

    struct alignas(64) ClassCache {
        void* bufs[kBufferPoolTlsCacheSize]{};
        size_t count{0};
        void* Pop() noexcept { return count == 0 ? nullptr : bufs[--count]; }
        bool Push(void* buf) noexcept { if (count >= kBufferPoolTlsCacheSize) return false; bufs[count++] = buf; return true; }
        PoolNode* FlushHalf() noexcept {
            if (count <= kBufferPoolTlsCacheSize / 4) return nullptr;
            size_t n = count / 2; count -= n;
            PoolNode* head = static_cast<PoolNode*>(bufs[count]);
            PoolNode* tail = head;
            for (size_t i = count + 1; i < count + n; i++) {
                tail->next.store(static_cast<PoolNode*>(bufs[i]), std::memory_order_relaxed);
                tail = static_cast<PoolNode*>(bufs[i]);
            }
            tail->next.store(nullptr, std::memory_order_relaxed);
            return head;
        }
    };

    alignas(64) TreiberStack free_lists_[kNumClasses];
    static thread_local ClassCache tls_cache_[kNumClasses];
    std::atomic<bool> class_initialized_[kNumClasses]{};
    std::vector<void*> slabs_[kNumClasses];
};

thread_local BufferPool::ClassCache BufferPool::tls_cache_[kNumClasses];

}  // namespace memory

namespace task {

template <typename T>
[[nodiscard]] inline memory::ObjectPool<T>& GetTaskPool() {
    static memory::ObjectPool<T> pool(kObjectPoolDefaultCapacity, kObjectPoolGrowBatch);
    return pool;
}

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_POOL_HPP_
