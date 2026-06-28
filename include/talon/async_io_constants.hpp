#ifndef TALON_ASYNC_IO_CONSTANTS_HPP_
#define TALON_ASYNC_IO_CONSTANTS_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <sys/socket.h>
#include <utility>

#ifdef TALON_DEBUG
#define TALON_DEBUG_IS_ENABLED_ 1
#else
#define TALON_DEBUG_IS_ENABLED_ 0
#endif

namespace talon {
inline namespace v2_2_0 {

inline constexpr std::string_view kVersion = "v2.2.0";
inline constexpr bool kDebugEnabled = (TALON_DEBUG_IS_ENABLED_ == 1);

inline constexpr int kTimeoutBit = 63;
inline constexpr int kErrorBit   = 62;
inline constexpr int kCancelBit  = 61;

inline constexpr uint64_t kTimeoutFlag = 1ULL << kTimeoutBit;
inline constexpr uint64_t kErrorFlag   = 1ULL << kErrorBit;
inline constexpr uint64_t kCancelFlag  = 1ULL << kCancelBit;
inline constexpr uint64_t kEventMaskFlags = kTimeoutFlag | kErrorFlag | kCancelFlag;

inline constexpr size_t kDefaultBufferSize = 2048;
inline constexpr size_t kKernelBufSboSize = 256;
inline constexpr size_t kInlineFunctionSboSize = 64;

inline constexpr size_t kObjectPoolDefaultCapacity = 4096;
inline constexpr size_t kObjectPoolGrowBatch = 1024;
inline constexpr size_t kObjectPoolTlsCacheSize = 32;

inline constexpr size_t kBufferPoolNumClasses = 11;
inline constexpr size_t kBufferPoolClassSizes[] = {
    64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
};
inline constexpr size_t kBufferPoolMaxPooledSize = 65536;
inline constexpr size_t kBufferPoolTlsCacheSize = 16;

// ============================================================================
// AsyncIoConfig — User-configurable runtime options
// ============================================================================

// All configurable parameters with sensible defaults. Users may construct
// an AsyncIoConfig, override desired fields, and pass it to IOHandler,
// TcpServer, or TcpClient constructors.
struct AsyncIoConfig {
    // --- io_uring ring parameters ---
    int max_entries = 256;

    // --- Buffer / pool defaults ---
    size_t default_buffer_size = 2048;
    size_t kernel_buf_sbo_size = 256;
    size_t task_pool_capacity = 4096;
    size_t task_pool_grow_batch = 1024;

    // --- Auto-flush ---
    int auto_flush_threshold_percent = 75;

    // --- Task defaults ---
    int default_timeout_ms = 0;
    int default_max_retry_count = -1;

    // --- TCP defaults ---
    int tcp_accept_backlog = SOMAXCONN;
    int tcp_default_port = 8080;
    int connect_timeout_ms = 5000;

    // --- Backpressure ---
    // Maximum number of in-flight I/O operations allowed.  When this limit
    // is reached, AddTask() returns false so the caller can apply back-off.
    // A value of 0 disables the limit (no backpressure).
    int max_inflight_ops = 0;  // 0 = unlimited

    // When backpressure is active and the caller busy-retries AddTask(),
    // this is the suggested sleep duration in microseconds between retries.
    int backpressure_retry_us = 100;
};

// ============================================================================
// DebugLog
// ============================================================================

template <typename... Args>
inline void DebugLog(const char* fmt, Args&&... args) noexcept {
    if constexpr (kDebugEnabled) {
        std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    }
}

// ============================================================================
// Enumerations
// ============================================================================

namespace task {

enum class TaskType : int {
    kNone = 0,
    kRead,
    kWrite,
    kAccept,
    kConnect,
    kClose,
    kTimeout,
    kCancel,
    kLinkTimeout,
    kTimeoutRemove,
    kTimeoutUpdate
};

[[nodiscard]] inline constexpr std::string_view TaskTypeName(TaskType type) noexcept {
    switch (type) {
        case TaskType::kNone:          return "NONE";
        case TaskType::kRead:          return "READ";
        case TaskType::kWrite:         return "WRITE";
        case TaskType::kAccept:        return "ACCEPT";
        case TaskType::kConnect:       return "CONNECT";
        case TaskType::kClose:         return "CLOSE";
        case TaskType::kTimeout:       return "TIMEOUT";
        case TaskType::kCancel:        return "CANCEL";
        case TaskType::kLinkTimeout:   return "LINK_TIMEOUT";
        case TaskType::kTimeoutRemove: return "TIMEOUT_REMOVE";
        case TaskType::kTimeoutUpdate: return "TIMEOUT_UPDATE";
    }
    return "UNKNOWN";
}

enum class TaskState : int {
    kReady = 1000,
    kSubmitted,
    kSuccess,
    kFailed,
    kTimeout,
    kCanceled
};

[[nodiscard]] inline constexpr std::string_view TaskStateName(TaskState state) noexcept {
    switch (state) {
        case TaskState::kReady:     return "READY";
        case TaskState::kSubmitted: return "SUBMITTED";
        case TaskState::kSuccess:   return "SUCCESS";
        case TaskState::kFailed:    return "FAILED";
        case TaskState::kTimeout:   return "TIMEOUT";
        case TaskState::kCanceled:  return "CANCELED";
    }
    return "UNKNOWN";
}

enum class EventFlag : uint64_t {
    kNone    = 0,
    kTimeout = kTimeoutFlag,
    kError   = kErrorFlag,
    kCancel  = kCancelFlag,
    kMask    = kEventMaskFlags
};

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#undef TALON_DEBUG_IS_ENABLED_

#endif  // TALON_ASYNC_IO_CONSTANTS_HPP_
