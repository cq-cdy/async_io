#ifndef TALON_ASYNC_IO_HANDLER_HPP_
#define TALON_ASYNC_IO_HANDLER_HPP_

#include <arpa/inet.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "async_io_constants.hpp"
#include "async_io_kernelbuf.hpp"
#include "async_io_task.hpp"

namespace talon {
inline namespace v2_2_0 {

class IOHandler {
    friend class TcpServer;
    friend class TcpClient;

public:
    explicit IOHandler(const AsyncIoConfig& config = {})
        : config_(config) {
        uring_ = std::make_unique<io_uring>();
        int ret = io_uring_queue_init(config_.max_entries, uring_.get(), 0);
        if (ret < 0) {
            init_error_ = std::string("io_uring_queue_init: ") + strerror(-ret);
            DebugLog("%s\n", init_error_.c_str());
            return;
        }
        StartEventLoop();
    }

    ~IOHandler() {
        UninstallSignalHandlers();
        RequestShutdown(); Join();
        if (uring_) io_uring_queue_exit(uring_.get());
    }

    IOHandler(const IOHandler&) = delete;
    IOHandler& operator=(const IOHandler&) = delete;

    // ========================================================================
    // Signal Handling
    // ========================================================================
    //
    // InstallSignalHandlers() registers SIGINT and SIGTERM handlers that
    // call RequestShutdown() in an async-signal-safe manner.  This is a
    // convenience — users who need custom signal handling may call
    // RequestShutdown() directly from their own signal handler.
    //
    // Only one IOHandler can be registered for signal-based shutdown at a
    // time.  The destructor automatically unregisters.

    [[nodiscard]] bool InstallSignalHandlers() {
        IOHandler* prev = nullptr;
        if (!active_instance_.compare_exchange_strong(prev, this,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return false;  // Another IOHandler already registered.
        struct sigaction sa = {};
        sa.sa_handler = &IOHandler::OnSignal;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGINT, &sa, &saved_sigint_) < 0 ||
            sigaction(SIGTERM, &sa, &saved_sigterm_) < 0) {
            active_instance_.store(nullptr, std::memory_order_release);
            return false;
        }
        signals_installed_ = true;
        return true;
    }

    void UninstallSignalHandlers() noexcept {
        if (!signals_installed_) return;
        sigaction(SIGINT, &saved_sigint_, nullptr);
        sigaction(SIGTERM, &saved_sigterm_, nullptr);
        active_instance_.store(nullptr, std::memory_order_release);
        signals_installed_ = false;
    }

    // Async-signal-safe entry point for user-registered signal handlers.
    // Users may call this directly from their own sigaction callback.
    static void OnSignal(int /*signum*/) noexcept {
        auto* self = active_instance_.load(std::memory_order_acquire);
        if (self != nullptr) self->RequestShutdown();
    }

    // --- Status ---
    [[nodiscard]] bool initialized() const noexcept { return init_error_.empty(); }
    [[nodiscard]] const std::string& init_error() const noexcept { return init_error_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] io_uring* iouring() const noexcept { return uring_.get(); }
    [[nodiscard]] const AsyncIoConfig& config() const noexcept { return config_; }

    // --- Backpressure / Load ---
    // Returns the current number of in-flight I/O operations.  Useful for
    // implementing application-level flow control alongside max_inflight_ops.
    [[nodiscard]] int64_t inflight_count() const noexcept {
        return inflight_count_.load(std::memory_order_acquire);
    }

    // Returns true when the inflight limit is configured and currently
    // saturated — callers should apply back-off before retrying AddTask.
    [[nodiscard]] bool backpressure_active() const noexcept {
        int limit = config_.max_inflight_ops;
        return limit > 0 && inflight_count_.load(std::memory_order_acquire) >= limit;
    }

    // --- Task Submission ---

    template <typename Ret_, typename... UserArgs_>
    [[nodiscard]] bool AddTask(task::AsyncTask<Ret_, UserArgs_...>* task) {
        if (task == nullptr) { DebugLog("AddTask: null\n"); return false; }
        if (!TryTransitionToSubmitted(task)) {
            task->set_debug_str("failed: already executing");
            task->detach_.store(false, std::memory_order_release);
            return false;
        }

        // --- Backpressure gate ---
        // Reserve an inflight slot before submitting.  If the limit is
        // reached, transition the task back to ready so the caller may
        // retry later.
        if (config_.max_inflight_ops > 0) {
            int64_t cur = inflight_count_.load(std::memory_order_relaxed);
            while (cur < config_.max_inflight_ops) {
                if (inflight_count_.compare_exchange_weak(cur, cur + 1,
                        std::memory_order_relaxed))
                    goto submit;
            }
            // Backpressure: no slot available.  Roll back the state
            // transition so the caller can retry.
            task->task_state_.store(task::TaskState::kReady,
                                    std::memory_order_release);
            DebugLog("AddTask backpressure, inflight=%ld max=%d\n",
                     (long)cur, config_.max_inflight_ops);
            return false;
        } else {
            inflight_count_.fetch_add(1, std::memory_order_relaxed);
        }
    submit:
        task->MarkDetachedAndComplete(false);
        task->is_cancel_.store(false, std::memory_order_release);
        DebugLog("AddTask ok, fd=%d\n", task->fd_);
        if (task->repeat_when_failed()) task->try_count_.store(0, std::memory_order_relaxed);
        task->done_state_.Reset();
        SubmitTask(task);
        AutoFlush();
        return true;
    }

    void Flush() noexcept { io_uring_submit(uring_.get()); }

    // --- Shutdown ---
    void RequestShutdown() noexcept {
        shutdown_requested_.store(true, std::memory_order_release);
        auto* sqe = io_uring_get_sqe(uring_.get());
        if (sqe != nullptr) { io_uring_prep_nop(sqe); sqe->user_data = 0; io_uring_submit(uring_.get()); }
    }
    void Join() { if (event_thread_.joinable()) event_thread_.join(); }

private:
    // ========================================================================
    // Event Loop
    // ========================================================================
    void StartEventLoop() { event_thread_ = std::thread([this] { RunEventLoop(); }); }

    void RunEventLoop() {
        while (!shutdown_requested_.load(std::memory_order_acquire)) {
            io_uring_cqe* cqe = nullptr;
            int ret = io_uring_peek_cqe(uring_.get(), &cqe);
            if (ret == -EAGAIN) ret = io_uring_wait_cqe(uring_.get(), &cqe);
            if (ret < 0) {
                if (ret == -EINTR) continue;
                DebugLog("RunEventLoop error: %s\n", strerror(-ret));
                if (shutdown_requested_.load(std::memory_order_acquire)) break;
                continue;
            }
            if (cqe == nullptr) continue;
            io_uring_cqe_seen(uring_.get(), cqe);

            uint64_t ud = cqe->user_data;
            if (ud == 0) continue;  // Shutdown sentinel.
            auto et = static_cast<task::EventFlag>(ud & kEventMaskFlags);
            auto* tp = reinterpret_cast<task::AsyncTask<>*>(ud & ~kEventMaskFlags);
            if (tp == nullptr) continue;
            __builtin_prefetch(tp, 0, 3);
            DispatchCqe(tp, et, cqe->res);
        }
    }

    void DispatchCqe(task::AsyncTask<>* tp, task::EventFlag et, int res) {
        auto st = tp->task_state_.load(std::memory_order_acquire);
        if (st != task::TaskState::kSubmitted && st != task::TaskState::kReady) {
            DebugLog("Skip CQE fd=%d state=%s\n", tp->fd_, task::TaskStateName(st).data());
            return;  // Stale CQE — inflight was already decremented.
        }
        if (tp->is_cancel_.load(std::memory_order_acquire)) {
            tp->MarkDetachedAndComplete(true);
            tp->SetTaskState(task::TaskState::kCanceled);
            delete tp;
            inflight_count_.fetch_sub(1, std::memory_order_relaxed);
            return;
        }
        switch (et) {
            case task::EventFlag::kNone:    HandleNormalEvent(tp, res); break;
            case task::EventFlag::kTimeout: HandleTimeoutEvent(tp);    break;
            case task::EventFlag::kError:   HandleErrorEvent(tp, res); break;
            case task::EventFlag::kCancel:  if (res >= 0) HandleCancelEvent(tp); break;
            default:                        HandleErrorEvent(tp, res); break;
        }
        // Every path through the switch above represents one completed I/O
        // operation.  Re-submissions (repeat_forever / chained tasks) are
        // re-counted inside AddTask, so the net effect is correct.
        inflight_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    // ========================================================================
    // CAS Gate
    // ========================================================================
    template <typename Ret_, typename... UserArgs_>
    bool TryTransitionToSubmitted(task::AsyncTask<Ret_, UserArgs_...>* t) noexcept {
        auto e = t->task_state_.load(std::memory_order_acquire);
        if (e == task::TaskState::kSubmitted) return false;
        return t->task_state_.compare_exchange_strong(e, task::TaskState::kSubmitted,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // ========================================================================
    // SQE Submission
    // ========================================================================
    template <typename Ret_, typename... UserArgs_>
    void SubmitTask(task::AsyncTask<Ret_, UserArgs_...>* task) {
        auto* sqe = io_uring_get_sqe(uring_.get());
        if (sqe == nullptr) { QueueFailedTask(task); return; }

        switch (task->task_type_) {
            case task::TaskType::kRead:
                io_uring_prep_read(sqe, task->fd_, task->buffer_->data(),
                    static_cast<unsigned>(task->buffer_->size_),
                    static_cast<__u64>(task->buffer_->fd_offset())); break;
            case task::TaskType::kWrite:
                io_uring_prep_write(sqe, task->fd_, task->buffer_->data(),
                    static_cast<unsigned>(task->buffer_->size_),
                    static_cast<__u64>(task->buffer_->fd_offset())); break;
            case task::TaskType::kAccept:
                io_uring_prep_accept(sqe, task->fd_, nullptr, nullptr, SOCK_NONBLOCK); break;
            case task::TaskType::kConnect:
                HandleConnect(task, sqe); break;
            default:
                DebugLog("SubmitTask: unsupported type %s fd=%d\n",
                    task::TaskTypeName(task->task_type_).data(), task->fd_);
                task->buffer_->set_bytes_transferred(-EINVAL);
                task->buffer_->set_active_file_descriptor(task->fd_);
                QueueFailedTask(task); return;
        }

        sqe->user_data = reinterpret_cast<uintptr_t>(task);
        task->user_data_ = sqe->user_data;

        if (task->timeout_ms_ > 0) {
            sqe->flags |= IOSQE_IO_LINK;
            auto* tsqe = io_uring_get_sqe(uring_.get());
            if (tsqe != nullptr) {
                struct __kernel_timespec ts;
                ts.tv_sec = task->timeout_ms_ / 1000;
                ts.tv_nsec = (task->timeout_ms_ % 1000) * 1000000LL;
                io_uring_prep_link_timeout(tsqe, &ts, 0);
                tsqe->user_data = reinterpret_cast<uintptr_t>(task) | kTimeoutFlag;
            } else {
                sqe->flags &= ~IOSQE_IO_LINK;
                DebugLog("SubmitTask: no timeout SQE for fd=%d\n", task->fd_);
            }
        }
    }

    void AutoFlush() {
        uint32_t used = io_uring_sq_ready(uring_.get());
        if (used * 100 >= uring_->sq.ring_entries * config_.auto_flush_threshold_percent)
            io_uring_submit(uring_.get());
    }

    // ========================================================================
    // Event Handlers
    // ========================================================================
    template <typename Ret_, typename... UserArgs_>
    void HandleNormalEvent(task::AsyncTask<Ret_, UserArgs_...>* tp, int res) {
        DebugLog("HandleNormalEvent fd=%d res=%d\n", tp->fd_, res);
        tp->buffer_->set_bytes_transferred(res);
        tp->buffer_->set_active_file_descriptor(tp->fd_);

        if (res < 0) {
            if (res == -EAGAIN || res == -EWOULDBLOCK) {
                while (!AddTask(tp)) std::this_thread::yield(); return;
            }
            if (res == -ECANCELED) {
                if (tp->timeout_ms_ > 0) HandleTimeoutEvent(tp);
                else { tp->set_debug_str("cancelled by kernel");
                       tp->MarkDetachedAndComplete(true);
                       tp->SetTaskState(task::TaskState::kCanceled); delete tp; }
                return;
            }
            HandleIoFailure(tp); return;
        }

        if (res == 0) { HandleConnectionClose(tp); return; }
        tp->ExecuteCompletionHandler();

        if (tp->repeat_forever()) { tp->SetTaskState(task::TaskState::kSuccess); AddTask(tp); }
        else { tp->SetTaskState(task::TaskState::kSuccess);
               tp->detach_.store(true, std::memory_order_release); }

        if (tp->next_task()) { auto* n = tp->ReleaseNextTask(); AddTask(static_cast<task::AsyncTask<>*>(n)); }
        if (!tp->repeat_forever() && !tp->next_task()) tp->MarkDetachedAndComplete(true);
    }

    template <typename Ret_, typename... UserArgs_>
    void HandleErrorEvent(task::AsyncTask<Ret_, UserArgs_...>* tp, int res) {
        tp->buffer_->set_bytes_transferred(res);
        tp->buffer_->set_active_file_descriptor(tp->fd_);
        HandleFailedTask(tp);
    }

    template <typename Ret_, typename... UserArgs_>
    void HandleCancelEvent(task::AsyncTask<Ret_, UserArgs_...>* tp) {
        tp->is_cancel_.store(true, std::memory_order_release);
        DebugLog("Canceled fd=%d\n", tp->fd_);
        tp->set_debug_str("cancelled");
    }

    template <typename Ret_, typename... UserArgs_>
    void HandleTimeoutEvent(task::AsyncTask<Ret_, UserArgs_...>* tp) { HandleTimeoutTask(tp); }

    template <typename Ret_, typename... UserArgs_>
    void HandleConnectionClose(task::AsyncTask<Ret_, UserArgs_...>* tp) {
        if (tp->fd_ > 0) { DebugLog("Closing fd=%d\n", tp->fd_); close(tp->fd_); tp->fd_ = -1; }
        tp->MarkDetachedAndComplete(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        tp->SetTaskState(task::TaskState::kSuccess);
        // Process chained next task before deletion, if any.
        if (tp->next_task()) {
            auto* n = tp->ReleaseNextTask();
            AddTask(static_cast<task::AsyncTask<>*>(n));
        }
        delete tp;
    }

    template <typename Ret_, typename... UserArgs_>
    bool HandleTimeoutTask(task::AsyncTask<Ret_, UserArgs_...>* tp) {
        auto st = tp->task_state_.load(std::memory_order_acquire);
        if (st != task::TaskState::kSubmitted && st != task::TaskState::kReady) {
            DebugLog("HandleTimeoutTask skip fd=%d state=%s\n", tp->fd_, task::TaskStateName(st).data());
            return false;
        }
        tp->set_debug_str("timed out");
        DebugLog("Timeout fd=%d\n", tp->fd_);
        tp->MarkDetachedAndComplete(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        tp->SetTaskState(task::TaskState::kTimeout);
        delete tp; return false;
    }

    template <typename Ret_, typename... UserArgs_>
    bool HandleFailedTask(task::AsyncTask<Ret_, UserArgs_...>* tp) {
        int cur = tp->try_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (cur < tp->max_retry_count_) { tp->SetTaskState(task::TaskState::kReady); AddTask(tp); return true; }
        tp->set_debug_str("failed after " + std::to_string(cur) + " retries");
        tp->try_count_.store(0, std::memory_order_relaxed);
        tp->MarkDetachedAndComplete(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        tp->SetTaskState(task::TaskState::kFailed);
        delete tp; return false;
    }

    template <typename Ret_, typename... UserArgs_>
    void HandleIoFailure(task::AsyncTask<Ret_, UserArgs_...>* tp) {
        if (!HandleFailedTask(tp)) DebugLog("Final failure fd=%d\n", tp->fd_);
    }

    // ========================================================================
    // Connect (used by TcpClient)
    // ========================================================================
    template <typename Ret_, typename... UserArgs_>
    void HandleConnect(task::AsyncTask<Ret_, UserArgs_...>* task, io_uring_sqe* sqe) {
        if (!ValidateConnectTask(task)) {
            task->buffer_->set_bytes_transferred(-EINVAL);
            task->buffer_->set_active_file_descriptor(task->fd_);
            QueueFailedTask(task); return;
        }
        auto* addr = reinterpret_cast<sockaddr_in*>(task->buffer_->data());
        io_uring_prep_connect(sqe, task->fd_, reinterpret_cast<sockaddr*>(addr), sizeof(sockaddr_in));
    }

    template <typename Ret_, typename... UserArgs_>
    bool ValidateConnectTask(task::AsyncTask<Ret_, UserArgs_...>* task) {
        if (task->fd_ <= 0) return false;
        auto* buf = task->buffer();
        if (buf == nullptr || buf->size_ < sizeof(sockaddr_in)) return false;
        auto* addr = reinterpret_cast<sockaddr_in*>(buf->data());
        if (addr->sin_family != AF_INET) return false;
        if (ntohs(addr->sin_port) == 0 || ntohs(addr->sin_port) > 65535) return false;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
        if (std::strcmp(ip, "0.0.0.0") == 0) return false;
        return true;
    }

    // ========================================================================
    // Queue Failed
    // ========================================================================
    template <typename Ret_, typename... UserArgs_>
    void QueueFailedTask(task::AsyncTask<Ret_, UserArgs_...>* task) {
        auto* sqe = io_uring_get_sqe(uring_.get());
        if (sqe == nullptr) { HandleFailedTask(task); return; }
        struct __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 0};
        io_uring_prep_timeout(sqe, &ts, 0, 0);
        sqe->user_data = reinterpret_cast<uintptr_t>(task) | kErrorFlag;
        io_uring_submit(uring_.get());
    }

    // ========================================================================
    // Members
    // ========================================================================
    AsyncIoConfig config_;
    std::unique_ptr<io_uring> uring_;
    std::thread event_thread_;
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<int64_t> inflight_count_{0};
    std::string init_error_;
    std::string last_error_;

    // Signal handling (static + per-instance)
    static std::atomic<IOHandler*> active_instance_;
    bool signals_installed_{false};
    struct sigaction saved_sigint_{};
    struct sigaction saved_sigterm_{};
};

// Static member definition (header-only library, C++17 inline variable).
inline std::atomic<IOHandler*> IOHandler::active_instance_{nullptr};

}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_HANDLER_HPP_
