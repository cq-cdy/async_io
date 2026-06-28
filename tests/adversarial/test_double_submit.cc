#include <talon/async_io.hpp>
#include <atomic>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("adversarial double submit same thread") {
    IOHandler io; REQUIRE(io.initialized());
    auto* t = CreateTaskWithHandler(1); t->set_task_type(TaskType::kRead);
    CHECK(io.AddTask(t)); CHECK_FALSE(io.AddTask(t));
    io.RequestShutdown(); io.Join();
}

TEST_CASE("adversarial double submit two threads") {
    IOHandler io; REQUIRE(io.initialized());
    auto* t = CreateTaskWithHandler(1); t->set_task_type(TaskType::kRead);
    std::atomic<int> ok{0};
    auto fn = [&] { if (io.AddTask(t)) ok.fetch_add(1); };
    std::thread t1(fn), t2(fn); t1.join(); t2.join();
    CHECK(ok.load() == 1);
    io.RequestShutdown(); io.Join();
}
