#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("lifetime task destroyed before IOHandler shutdown") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> handler_called{false};
    auto h = [&](KernelBuf*) { handler_called.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    // Wait for handler to be called (task implicitly deleted by IOHandler).
    for (int i = 0; i < 1000 && !handler_called.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(handler_called.load(std::memory_order_acquire));

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("lifetime IOHandler destroyed after tasks complete") {
    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    {
        IOHandler io;
        REQUIRE(io.Initialized());

        std::atomic<bool> done{false};
        auto h = [&](KernelBuf*) { done.store(true, std::memory_order_release); };

        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        io.AddTask(t);
        io.Flush();

        for (int i = 0; i < 500 && !done.load(std::memory_order_acquire); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        CHECK(done.load(std::memory_order_acquire));
        io.RequestShutdown();
        io.Join();
        // IOHandler destroyed here.
    }

    close(fd);
    MESSAGE("IOHandler destroyed cleanly");
}

TEST_CASE("lifetime WaitForCompletion on never-submitted task") {
    auto* t = CreateTaskWithHandler(0);
    auto r = t->WaitForCompletion(100);
    CHECK_FALSE(r.IoDone());
    CHECK(r.ErrMsg().find("not detached") != std::string::npos);
    delete t;
}

TEST_CASE("lifetime WaitForCompletion on completed task") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> handler_done{false};
    auto h = [&](KernelBuf*) { handler_done.store(true, std::memory_order_release); };

    auto* t = CreateTaskWithHandler(fd, h);
    t->SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 500 && !handler_done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(handler_done.load(std::memory_order_acquire));

    auto r = t->WaitForCompletion(100);
    // Task should be detached and completed.
    CHECK(r.IoDone());

    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("lifetime task object pool reuse across many create/delete") {
    constexpr int kCycles = 500;
    for (int i = 0; i < kCycles; i++) {
        auto* t = CreateTaskWithHandler(i % 100);
        t->SetTaskType(TaskType::kRead);
        delete t;
    }
    MESSAGE("500 create/delete cycles ok");
}
