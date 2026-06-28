#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("submit more tasks than max_entries") {
    AsyncIoConfig config;
    config.max_entries = 32;  // Small queue.

    IOHandler io(config);
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> completed{0};
    std::atomic<int> submitted{0};
    auto h = [&](KernelBuf*) { completed.fetch_add(1, std::memory_order_relaxed); };

    // Submit far more tasks than the ring can hold at once.
    for (int i = 0; i < 200; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t.SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) {
            submitted.fetch_add(1, std::memory_order_relaxed);
        }
    }
    io.Flush();

    // Wait for completions.
    for (int w = 0; w < 10000 && completed.load(std::memory_order_relaxed) < submitted.load(); w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(completed.load(std::memory_order_relaxed) == submitted.load());

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("queue full does not crash") {
    AsyncIoConfig config;
    config.max_entries = 16;
    config.auto_flush_threshold_percent = 100;  // Disable auto-flush.

    IOHandler io(config);
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    auto h = [](KernelBuf*) {};

    for (int i = 0; i < 300; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t.SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    close(fd);
    io.RequestShutdown();
    io.Join();
    SUCCEED("queue full handled gracefully");
}
