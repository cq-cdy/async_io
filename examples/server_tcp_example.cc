// ============================================================================
// Example: TCP Echo Server with graceful shutdown
//
// Demonstrates: TcpServer, TaskType::kAccept, repeat-forever pattern,
//               TaskType::kRead + TaskType::kWrite for echo,
//               signal-based shutdown via InstallSignalHandlers().
// ============================================================================

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

static IOHandler io;
static TcpServer server(io);

// ---- Echo handler: reads data, reverses it, writes back ----
void EchoHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    int fd    = buf->ActiveFileDescriptor();

    if (bytes <= 0) {
        if (bytes == 0) {
            printf("Client fd=%d disconnected (EOF)\n", fd);
        } else {
            fprintf(stderr, "EchoHandler fd=%d error: %s\n",
                    fd, strerror(-bytes));
        }
        close(fd);
        return;
    }

    printf("EchoHandler: %d bytes from fd=%d\n", bytes, fd);

    std::string original(buf->Data(), buf->Data() + bytes);
    std::string reversed(original.rbegin(), original.rend());
    std::string resp = "echo fd[" + std::to_string(fd) + "]: " + reversed + "\n";

    auto wb = MakeKernelBuffer(resp.size());
    std::memcpy(wb->Data(), resp.data(), resp.size());

    auto* wt = CreateTaskWithHandler(fd);
    wt->SetTaskType(TaskType::kWrite);
    wt->SetBuffer(std::move(wb));
    io.AddTask(wt);
}

// ---- Accept handler: called each time a new client connects ----
void AcceptHandler(KernelBuf* buf) {
    int client_fd = buf->BytesTransferred();
    if (client_fd < 0) {
        fprintf(stderr, "AcceptHandler: error %s\n", strerror(-client_fd));
        return;
    }
    printf("Accepted client fd=%d\n", client_fd);

    auto* rt = CreateTaskWithHandler(client_fd, EchoHandler);
    rt->SetTaskType(TaskType::kRead);
    rt->SetRepeatForever(true);
    rt->SetDebugStr("echo_read_fd=" + std::to_string(client_fd));
    io.AddTask(rt);
}

// ---- Main ----
int main() {
    if (!io.Initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n",
                io.InitError().c_str());
        return EXIT_FAILURE;
    }

    if (!server.Start(8081)) {
        fprintf(stderr, "TcpServer failed: %s\n",
                server.LastError().c_str());
        return EXIT_FAILURE;
    }
    printf("TCP echo server listening on port 8081\n");

    if (!io.InstallSignalHandlers()) {
        fprintf(stderr, "Warning: could not install signal handlers "
                "(another IOHandler may be active)\n");
    }
    printf("Press Ctrl+C to stop.\n");

    auto* at = server.CreateAcceptTask(AcceptHandler);
    io.AddTask(at);

    io.Join();
    printf("Server shut down cleanly.\n");
    return 0;
}
