#ifndef TALON_ASYNC_IO_RESULT_HPP_
#define TALON_ASYNC_IO_RESULT_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace talon {
inline namespace v2_2_0 {
namespace task {

struct IOResult {
    IOResult(int io_ret, int event_fd, bool iodone, std::string err_msg = "")
        : io_ret(io_ret), event_fd(event_fd), iodone(iodone),
          err_msg(std::move(err_msg)) {}

    [[nodiscard]] int IoRet() const noexcept { return io_ret; }
    [[nodiscard]] int EventFd() const noexcept { return event_fd; }
    [[nodiscard]] bool IoDone() const noexcept { return iodone; }
    [[nodiscard]] const std::string& ErrMsg() const noexcept { return err_msg; }

    int io_ret{};
    int event_fd{};
    bool iodone{false};
    std::string err_msg;
};

// Inline signal/wait mechanism replacing std::promise<bool>.  Eliminates two
// heap allocations per AddTask() call.  signal() is the hot path (event-loop
// thread); wait() is the cold path (user thread via WaitForCompletion()).
class DoneState {
public:
    DoneState() = default;
    DoneState(const DoneState&) = delete;
    DoneState& operator=(const DoneState&) = delete;
    DoneState(DoneState&&) = delete;
    DoneState& operator=(DoneState&&) = delete;

    void Signal() noexcept {
        int prev = state_.exchange(kSignaled, std::memory_order_release);
        if (prev == kWaiting) {
            { std::lock_guard<std::mutex> lk(mtx_); }
            cv_.notify_one();
        }
    }

    void Reset() noexcept { state_.store(kIdle, std::memory_order_relaxed); }

    [[nodiscard]] bool Wait(int timeout_ms) noexcept {
        if (state_.load(std::memory_order_acquire) == kSignaled) return true;
        if (timeout_ms == 0) return state_.load(std::memory_order_acquire) == kSignaled;

        std::unique_lock<std::mutex> lk(mtx_);
        if (state_.load(std::memory_order_relaxed) == kSignaled) return true;
        state_.store(kWaiting, std::memory_order_relaxed);

        if (timeout_ms < 0) {
            cv_.wait(lk, [this]() { return state_.load(std::memory_order_acquire) == kSignaled; });
            return true;
        }
        bool ready = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
            [this]() { return state_.load(std::memory_order_acquire) == kSignaled; });
        if (!ready) state_.store(kIdle, std::memory_order_relaxed);
        return ready;
    }

    [[nodiscard]] bool IsReady() const noexcept { return state_.load(std::memory_order_acquire) == kSignaled; }

private:
    static constexpr int kIdle     = -1;
    static constexpr int kWaiting  = 0;
    static constexpr int kSignaled = 1;

    std::atomic<int> state_{kIdle};
    std::mutex mtx_;
    std::condition_variable cv_;
};

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_RESULT_HPP_
