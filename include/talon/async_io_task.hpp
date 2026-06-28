#ifndef TALON_ASYNC_IO_TASK_HPP_
#define TALON_ASYNC_IO_TASK_HPP_

#include "async_io_constants.hpp"
#include "async_io_kernelbuf.hpp"
#include "async_io_pool.hpp"
#include "async_io_result.hpp"
#include "async_io_traits.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <liburing.h>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace talon {
inline namespace v2_2_0 {
namespace task {

// ============================================================================
// InlineFunction — Type-erased callable with configurable SBO
// ============================================================================

template <typename Signature, size_t InlineSize = kInlineFunctionSboSize>
class InlineFunction;

template <typename Ret, typename... Args, size_t InlineSize>
class InlineFunction<Ret(Args...), InlineSize> {
public:
    InlineFunction() noexcept : vtable_(nullptr), is_heap_(false) {}

    template <typename Func,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, InlineFunction>>>
    InlineFunction(Func&& func) : is_heap_(false) { Construct(std::forward<Func>(func)); }

    InlineFunction(InlineFunction&& other) noexcept
        : vtable_(other.vtable_), is_heap_(other.is_heap_) {
        if (other.vtable_ != nullptr) {
            if (other.is_heap_) std::memcpy(storage_, other.storage_, sizeof(void*));
            else other.vtable_->move_construct(other.storage_, storage_);
        }
        other.vtable_ = nullptr; other.is_heap_ = false;
    }

    InlineFunction& operator=(InlineFunction&& other) noexcept {
        if (this != &other) {
            Destroy();
            vtable_ = other.vtable_; is_heap_ = other.is_heap_;
            if (other.vtable_ != nullptr) {
                if (other.is_heap_) std::memcpy(storage_, other.storage_, sizeof(void*));
                else other.vtable_->move_construct(other.storage_, storage_);
            }
            other.vtable_ = nullptr; other.is_heap_ = false;
        }
        return *this;
    }

    InlineFunction(const InlineFunction&) = delete;
    InlineFunction& operator=(const InlineFunction&) = delete;
    ~InlineFunction() { Destroy(); }

    Ret operator()(Args... args) const { return vtable_->invoke(ObjPtr(), std::forward<Args>(args)...); }
    [[nodiscard]] explicit operator bool() const noexcept { return vtable_ != nullptr; }

private:
    struct VTable {
        Ret (*invoke)(const void*, Args...);
        void (*destroy)(void*);
        void (*move_construct)(void* src, void* dst);
    };

    static constexpr size_t kStorageSize = InlineSize;
    static constexpr size_t kStorageAlign = alignof(std::max_align_t);

    alignas(kStorageAlign) char storage_[kStorageSize]{};
    const VTable* vtable_{nullptr};
    bool is_heap_{false};

    void* ObjPtr() {
        if (is_heap_) { void* p; std::memcpy(&p, storage_, sizeof(void*)); return p; }
        return storage_;
    }
    const void* ObjPtr() const { return const_cast<InlineFunction*>(this)->ObjPtr(); }

    template <typename Func>
    void Construct(Func&& func) {
        using Decayed = std::decay_t<Func>;
        static const VTable vt = {
            [](const void* s, Args... args) -> Ret {
                return (*static_cast<const Decayed*>(s))(std::forward<Args>(args)...);
            },
            [](void* s) { static_cast<Decayed*>(s)->~Decayed(); },
            [](void* src, void* dst) {
                auto* s = static_cast<Decayed*>(src);
                new (dst) Decayed(std::move(*s));
                s->~Decayed();
            }
        };
        if constexpr (sizeof(Decayed) <= kStorageSize && alignof(Decayed) <= kStorageAlign) {
            new (storage_) Decayed(std::forward<Func>(func)); is_heap_ = false;
        } else {
            auto* ptr = new Decayed(std::forward<Func>(func));
            std::memcpy(storage_, &ptr, sizeof(ptr)); is_heap_ = true;
        }
        vtable_ = &vt;
    }

    void Destroy() {
        if (vtable_ != nullptr) {
            vtable_->destroy(ObjPtr());
            if (is_heap_) ::operator delete(ObjPtr());
            vtable_ = nullptr; is_heap_ = false;
        }
    }
};

// ============================================================================
// Task Base & DefaultHandler
// ============================================================================

struct Task { virtual ~Task() = default; };

struct DefaultHandler {
    void operator()(KernelBuf*) const noexcept {}
};

// ============================================================================
// AsyncTask
// ============================================================================

template <typename Ret = void, typename... UserArgs>
class AsyncTask : public Task {
public:
    using FunctionType = InlineFunction<Ret(KernelBuf*), kInlineFunctionSboSize>;
    using CheckBufferFunc = InlineFunction<bool(const KernelBuf*), 32>;

    friend class talon::IOHandler;
    template <typename Func, typename... Args>
    friend auto CreateTaskWithHandler(int fd, Func&& func, Args&&... args);
    friend AsyncTask<void>* CreateTaskWithHandler(int fd);

    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;

    ~AsyncTask() override { DebugLog("AsyncTask::~AsyncTask fd=%d\n", fd_); }

    static void* operator new(size_t size) {
        if constexpr (std::is_same_v<Ret, void> && sizeof...(UserArgs) == 0) {
            (void)size;
            auto* mem = GetTaskPool<AsyncTask<void>>().Acquire();
            if (mem != nullptr) return mem;
        }
        return ::operator new(size);
    }

    static void operator delete(void* ptr) noexcept {
        if constexpr (std::is_same_v<Ret, void> && sizeof...(UserArgs) == 0) {
            if (ptr != nullptr) {
                // The delete-expression already invoked the destructor via
                // virtual dispatch.  ReleaseMemory recycles the backing
                // storage without a second destructor call.  Pool memory
                // is sliced from large aligned_alloc blocks and cannot be
                // passed to free() individually.
                GetTaskPool<AsyncTask<void>>().ReleaseMemory(
                    static_cast<AsyncTask<void>*>(ptr));
                return;
            }
        }
        ::operator delete(ptr);
    }

    [[nodiscard]] TaskState State() const noexcept { return task_state_.load(std::memory_order_acquire); }
    [[nodiscard]] TaskType Type() const noexcept { return task_type_; }
    [[nodiscard]] int Fd() const noexcept { return fd_; }
    [[nodiscard]] KernelBuf* Buffer() noexcept { return buffer_.get(); }
    [[nodiscard]] const KernelBuf* Buffer() const noexcept { return buffer_.get(); }
    [[nodiscard]] bool RepeatForever() const noexcept { return repeat_forever_; }
    [[nodiscard]] bool RepeatWhenFailed() const noexcept { return max_retry_count_ > 0; }
    [[nodiscard]] const std::string& DebugStr() const noexcept { return debug_str_; }
    [[nodiscard]] Task* NextTask() const noexcept { return next_.get(); }

    void SetTaskType(TaskType t) noexcept { task_type_ = t; }
    void SetTimeout(int ms) noexcept { timeout_ms_ = ms; }
    void SetMaxRetryCount(int n) noexcept { max_retry_count_ = n; }
    void SetDebugStr(std::string s) noexcept { debug_str_ = std::move(s); }
    void SetRepeatForever(bool v) noexcept {
        if (v) { detach_.store(false, std::memory_order_release); timeout_ms_ = 0; }
        repeat_forever_ = v;
    }
    void SetRepeatWhenFailed(bool v) noexcept { repeat_when_failed_ = v; }
    void SetCheckBuffer(CheckBufferFunc f) noexcept { check_buffer_ = std::move(f); }
    void ResetBuffer() noexcept { if (buffer_) { buffer_->Resize(0); } }
    void SetBuffer(KernelBufPtr buf) noexcept { buffer_ = std::move(buf); }

    void DeepCopyBufferFrom(const KernelBuf& src) noexcept {
        buffer_ = std::make_unique<KernelBuf>(src.size);
        if (src.size > 0) { std::memcpy(buffer_->Data(), src.Data(), src.size); }
    }

    template <typename Ret_, typename... UserArgs_>
    void SetNextTask(AsyncTask<Ret_, UserArgs_...>* next) noexcept { next_.reset(next); }
    Task* ReleaseNextTask() noexcept { return next_.release(); }

    // --- I/O Operations ---

    // Synchronous wait for completion result. Uses DoneState; zero heap allocations.
    [[nodiscard]] IOResult WaitForCompletion(int timeout_ms = -1) noexcept {
        bool is_detached = detach_.load(std::memory_order_acquire);
        IOResult ret(-1, buffer_ ? buffer_->ActiveFileDescriptor() : -1, is_detached, "io done");
        if (!is_detached) { ret.err_msg = "task not detached from execute queue yet"; return ret; }
        if (!done_state_.Wait(timeout_ms)) {
            if (timeout_ms > 0) { ret.err_msg = "WaitForCompletion: timeout after " + std::to_string(timeout_ms) + "ms"; }
            else if (timeout_ms == 0) { ret.err_msg = "WaitForCompletion: not yet ready (timeout=0)"; }
            return ret;
        }
        ret.io_ret = buffer_ ? buffer_->BytesTransferred() : -1;
        ret.err_msg = "IO result consumed";
        return ret;
    }

    [[nodiscard]] __u64 Cancel(struct io_uring* uring, __u64 special_user_data = 0) noexcept {
        if (uring == nullptr) { DebugLog("Cancel: null uring\n"); return 0; }
        __u64 userdata = (special_user_data == 0) ? user_data_ : special_user_data;
        io_uring_sqe* cancel_sqe = io_uring_get_sqe(uring);
        if (cancel_sqe == nullptr) { DebugLog("Cancel: io_uring_get_sqe null\n"); return 0; }
        io_uring_prep_cancel(cancel_sqe, reinterpret_cast<void*>(userdata), 0);
        cancel_sqe->user_data = userdata | kCancelFlag;
        int ret = io_uring_submit(uring);
        if (ret < 0) { DebugLog("Cancel submit failed: %s\n", strerror(-ret)); return 0; }
        return cancel_sqe->user_data;
    }

private:
    explicit AsyncTask(int fd, FunctionType&& handler) : handler_(std::move(handler)) {
        fd_ = fd;
        buffer_ = MakeKernelBuffer(kDefaultBufferSize);
    }

    Ret ExecuteCompletionHandler() {
        if constexpr (std::is_same_v<Ret, void>) handler_(buffer_.get());
        else return handler_(buffer_.get());
    }

    void SetTaskState(TaskState s) noexcept { task_state_.store(s, std::memory_order_release); }
    void MarkDetachedAndComplete(bool detach) noexcept {
        if (detach) done_state_.Signal();
        detach_.store(detach, std::memory_order_release);
    }

    // ---- Hot cache line ----
    alignas(64) std::atomic<TaskState> task_state_{TaskState::kReady};
    std::atomic<bool> detach_{false};
    DoneState done_state_;
    KernelBufPtr buffer_;
    __u64 user_data_{0};
    int fd_{-1};

    // ---- Warm cache line ----
    TaskType task_type_{TaskType::kNone};
    FunctionType handler_;
    std::unique_ptr<Task> next_{nullptr};
    std::atomic<bool> is_cancel_{false};
    std::atomic<int> try_count_{0};

    // ---- Cold fields ----
    int timeout_ms_{0};
    int max_retry_count_{-1};
    bool repeat_forever_{false};
    bool repeat_when_failed_{false};
    std::tuple<UserArgs...> args_tuple_{};
    CheckBufferFunc check_buffer_ = [](const KernelBuf*) noexcept { return true; };
    std::string debug_str_{};
    char padding_[32]{};
};

// ============================================================================
// CreateTaskWithHandler — factory functions
// ============================================================================

template <typename Func_, typename... Args_>
[[nodiscard]] auto CreateTaskWithHandler(int fd, Func_&& func, Args_&&... args) {
    using FuncTraits = FunctionTraits<std::decay_t<Func_>>;
    using FuncArgsTuple = typename FuncTraits::ArgsTuple;

    static_assert(std::tuple_size_v<FuncArgsTuple> == sizeof...(Args_) + 1,
                  "Handler arg count mismatch. First param must be KernelBuf*.");
    using FirstArgType = std::tuple_element_t<0, FuncArgsTuple>;
    static_assert(std::is_same_v<std::decay_t<FirstArgType>, KernelBuf*>,
                  "First handler parameter must be KernelBuf*.");

    using ReturnType = typename FuncTraits::ReturnType;
    auto bound = std::bind(std::forward<Func_>(func), std::placeholders::_1,
                           std::forward<Args_>(args)...);
    return new AsyncTask<ReturnType>(fd, InlineFunction<ReturnType(KernelBuf*)>(std::move(bound)));
}

[[nodiscard]] inline AsyncTask<void>* CreateTaskWithHandler(int fd) {
    return new AsyncTask<void>(fd, InlineFunction<void(KernelBuf*)>(
        [](KernelBuf*) {}));
}

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_TASK_HPP_
