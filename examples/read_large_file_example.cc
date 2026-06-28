#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

IOHandler io;

void ReadChunkHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd = buf->ActiveFileDescriptor();
    printf("ReadChunkHandler: %d bytes from fd=%d\n", bytes, fd);
    for (int i = 0; i < bytes; i++) printf("%c", (*buf)[i]);

    auto* next = CreateTaskWithHandler(fd, ReadChunkHandler);
    next.SetTaskType(TaskType::kRead);
    next->Buffer().SetFdOffset(buf->FdOffset() + bytes);
    io.AddTask(next);
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n", io.InitError().c_str());
        return EXIT_FAILURE;
    }

    int fd = open("./async_io.hpp", O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    auto* task = CreateTaskWithHandler(fd, ReadChunkHandler);
    task.SetTaskType(TaskType::kRead);
    io.AddTask(task);

    // Run until the file is fully read (chunks chain themselves).
    // In a real application, use a signal handler or completion callback.
    sleep(10);
    io.RequestShutdown();
    io.Join();
    close(fd);
    return 0;
}
