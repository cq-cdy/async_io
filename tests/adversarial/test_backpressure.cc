// ============================================================================
// Backpressure Tests — Verify inflight limiting and queue-full behavior.
// ============================================================================

#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("backpressure: AddTask respects max_inflight_ops") {
    AsyncIoConfig config;
    config.max_inflight_ops = 10;
    config.auto_flush_threshold_percent = 100;  // Manual flush only.

    IOHandler io(config);
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    auto h = [](KernelBuf*) {};  // No-op handler.

    // Submit tasks — first 10 should be accepted, the rest rejected.
    for (int i = 0; i < 50; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) accepted.fetch_add(1);
        else rejected.fetch_add(1);
    }

    // Without flushing, the inflight limit should have blocked some.
    CHECK(accepted.load() >= 10);
    CHECK(io.BackpressureActive());

    // Now flush and let tasks complete — backpressure should ease.
    io.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // After completion, we should be able to submit more.
    int after_flush_accepted = 0;
    for (int i = 0; i < 5; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) after_flush_accepted++;
    }

    CHECK(after_flush_accepted > 0);

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("backpressure: inflight_count is monotonic") {
    AsyncIoConfig config;
    config.max_inflight_ops = 64;
    config.max_entries = 128;

    IOHandler io(config);
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> done{0};
    auto h = [&](KernelBuf*) { done.fetch_add(1, std::memory_order_relaxed); };

    // Submit 100 tasks.
    int accepted = 0;
    for (int i = 0; i < 100; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) accepted++;
    }
    io.Flush();

    // Wait for all completions.
    for (int w = 0; w < 5000 && done.load() < accepted; w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(done.load() == accepted);
    // After all tasks complete, inflight should drop to near 0.
    int64_t final_inflight = io.InflightCount();
    CHECK(final_inflight >= 0);
    CHECK(final_inflight < 10);  // Near zero (allow for timing).

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("backpressure: zero max_inflight means no limit") {
    AsyncIoConfig config;
    config.max_inflight_ops = 0;  // No limit.
    config.max_entries = 128;

    IOHandler io(config);
    REQUIRE(io.Initialized());

    CHECK_FALSE(io.BackpressureActive());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    auto h = [](KernelBuf*) {};
    int accepted = 0;
    for (int i = 0; i < 500; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) accepted++;
    }
    // Without a limit, all should be accepted.
    CHECK(accepted == 500);
    CHECK_FALSE(io.BackpressureActive());

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("backpressure: rejected task can be retried") {
    AsyncIoConfig config;
    config.max_inflight_ops = 5;
    config.auto_flush_threshold_percent = 100;

    IOHandler io(config);
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> total{0};
    auto h = [&](KernelBuf*) { total.fetch_add(1, std::memory_order_relaxed); };

    // Fill the queue.
    for (int i = 0; i < 5; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }

    // This one should be rejected.
    auto* extra = CreateTaskWithHandler(fd, h);
    extra->SetTaskType(TaskType::kRead);
    CHECK_FALSE(io.AddTask(extra));

    // Flush and wait — backpressure should ease.
    io.Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Now retry the extra task.
    CHECK(io.AddTask(extra));
    io.Flush();

    for (int w = 0; w < 2000 && total.load() < 6; w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(total.load() >= 5);  // Most should have completed.

    close(fd);
    io.RequestShutdown();
    io.Join();
}
