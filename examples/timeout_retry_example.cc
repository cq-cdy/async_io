// ============================================================================
// Example: Timeout and Retry
//
// Demonstrates: SetTimeout(), SetMaxRetryCount(), repeat-when-failed.
//               Reads from /dev/random (may stall) with a 10ms timeout.
// ============================================================================

#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;
static int g_attempts = 0;

// Handler with timeout + retry support.
void TimeoutAwareHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();
    g_attempts++;

    printf("Attempt #%d: bytes_transferred=%d, fd=%d\n",
           g_attempts, bytes, fd);

    if (bytes < 0) {
        if (bytes == -ETIME || bytes == -ECANCELED) {
            printf("  -> Timed out or cancelled (attempt %d)\n", g_attempts);
        } else {
            fprintf(stderr, "  -> Error: %s\n", strerror(-bytes));
        }
        return;  // Retry logic handles re-submission.
    }
    if (bytes == 0) {
        printf("  -> EOF\n");
        close(fd);
        io.RequestShutdown();
        return;
    }
    printf("  -> Read %d bytes successfully!\n", bytes);
    close(fd);
    io.RequestShutdown();
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    // /dev/random may block waiting for entropy — perfect for timeout demo.
    int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/random");
        return EXIT_FAILURE;
    }

    auto* task = CreateTaskWithHandler(fd, TimeoutAwareHandler);
    task->SetTaskType(TaskType::kRead);
    task->SetDebugStr("timeout_demo");

    // --- Timeout: the read will fail with -ETIME after 10ms ---
    task->SetTimeout(10);

    // --- Retry: retry up to 3 times on failure ---
    task->SetMaxRetryCount(3);
    // repeat_when_failed() returns true when max_retry_count > 0

    if (!io.AddTask(task)) {
        fprintf(stderr, "AddTask failed\n");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Reading from /dev/random with 10ms timeout, max 3 retries...\n");

    io.Join();
    printf("Finished after %d attempts.\n", g_attempts);
    return 0;
}
