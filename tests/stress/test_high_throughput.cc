#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("high throughput many reads from pipe") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    constexpr int kNumTasks = 500;
    std::atomic<int> completed{0};

    auto handler = [&](KernelBuf* /*unused*/) {
        completed.fetch_add(1, std::memory_order_relaxed);
    };

    // Write data to the pipe so reads don't hit EOF immediately.
    const char data = 'x';
    write(pipefd[1], &data, 1);

    for (int i = 0; i < kNumTasks; i++) {
        auto* t = CreateTaskWithHandler(pipefd[0], handler);
        t->SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    for (int w = 0; w < 10000; w++) {
        if (completed.load(std::memory_order_relaxed) >= kNumTasks) break;
        write(pipefd[1], &data, 1);  // Keep feeding the pipe.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(completed.load(std::memory_order_relaxed) >= 1);  // At least some completed.

    close(pipefd[0]);
    close(pipefd[1]);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("high throughput rapid submit with write") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    std::atomic<int> total{0};

    auto handler = [&](KernelBuf*) {
        total.fetch_add(1, std::memory_order_relaxed);
    };

    // Submit in batches, flushing between.
    for (int batch = 0; batch < 5; batch++) {
        write(pipefd[1], "aaaa", 4);
        for (int i = 0; i < 20; i++) {
            auto* t = CreateTaskWithHandler(pipefd[0], handler);
            t->SetTaskType(TaskType::kRead);
            io.AddTask(t);
        }
        io.Flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(total.load(std::memory_order_relaxed) >= 1);

    close(pipefd[0]);
    close(pipefd[1]);
    io.RequestShutdown();
    io.Join();
}
