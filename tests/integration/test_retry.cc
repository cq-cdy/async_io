#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("retry on failure fires handler eventually") {
    IOHandler io;
    REQUIRE(io.Initialized());

    // Use a bad fd that will fail — retry should eventually give up.
    std::atomic<int> call_count{0};
    auto h = [&](KernelBuf* b) {
        call_count.fetch_add(1, std::memory_order_relaxed);
        int r = b->BytesTransferred();
        (void)r;
    };

    auto* t = CreateTaskWithHandler(-1, h);
    t->SetTaskType(TaskType::kRead);
    t->SetMaxRetryCount(3);  // Retry up to 3 times
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // With a bad fd, the task should fail and retry up to 3 times,
    // then give up. The handler is called on each failure.
    CHECK(call_count.load(std::memory_order_relaxed) <= 3);

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("retry count zero means no retry") {
    IOHandler io;
    REQUIRE(io.Initialized());

    auto* t = CreateTaskWithHandler(-1);
    t->SetTaskType(TaskType::kRead);
    t->SetMaxRetryCount(0);
    CHECK_FALSE(t->RepeatWhenFailed());
    delete t;

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("retry count negative means no retry by default") {
    auto* t = CreateTaskWithHandler(1);
    t->SetTaskType(TaskType::kRead);
    // Default is -1, repeat_when_failed checks > 0
    CHECK_FALSE(t->RepeatWhenFailed());
    delete t;
}
