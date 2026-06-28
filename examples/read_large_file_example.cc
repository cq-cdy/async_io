// ============================================================================
// Example: Chunked asynchronous file read (large file streaming)
//
// Demonstrates: Task chaining via handler, fd_offset for seek, KernelBuf
//               Resize, repeat-forever vs one-shot patterns.
// ============================================================================

#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

// Global IOHandler so the handler can re-submit chains.
static IOHandler io;

// Each invocation reads one chunk, prints it, and creates the next chunk.
void ReadChunkHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes < 0) {
        fprintf(stderr, "ReadChunkHandler: error fd=%d: %s\n",
                fd, strerror(-bytes));
        close(fd);
        io.RequestShutdown();
        return;
    }
    if (bytes == 0) {
        printf("\n--- EOF (fd=%d) ---\n", fd);
        close(fd);
        io.RequestShutdown();
        return;
    }

    printf("[chunk fd=%d offset=%lld size=%d]\n",
           fd, (long long)buf->FdOffset(), bytes);
    for (int i = 0; i < bytes; i++) putchar(buf->Data()[i]);

    // Chain the next chunk.
    int64_t next_offset = buf->FdOffset() + bytes;
    auto* next = CreateTaskWithHandler(fd, ReadChunkHandler);
    next->SetTaskType(TaskType::kRead);
    next->Buffer()->Resize(32);
    next->Buffer()->SetFdOffset(next_offset);
    io.AddTask(next);
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    int fd = open("./include/talon/async_io.hpp", O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    auto* task = CreateTaskWithHandler(fd, ReadChunkHandler);
    task->SetTaskType(TaskType::kRead);
    task->SetDebugStr("chunked_read");
    task->Buffer()->Resize(32);
    task->Buffer()->SetFdOffset(0);

    if (!io.AddTask(task)) {
        fprintf(stderr, "AddTask failed\n");
        close(fd);
        return EXIT_FAILURE;
    }

    io.Join();
    printf("\nChunked file read complete.\n");
    return 0;
}
