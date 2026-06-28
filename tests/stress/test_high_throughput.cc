#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("high throughput many reads from /dev/null") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    constexpr int kNumTasks = 1000;
    std::atomic<int> completed{0};

    auto handler = [&](KernelBuf*) {
        completed.fetch_add(1, std::memory_order_relaxed);
    };

    for (int i = 0; i < kNumTasks; i++) {
        auto* t = CreateTaskWithHandler(fd, handler);
        t.SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    for (int w = 0; w < 10000 && completed.load(std::memory_order_relaxed) < kNumTasks; w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(completed.load(std::memory_order_relaxed) == kNumTasks);

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("high throughput rapid submit and complete cycles") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> total{0};

    auto handler = [&](KernelBuf*) {
        total.fetch_add(1, std::memory_order_relaxed);
    };

    // Submit in batches of 100, flushing between batches.
    for (int batch = 0; batch < 10; batch++) {
        for (int i = 0; i < 100; i++) {
            auto* t = CreateTaskWithHandler(fd, handler);
            t.SetTaskType(TaskType::kRead);
            io.AddTask(t);
        }
        io.Flush();
    }

    for (int w = 0; w < 10000 && total.load(std::memory_order_relaxed) < 1000; w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(total.load(std::memory_order_relaxed) == 1000);

    close(fd);
    io.RequestShutdown();
    io.Join();
}
