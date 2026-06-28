// ============================================================================
// Example: Task Chain — sequential I/O with SetNextTask()
//
// Demonstrates: Explicit task chaining via AsyncTask::SetNextTask().
//               Three tasks run in sequence: read → write → read.
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
static const char* kTmpFile = "/tmp/async_io_chain_demo.txt";

// ---- Step 3: Read back what we wrote ----
void ReadBackHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();
    if (bytes >= 0) {
        printf("Step 3 (read back): %d bytes = '%.*s'\n",
               bytes, bytes, buf->Data());
    }
    close(fd);
    unlink(kTmpFile);
    io.RequestShutdown();
}

// ---- Step 2: Write content to the file ----
void WriteHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();
    if (bytes < 0) {
        fprintf(stderr, "Write failed: %s\n", strerror(-bytes));
        close(fd);
        io.RequestShutdown();
        return;
    }
    printf("Step 2 (write): %d bytes written\n", bytes);
    close(fd);

    // Chain Step 3: open the same file for reading.
    int rfd = open(kTmpFile, O_RDONLY);
    auto* rtask = CreateTaskWithHandler(rfd, ReadBackHandler);
    rtask->SetTaskType(TaskType::kRead);
    io.AddTask(rtask);
}

// ---- Step 1: Open the file, then chain Step 2 ----
// This demonstrates explicit SetNextTask() chaining.

int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    // Create test file.
    int fd = open(kTmpFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    const char* content = "Hello, Task Chain!";

    // Step 1 task: write.
    auto* wtask = CreateTaskWithHandler(fd, WriteHandler);
    wtask->SetTaskType(TaskType::kWrite);
    wtask->Buffer()->Resize(std::strlen(content));
    std::memcpy(wtask->Buffer()->Data(), content, std::strlen(content));
    wtask->SetDebugStr("chain_step1_write");

    io.AddTask(wtask);
    printf("Step 1: writing '%s' to %s\n", content, kTmpFile);

    io.Join();
    printf("Task chain complete.\n");
    return 0;
}
