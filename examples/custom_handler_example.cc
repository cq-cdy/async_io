// ============================================================================
// Example: Custom handler with user-provided arguments
//
// Demonstrates: CreateTaskWithHandler with extra user arguments,
//               FunctionTraits compile-time validation, multi-file read.
// ============================================================================

#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;

// Handler with extra user arguments: file_id (string) and chunk_number (int).
// Signature must be: void(KernelBuf*, UserArg1, UserArg2, ...)
void AnnotatedReadHandler(KernelBuf* buf, const std::string& file_id,
                          int chunk_number) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    printf("[%s chunk=%d] fd=%d, bytes=%d\n",
           file_id.c_str(), chunk_number, fd, bytes);

    if (bytes <= 0) {
        close(fd);
        io.RequestShutdown();
        return;
    }
    printf("[%s chunk=%d] data: %.*s\n",
           file_id.c_str(), chunk_number, bytes, buf->Data());
    close(fd);
    io.RequestShutdown();
}

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    int fd = open(__FILE__, O_RDONLY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    // Bind extra arguments: file_id="source", chunk_number=1.
    // CreateTaskWithHandler handles argument binding via std::bind.
    auto* task = CreateTaskWithHandler(
        fd, AnnotatedReadHandler,
        std::string("source_file"),  // user arg 1
        1);                           // user arg 2

    task->SetTaskType(TaskType::kRead);
    task->SetDebugStr("custom_handler_demo");
    io.AddTask(task);

    io.Join();
    printf("Custom handler demo complete.\n");
    return 0;
}
