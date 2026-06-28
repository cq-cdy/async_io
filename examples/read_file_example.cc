#include <fcntl.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

void ReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd = buf->ActiveFileDescriptor();
    printf("ReadHandler: %d bytes from fd=%d\n", bytes, fd);
    for (int i = 0; i < bytes; i++) printf("%c", buf->Data()[i]);
}

int main() {
    IOHandler io;
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n", io.InitError().c_str());
        return EXIT_FAILURE;
    }

    int fd = open(__FILE__, O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    auto* task = CreateTaskWithHandler(fd, ReadHandler);
    task.SetTaskType(TaskType::kRead);
    io.AddTask(task);

    // Wait for I/O completion, then perform orderly shutdown.
    task->WaitForCompletion();
    io.RequestShutdown();
    io.Join();
    close(fd);
    return 0;
}
