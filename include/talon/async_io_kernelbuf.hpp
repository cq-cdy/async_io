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

    explicit KernelBuf(size_t sz) : size(sz) { AllocateStorage(sz); }

    KernelBuf(const KernelBuf&) = delete;
    KernelBuf& operator=(const KernelBuf&) = delete;

    KernelBuf(KernelBuf&& other) noexcept
        : size(other.size), offset(other.offset), io_ret_(other.io_ret_) {
        MoveStorageFrom(other);
        other.size = 0;
        other.offset = 0;
    }

    KernelBuf& operator=(KernelBuf&& other) noexcept {
        if (this != &other) {
            DestroyStorage();
            size = other.size;
            offset = other.offset;
            io_ret_ = other.io_ret_;
            MoveStorageFrom(other);
            other.size = 0;
            other.offset = 0;
        }
        return *this;
    }

    ~KernelBuf() { DestroyStorage(); }

    char& operator[](size_t index) noexcept { return Data()[index]; }
    const char& operator[](size_t index) const noexcept { return Data()[index]; }

    [[nodiscard]] char* Data() noexcept {
        switch (storage_tag_) {
            case StorageTag::kSbo:  return sbo_data_;
            case StorageTag::kPool: return pool_data_;
            case StorageTag::kHeap: return heap_vec_.data();
            case StorageTag::kNone: return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] const char* Data() const noexcept { return const_cast<KernelBuf*>(this)->Data(); }

    void Resize(size_t new_size) {
        if (new_size == size && storage_tag_ != StorageTag::kNone) return;
        size_t copy_size = (new_size < size) ? new_size : size;
        char* old_data = (storage_tag_ != StorageTag::kNone) ? Data() : nullptr;
        DestroyStorage();
        AllocateStorage(new_size);
        if (old_data != nullptr && copy_size > 0) std::memcpy(Data(), old_data, copy_size);
        size = new_size;
    }

    void SetFdOffset(int64_t off) noexcept { offset = off; }
    [[nodiscard]] int64_t FdOffset() const noexcept { return offset; }

    [[nodiscard]] int BytesTransferred() const noexcept { return io_ret_.value_; }
    [[nodiscard]] int ActiveFileDescriptor() const noexcept { return io_ret_.ret_fd_; }

    size_t size{0};
    int64_t offset{0};

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
        this->size = size;
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

    void SetBytesTransferred(int ret) noexcept { io_ret_.value_ = ret; }
    void SetActiveFileDescriptor(int fd) noexcept { io_ret_.ret_fd_ = fd; }
};

using KernelBufPtr = std::unique_ptr<KernelBuf>;

[[nodiscard]] inline KernelBufPtr MakeKernelBuffer(size_t size) {
    return std::make_unique<KernelBuf>(size);
}

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_KERNELBUF_HPP_
