// ============================================================================
// Example: TCP Client — multi-message write+read with repeat-forever
//
// Demonstrates: TcpClient, repeat-forever read, multiple writes,
//               SetNextTask for connect-then-read pattern,
//               manual shutdown after receiving expected response count.
// ============================================================================

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;
static int g_responses_needed = 3;
static int g_responses_seen   = 0;

// ---- Read handler: receives echo responses ----
void ClientReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes <= 0) {
        close(fd);
        return;
    }

    g_responses_seen++;
    printf("[response %d/%d] %.*s\n",
           g_responses_seen, g_responses_needed, bytes, buf->Data());

    if (g_responses_seen >= g_responses_needed) {
        printf("All responses received. Shutting down.\n");
        close(fd);
        io.RequestShutdown();
    }
}

// ---- Connect complete: start reading, then send messages ----
void ConnectCompleteHandler(KernelBuf* buf) {
    int ret = buf->BytesTransferred();
    int fd  = buf->ActiveFileDescriptor();

    if (ret < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(-ret));
        close(fd);
        io.RequestShutdown();
        return;
    }
    printf("Connected fd=%d\n", fd);

    // Set up repeat-forever read to receive all responses.
    auto* rt = CreateTaskWithHandler(fd, ClientReadHandler);
    rt->SetTaskType(TaskType::kRead);
    rt->SetRepeatForever(true);
    rt->SetDebugStr("client_repeat_read");
    io.AddTask(rt);

    // Send 3 messages.
    const char* messages[] = {
        "Hello server!\n",
        "How are you?\n",
        "Goodbye!\n"
    };
    for (int i = 0; i < 3; i++) {
        auto* wt = CreateTaskWithHandler(fd);
        wt->SetTaskType(TaskType::kWrite);
        wt->Buffer()->Resize(std::strlen(messages[i]));
        std::memcpy(wt->Buffer()->Data(), messages[i],
                    std::strlen(messages[i]));
        wt->SetDebugStr("client_write_" + std::to_string(i));
        io.AddTask(wt);
    }
    printf("Sent 3 messages to server\n");
}

// ---- Main ----
int main(int argc, char* argv[]) {
    const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port       = (argc > 2) ? atoi(argv[2]) : 8081;

    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    TcpClient client(io);
    printf("Connecting to echo server at %s:%d ...\n", ip, port);

    auto* ct = client.Connect(ip, port, ConnectCompleteHandler);
    if (ct == nullptr) {
        fprintf(stderr, "Connect failed: %s\n",
                client.LastError().c_str());
        return EXIT_FAILURE;
    }
    io.AddTask(ct);

    io.Join();
    printf("TCP write+read demo complete.\n");
    return 0;
}
