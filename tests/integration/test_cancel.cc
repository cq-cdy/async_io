#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("cancel task that was submitted") {
    IOHandler io;
    REQUIRE(io.initialized());

    int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    REQUIRE(fd >= 0);

    std::atomic<bool> handler_called{false};
    auto h = [&](KernelBuf*) { handler_called.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->set_task_type(TaskType::kRead);
    t->set_timeout(5000);
    io.AddTask(t);
    io.Flush();

    // Immediately cancel.
    t->Cancel(io.iouring());

    // Wait a bit.
    for (int i = 0; i < 200; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("cancel with nullptr uring returns zero") {
    auto* t = CreateTaskWithHandler(1);
    t->set_task_type(TaskType::kRead);
    __u64 r = t->Cancel(nullptr);
    CHECK(r == 0);
    delete t;
}
