#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("repeat forever re-submits on success") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> count{0};
    auto h = [&](KernelBuf*) { count.fetch_add(1, std::memory_order_relaxed); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    t->set_repeat_forever(true);
    io.AddTask(t);
    io.Flush();

    // Let it run for a bit — should repeat several times.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int c = count.load(std::memory_order_relaxed);
    CHECK(c > 1);  // Should have repeated at least once

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("repeat forever set to false is one-shot") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> count{0};
    auto h = [&](KernelBuf*) { count.fetch_add(1, std::memory_order_relaxed); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    // repeat_forever is false by default — should fire once.
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 200 && count.load(std::memory_order_relaxed) == 0; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // After completion, wait a bit more to make sure it doesn't fire again.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(count.load(std::memory_order_relaxed) == 1);

    close(fd);
    io.RequestShutdown();
    io.Join();
}
