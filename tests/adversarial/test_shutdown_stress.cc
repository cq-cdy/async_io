#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("shutdown while tasks are in flight") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> completed{0};
    auto h = [&](KernelBuf*) { completed.fetch_add(1, std::memory_order_relaxed); };

    // Submit many tasks.
    for (int i = 0; i < 200; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    // Give some time for processing, then request shutdown.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    io.RequestShutdown();
    io.Join();

    // Some tasks should have completed.
    CHECK(completed.load(std::memory_order_relaxed) > 0);
    close(fd);
}

TEST_CASE("double shutdown is safe") {
    IOHandler io;
    REQUIRE(io.Initialized());

    io.RequestShutdown();
    io.RequestShutdown();  // Double shutdown — should be safe.
    io.Join();

    MESSAGE("double shutdown ok");
}

TEST_CASE("shutdown without any tasks submitted") {
    IOHandler io;
    REQUIRE(io.Initialized());

    // No tasks — just start and stop.
    io.RequestShutdown();
    io.Join();

    MESSAGE("empty shutdown ok");
}

TEST_CASE("rapid init shutdown cycles") {
    for (int i = 0; i < 10; i++) {
        IOHandler io;
        if (io.Initialized()) {
            io.RequestShutdown();
            io.Join();
        }
    }
    MESSAGE("10 init/shutdown cycles ok");
}
