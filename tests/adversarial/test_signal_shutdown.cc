// ============================================================================
// Signal Shutdown Tests — Verify graceful shutdown via signal and direct call.
// ============================================================================

#include <talon/async_io.hpp>
#include <atomic>
#include <csignal>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("signal: InstallSignalHandlers succeeds") {
    IOHandler io;
    REQUIRE(io.Initialized());

    bool installed = io.InstallSignalHandlers();
    CHECK(installed);

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("signal: only one IOHandler can install signal handlers") {
    IOHandler io1;
    REQUIRE(io1.Initialized());

    bool first = io1.InstallSignalHandlers();
    CHECK(first);

    IOHandler io2;
    REQUIRE(io2.Initialized());

    bool second = io2.InstallSignalHandlers();
    CHECK_FALSE(second);  // Already taken by io1.

    io1.RequestShutdown();
    io1.Join();
    io2.RequestShutdown();
    io2.Join();
}

TEST_CASE("signal: RequestShutdown is safe to call multiple times") {
    IOHandler io;
    REQUIRE(io.Initialized());

    io.RequestShutdown();
    io.RequestShutdown();  // Double call.
    io.RequestShutdown();  // Triple call.
    io.Join();

    MESSAGE("Multiple RequestShutdown calls are safe");
}

TEST_CASE("signal: shutdown while tasks are pending") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<int> completed{0};
    auto h = [&](KernelBuf*) { completed.fetch_add(1, std::memory_order_relaxed); };

    // Submit tasks but don't flush — shutdown should still work.
    for (int i = 0; i < 100; i++) {
        auto* t = CreateTaskWithHandler(fd, h);
        t->SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Shutdown should wake the event loop even if it's waiting.
    io.RequestShutdown();
    io.Join();

    // Some tasks should have completed before shutdown.
    CHECK(completed.load() > 0);

    close(fd);
}

TEST_CASE("signal: OnSignal static method is safe") {
    IOHandler io;
    REQUIRE(io.Initialized());

    REQUIRE(io.InstallSignalHandlers());

    // Simulate signal delivery by calling OnSignal directly.
    // This should be safe even if called from a signal context.
    IOHandler::OnSignal(SIGTERM);

    // The event loop should have been requested to shut down.
    io.Join();

    MESSAGE("OnSignal delivered");
}

TEST_CASE("signal: shutdown then join twice is safe") {
    IOHandler io;
    REQUIRE(io.Initialized());

    io.RequestShutdown();
    io.Join();
    io.Join();  // Second join should be safe (no-op).

    MESSAGE("Double join is safe");
}
