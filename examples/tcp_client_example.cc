// ============================================================================
// Example: TCP Client — connect, write, read response
//
// Demonstrates: TcpClient::Connect(), TaskType::kConnect,
//               TaskType::kWrite, TaskType::kRead, task chaining
//               with SetNextTask() for sequential operations.
// ============================================================================

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;

// ---- Step 3: Read the server response ----
void ResponseHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes <= 0) {
        fprintf(stderr, "ResponseHandler fd=%d: %s\n",
                fd, bytes == 0 ? "EOF" : strerror(-bytes));
        close(fd);
        io.RequestShutdown();
        return;
    }

    printf("Server response (%d bytes): %.*s\n", bytes, bytes, buf->Data());
    close(fd);
    io.RequestShutdown();
}

// ---- Step 2: After connect + write, start reading ----
void WriteDoneHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes < 0) {
        fprintf(stderr, "Write failed fd=%d: %s\n", fd, strerror(-bytes));
        close(fd);
        io.RequestShutdown();
        return;
    }
    printf("Sent %d bytes to server fd=%d\n", bytes, fd);

    // Now create a read task to get the response from the server.
    auto* rt = CreateTaskWithHandler(fd, ResponseHandler);
    rt->SetTaskType(TaskType::kRead);
    io.AddTask(rt);
}

int main(int argc, char* argv[]) {
    const char* server_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int server_port       = (argc > 2) ? atoi(argv[2]) : 8081;

    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    TcpClient client(io);
    printf("Connecting to %s:%d ...\n", server_ip, server_port);

    // Connect. The returned task has TaskType::kConnect.
    auto* ct = client.Connect(server_ip, server_port);
    if (ct == nullptr) {
        fprintf(stderr, "Connect failed: %s\n",
                client.LastError().c_str());
        return EXIT_FAILURE;
    }
    ct->SetDebugStr("tcp_connect");

    // Prepare the write task that fires after connect succeeds.
    const char* msg = "Hello from async_io client!\n";
    auto* wt = CreateTaskWithHandler(ct->Fd(), WriteDoneHandler);
    wt->SetTaskType(TaskType::kWrite);
    wt->Buffer()->Resize(std::strlen(msg));
    std::memcpy(wt->Buffer()->Data(), msg, std::strlen(msg));

    // Chain: connect -> write -> read (chained inside WriteDoneHandler).
    ct->SetNextTask(wt);
    io.AddTask(ct);

    printf("Connected, sending: %s", msg);

    // Wait for the full chain to complete.
    io.Join();
    printf("Client done.\n");
    return 0;
}
