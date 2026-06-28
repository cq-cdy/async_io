#include <talon/async_io.hpp>
#include <atomic>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

// ============================================================================
// Double-submit prevention: the CAS gate must reject the second submission
// of the same task pointer, even when called from the same thread.
// ============================================================================

TEST_CASE("adversarial double submit same thread") {
    IOHandler io;
    REQUIRE(io.Initialized());
    auto* t = CreateTaskWithHandler(1);
    t->SetTaskType(TaskType::kRead);
    CHECK(io.AddTask(t));
    CHECK_FALSE(io.AddTask(t));  // CAS gate rejects duplicate
    io.RequestShutdown();
    io.Join();
}

// ============================================================================
// Concurrent submission of independent tasks: each thread creates and
// submits its own task.  The CAS gate per task must allow both.
// ============================================================================

TEST_CASE("adversarial concurrent submit two independent tasks") {
    IOHandler io;
    REQUIRE(io.Initialized());
    std::atomic<int> ok{0};
    auto fn = [&] {
        auto* t = CreateTaskWithHandler(1);
        t->SetTaskType(TaskType::kRead);
        if (io.AddTask(t)) ok.fetch_add(1, std::memory_order_relaxed);
    };
    std::thread t1(fn), t2(fn);
    t1.join();
    t2.join();
    CHECK(ok.load(std::memory_order_relaxed) == 2);  // Both independent tasks accepted
    io.RequestShutdown();
    io.Join();
}
