#include <talon/async_io.hpp>
#include <atomic>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("adversarial read fd=-1") {
    IOHandler io; REQUIRE(io.initialized());
    std::atomic<bool> done{false}; int res = 0;
    auto h = [&](KernelBuf* b) { res = b->bytes_transferred(); done.store(true); };
    auto* t = CreateTaskWithHandler(-1, h); t->set_task_type(TaskType::kRead);
    io.AddTask(t); io.Flush();
    for (int i = 0; i < 1000 && !done.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(done.load()); CHECK(res < 0);
    io.RequestShutdown(); io.Join();
}
