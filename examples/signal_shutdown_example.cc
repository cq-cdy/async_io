// ============================================================================
// Example: Graceful shutdown via SIGINT/SIGTERM
//
// Demonstrates: InstallSignalHandlers(), RequestShutdown(), Join().
//               The IOHandler stops cleanly when Ctrl+C is pressed.
//               Also shows how to use OnSignal() from a custom handler.
// ============================================================================

#include <fcntl.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;

// Repeat-forever read handler — keeps reading until shutdown.
void RepeatReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    if (bytes <= 0) {
        close(buf->ActiveFileDescriptor());
        return;  // RepeatForever will re-submit unless EOF
    }
    printf("Read %d bytes from fd=%d\n", bytes, buf->ActiveFileDescriptor());
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    // Open /dev/null for continuous reading.
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    // Create a repeat-forever task.
    auto* task = CreateTaskWithHandler(fd, RepeatReadHandler);
    task->SetTaskType(TaskType::kRead);
    task->SetRepeatForever(true);
    task->SetDebugStr("repeat_null_read");
    io.AddTask(task);

    // ---- Method 1: Use the built-in signal handler ----
    if (!io.InstallSignalHandlers()) {
        printf("InstallSignalHandlers failed — "
               "another IOHandler already registered.\n");
        printf("Falling back to manual signal setup.\n");

        // ---- Method 2: Register your own signal handler ----
        // IOHandler::OnSignal() is async-signal-safe and can be
        // passed directly to sigaction().
        struct sigaction sa = {};
        sa.sa_handler = &IOHandler::OnSignal;
        sa.sa_flags   = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT,  &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

    printf("Event loop running. Press Ctrl+C to stop.\n");

    // Block until the signal arrives and the event loop exits.
    io.Join();

    close(fd);
    printf("Clean shutdown complete.\n");
    return 0;
}
