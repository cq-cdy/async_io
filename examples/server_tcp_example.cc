#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

IOHandler io;
TcpServer server(io);

void EchoHandler(KernelBuf* buf) {
    int bytes = buf->bytes_transferred();
    int fd = buf->active_file_descriptor();
    printf("EchoHandler: %d bytes fd=%d\n", bytes, fd);

    std::string original(buf->data(), buf->data() + bytes);
    std::string reversed = original;
    std::reverse(reversed.begin(), reversed.end());
    std::string resp = "peer fd=[" + std::to_string(fd) + "]: " + reversed;

    auto wb = MakeKernelBuffer(resp.size());
    std::copy(resp.begin(), resp.end(), wb->data());

    auto* wt = CreateTaskWithHandler(fd);
    wt->set_task_type(TaskType::kWrite);
    wt->SetBuffer(std::move(wb));
    io.AddTask(wt);
}

void AcceptHandler(KernelBuf* buf) {
    int client_fd = buf->bytes_transferred();
    printf("Accepted fd=%d\n", client_fd);
    auto* rt = CreateTaskWithHandler(client_fd, EchoHandler);
    rt->set_task_type(TaskType::kRead);
    rt->set_repeat_forever(true);
    io.AddTask(rt);
}

int main() {
    if (!io.initialized()) {
        fprintf(stderr, "IOHandler init failed: %s\n", io.init_error().c_str());
        return EXIT_FAILURE;
    }
    if (!server.Start(8081)) {
        fprintf(stderr, "TcpServer failed: %s\n", server.last_error().c_str());
        return EXIT_FAILURE;
    }

    auto* at = server.CreateAcceptTask(AcceptHandler);
    io.AddTask(at);

    printf("TCP echo server running on port 8081. Press Ctrl+C to stop.\n");
    sleep(UINT_MAX);
    return 0;
}
