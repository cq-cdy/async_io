#ifndef TALON_ASYNC_IO_KERNELBUF_HPP_
#define TALON_ASYNC_IO_KERNELBUF_HPP_

#include "async_io_constants.hpp"
#include "async_io_pool.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace talon {
inline namespace v2_2_0 {
namespace task {

class IOHandler;

struct KernelBuf {
    friend class talon::IOHandler;

    explicit KernelBuf(size_t size) : size_(size) { AllocateStorage(size); }

    KernelBuf(const KernelBuf&) = delete;
    KernelBuf& operator=(const KernelBuf&) = delete;

    KernelBuf(KernelBuf&& other) noexcept
        : size_(other.size_), offset_(other.offset_), io_ret_(other.io_ret_) {
        MoveStorageFrom(other);
        other.size_ = 0;
        other.offset_ = 0;
    }

    KernelBuf& operator=(KernelBuf&& other) noexcept {
        if (this != &other) {
            DestroyStorage();
            size_ = other.size_;
            offset_ = other.offset_;
            io_ret_ = other.io_ret_;
            MoveStorageFrom(other);
            other.size_ = 0;
            other.offset_ = 0;
        }
        return *this;
    }

    ~KernelBuf() { DestroyStorage(); }

    char& operator[](size_t index) noexcept { return data()[index]; }
    const char& operator[](size_t index) const noexcept { return data()[index]; }

    [[nodiscard]] char* data() noexcept {
        switch (storage_tag_) {
            case StorageTag::kSbo:  return sbo_data_;
            case StorageTag::kPool: return pool_data_;
            case StorageTag::kHeap: return heap_vec_.data();
            case StorageTag::kNone: return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] const char* data() const noexcept { return const_cast<KernelBuf*>(this)->data(); }

    void resize(size_t new_size) {
        if (new_size == size_ && storage_tag_ != StorageTag::kNone) return;
        size_t copy_size = (new_size < size_) ? new_size : size_;
        char* old_data = (storage_tag_ != StorageTag::kNone) ? data() : nullptr;
        DestroyStorage();
        AllocateStorage(new_size);
        if (old_data != nullptr && copy_size > 0) std::memcpy(data(), old_data, copy_size);
        size_ = new_size;
    }

    void set_fd_offset(int64_t offset) noexcept { offset_ = offset; }
    [[nodiscard]] int64_t fd_offset() const noexcept { return offset_; }

    [[nodiscard]] int bytes_transferred() const noexcept { return io_ret_.value_; }
    [[nodiscard]] int active_file_descriptor() const noexcept { return io_ret_.ret_fd_; }

    size_t size_{0};
    int64_t offset_{0};

private:
    enum class StorageTag : uint8_t { kNone = 0, kSbo, kPool, kHeap };
    StorageTag storage_tag_{StorageTag::kNone};

    char sbo_data_[kKernelBufSboSize]{};

    struct { char* data; size_t alloc_size; } pool_{nullptr, 0};
    std::vector<char> heap_vec_{};

    struct IORet { int value_{}; int ret_fd_{}; };
    IORet io_ret_{0, 0};

    void AllocateStorage(size_t size) {
        if (size <= kKernelBufSboSize) {
            storage_tag_ = StorageTag::kSbo;
            std::memset(sbo_data_, 0, kKernelBufSboSize);
        } else if (size <= memory::BufferPool::kMaxPooledSize) {
            void* buf = memory::BufferPool::Instance().Allocate(size);
            if (buf != nullptr) {
                storage_tag_ = StorageTag::kPool;
                pool_.data = static_cast<char*>(buf);
                pool_.alloc_size = size;
            } else {
                storage_tag_ = StorageTag::kHeap;
                new (&heap_vec_) std::vector<char>(size);
            }
        } else {
            storage_tag_ = StorageTag::kHeap;
            new (&heap_vec_) std::vector<char>(size);
        }
        size_ = size;
    }

    void DestroyStorage() {
        switch (storage_tag_) {
            case StorageTag::kSbo: break;
            case StorageTag::kPool:
                if (pool_.data != nullptr) {
                    memory::BufferPool::Instance().Deallocate(pool_.data, pool_.alloc_size);
                    pool_.data = nullptr;
                    pool_.alloc_size = 0;
                }
                break;
            case StorageTag::kHeap: heap_vec_.~vector(); break;
            case StorageTag::kNone: break;
        }
        storage_tag_ = StorageTag::kNone;
    }

    void MoveStorageFrom(KernelBuf& other) {
        storage_tag_ = other.storage_tag_;
        switch (other.storage_tag_) {
            case StorageTag::kSbo:
                std::memcpy(sbo_data_, other.sbo_data_, kKernelBufSboSize); break;
            case StorageTag::kPool:
                pool_ = other.pool_; other.pool_ = {nullptr, 0}; break;
            case StorageTag::kHeap:
                new (&heap_vec_) std::vector<char>(std::move(other.heap_vec_)); break;
            case StorageTag::kNone: break;
        }
        other.storage_tag_ = StorageTag::kNone;
    }

    void set_bytes_transferred(int ret) noexcept { io_ret_.value_ = ret; }
    void set_active_file_descriptor(int fd) noexcept { io_ret_.ret_fd_ = fd; }
};

using KernelBufPtr = std::unique_ptr<KernelBuf>;

[[nodiscard]] inline KernelBufPtr MakeKernelBuffer(size_t size) {
    return std::make_unique<KernelBuf>(size);
}

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_KERNELBUF_HPP_
