// ============================================================================
// Example: Basic asynchronous file read
//
// Demonstrates: IOHandler setup, TaskType::kRead, handler-driven I/O,
//               WaitForCompletion, graceful shutdown.
// ============================================================================

#include <fcntl.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

// Handler receives the completed I/O result via KernelBuf.
void ReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();
    if (bytes < 0) {
        fprintf(stderr, "ReadHandler: I/O error fd=%d: %s\n",
                fd, strerror(-bytes));
        return;
    }
    printf("ReadHandler: %d bytes from fd=%d\n", bytes, fd);
    printf("Content: %.*s\n", bytes, buf->Data());
}

int main() {
    // Step 1: Create the event loop.
    IOHandler io;
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }
    printf("IOHandler initialized with max_entries=%d\n",
           io.Config().max_entries);

    // Step 2: Open the file.
    int fd = open(__FILE__, O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    // Step 3: Create a read task with our handler.
    auto* task = CreateTaskWithHandler(fd, ReadHandler);
    task->SetTaskType(TaskType::kRead);
    task->SetDebugStr("read_file_example");

    // Step 4: Submit the task to the event loop.
    if (!io.AddTask(task)) {
        fprintf(stderr, "AddTask failed\n");
        close(fd);
        return EXIT_FAILURE;
    }

    // Step 5: Wait for the I/O to complete.
    IOResult result = task->WaitForCompletion();
    printf("IOResult: io_ret=%d, event_fd=%d, iodone=%d, err_msg=%s\n",
           result.IoRet(), result.EventFd(), result.IoDone(),
           result.ErrMsg().c_str());

    // Step 6: Clean shutdown.
    io.RequestShutdown();
    io.Join();
    close(fd);
    printf("IOHandler shut down cleanly.\n");
    return 0;
}
