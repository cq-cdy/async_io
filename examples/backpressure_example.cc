// ============================================================================
// Example: Backpressure — limiting in-flight operations
//
// Demonstrates: AsyncIoConfig::max_inflight_ops, InflightCount(),
//               BackpressureActive(), retry loop with sleep back-off.
// ============================================================================

#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

int main() {
    // Configure a small ring with inflight limit.
    AsyncIoConfig config;
    config.max_entries = 64;
    config.max_inflight_ops = 10;     // Only 10 ops in flight at a time.
    config.backpressure_retry_us = 100;

    IOHandler io(config);
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    printf("max_inflight_ops=%d, backpressure_retry_us=%d\n",
           config.max_inflight_ops, config.backpressure_retry_us);

    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    std::atomic<int> done{0};
    auto handler = [&](KernelBuf*) {
        done.fetch_add(1, std::memory_order_relaxed);
    };

    // Submit far more tasks than the limit allows.
    int accepted = 0;
    int rejected = 0;
    for (int i = 0; i < 100; i++) {
        auto* t = CreateTaskWithHandler(fd, handler);
        t->SetTaskType(TaskType::kRead);

        // Retry loop with back-off when backpressure is active.
        while (!io.AddTask(t)) {
            rejected++;
            if (io.BackpressureActive()) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(config.backpressure_retry_us));
            }
        }
        accepted++;

        // Periodically flush to drain completions.
        if (i % 20 == 0) { io.Flush(); }
    }

    printf("Accepted: %d, Rejected (retried): %d\n", accepted, rejected);
    printf("Inflight before flush: %lld\n",
           (long long)io.InflightCount());

    // Drain all remaining work.
    io.Flush();
    for (int w = 0; w < 5000 && done.load() < accepted; w++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    printf("Completed: %d, Final inflight: %lld\n",
           done.load(), (long long)io.InflightCount());

    close(fd);
    io.RequestShutdown();
    io.Join();
    printf("Backpressure demo complete.\n");
    return 0;
}
