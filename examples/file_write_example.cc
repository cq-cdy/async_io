// ============================================================================
// Example: Asynchronous file write + read-back verification
//
// Demonstrates: TaskType::kWrite, TaskType::kRead, chained write-then-read
//               pattern, DeepCopyBufferFrom, WaitForCompletion.
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
static const char* kTmpFile = "/tmp/async_io_write_demo.txt";

// ---- Step 2: Read back and verify ----
void VerifyReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes < 0) {
        fprintf(stderr, "Verify read failed: %s\n", strerror(-bytes));
    } else {
        printf("Read back %d bytes: %.*s\n", bytes, bytes, buf->Data());
    }
    close(fd);
    unlink(kTmpFile);
    io.RequestShutdown();
}

// ---- Step 1: Write to file ----
void WriteCompleteHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes < 0) {
        fprintf(stderr, "Write failed: %s\n", strerror(-bytes));
        close(fd);
        unlink(kTmpFile);
        io.RequestShutdown();
        return;
    }
    printf("Wrote %d bytes to fd=%d\n", bytes, fd);
    close(fd);

    // Verify: open and read back.
    int rfd = open(kTmpFile, O_RDONLY);
    if (rfd < 0) {
        perror("open for verify");
        io.RequestShutdown();
        return;
    }
    auto* rt = CreateTaskWithHandler(rfd, VerifyReadHandler);
    rt->SetTaskType(TaskType::kRead);
    io.AddTask(rt);
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    int fd = open(kTmpFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    const char* content = "Hello, async I/O world!\nThis is a write demo.\n";

    auto* task = CreateTaskWithHandler(fd, WriteCompleteHandler);
    task->SetTaskType(TaskType::kWrite);
    task->SetDebugStr("file_write_demo");

    // Copy content into the task's I/O buffer.
    task->Buffer()->Resize(std::strlen(content));
    std::memcpy(task->Buffer()->Data(), content, std::strlen(content));

    io.AddTask(task);
    printf("Writing %zu bytes to %s...\n", std::strlen(content), kTmpFile);

    io.Join();
    printf("File write demo complete.\n");
    return 0;
}
