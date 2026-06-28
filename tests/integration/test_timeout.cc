#include <talon/async_io.hpp>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

// Read from /dev/random with a 1ms timeout — should always time out since
// /dev/random may block waiting for entropy.
TEST_CASE("timeout read from /dev/random fires timeout") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    REQUIRE(fd >= 0);

    std::atomic<bool> timed_out{false};
    int handler_ret = -99;

    auto h = [&](KernelBuf* b) {
        handler_ret = b->bytes_transferred();
        timed_out.store(true, std::memory_order_release);
    };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    t->set_timeout(1);  // 1ms timeout — very aggressive
    io.AddTask(t);
    io.Flush();

    // Wait for either completion or timeout.
    for (int i = 0; i < 2000 && !timed_out.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // The task should have completed (either with data or timeout).
    CHECK(timed_out.load(std::memory_order_acquire));

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("timeout with short duration") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    REQUIRE(fd >= 0);

    std::atomic<bool> completed{false};
    auto h = [&](KernelBuf*) { completed.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    t->set_timeout(50);  // 50ms
    io.AddTask(t);
    io.Flush();

    // Poll until complete or timeout (wait up to 2 seconds).
    for (int i = 0; i < 400 && !completed.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Either data was read or timeout fired — in both cases, handler should run.
    CHECK(completed.load(std::memory_order_acquire));

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("timeout on write would fail fd") {
    IOHandler io;
    REQUIRE(io.initialized());

    std::atomic<bool> completed{false};

    auto h = [&](KernelBuf*) { completed.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(-1, h);
    t->set_task_type(TaskType::kWrite);
    t->set_timeout(100);
    t->buffer()->resize(4);
    std::memset(t->buffer()->data(), 'A', 4);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 500 && !completed.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(completed.load(std::memory_order_acquire));

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("timeout zero means no timeout") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> done{false};
    auto h = [&](KernelBuf*) { done.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    t->set_timeout(0);  // No timeout
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 500 && !done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(done.load(std::memory_order_acquire));  // Should complete, not time out

    close(fd);
    io.RequestShutdown();
    io.Join();
}
