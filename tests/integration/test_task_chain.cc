#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("task chain two tasks sequential") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> order{0};
    std::atomic<bool> first_done{false};
    std::atomic<bool> second_done{false};

    auto handler1 = [&](KernelBuf*) {
        order.fetch_add(1, std::memory_order_relaxed);
        first_done.store(true, std::memory_order_release);
    };
    auto handler2 = [&](KernelBuf*) {
        order.fetch_add(1, std::memory_order_relaxed);
        second_done.store(true, std::memory_order_release);
    };

    auto* t1 = CreateTaskWithHandler(fd, handler1);
    t1.SetTaskType(TaskType::kRead);

    auto* t2 = CreateTaskWithHandler(fd, handler2);
    t2.SetTaskType(TaskType::kRead);
    t1.SetNextTask(t2);

    io.AddTask(t1);
    io.Flush();

    for (int i = 0; i < 1000 && !second_done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(first_done.load(std::memory_order_acquire));
    CHECK(second_done.load(std::memory_order_acquire));

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("task chain three tasks") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> counter{0};
    auto make_handler = [&]() {
        return [&](KernelBuf*) { counter.fetch_add(1, std::memory_order_relaxed); };
    };

    auto* t1 = CreateTaskWithHandler(fd, make_handler());
    t1.SetTaskType(TaskType::kRead);
    auto* t2 = CreateTaskWithHandler(fd, make_handler());
    t2.SetTaskType(TaskType::kRead);
    auto* t3 = CreateTaskWithHandler(fd, make_handler());
    t3.SetTaskType(TaskType::kRead);

    t1.SetNextTask(t2);
    t2.SetNextTask(t3);

    io.AddTask(t1);
    io.Flush();

    for (int i = 0; i < 1000 && counter.load(std::memory_order_relaxed) < 3; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(counter.load(std::memory_order_relaxed) == 3);

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("task chain next_task nullptr is fine") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> done{false};
    auto* t = CreateTaskWithHandler(fd, [&](KernelBuf*) { done.store(true); });
    t.SetTaskType(TaskType::kRead);
    // No next task set — should still complete normally.
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000 && !done.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(done.load());

    close(fd);
    io.RequestShutdown();
    io.Join();
}
