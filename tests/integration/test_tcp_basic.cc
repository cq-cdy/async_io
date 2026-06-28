#include <talon/async_io.hpp>
#include <atomic>
#include <cstring>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("TCP server start and stop") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpServer server(io);
    CHECK(server.ListenFd() == -1);
    CHECK(server.Start(19999));
    CHECK(server.ListenFd() >= 0);

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("TCP server start on invalid port fails gracefully") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpServer server(io);
    // Port 0 is technically valid (assigns random), but we test that
    // Start returns a bool and last_error() is meaningful.
    bool started = server.Start(8080);
    if (started) {
        CHECK(server.ListenFd() >= 0);
    } else {
        CHECK_FALSE(server.LastError().empty());
    }

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("TCP client connect to refused port") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpClient client(io);

    std::atomic<bool> done{false};
    int ret_code = 0;

    auto* task = client.Connect("127.0.0.1", 19998);
    if (task != nullptr) {
        io.AddTask(task);
        io.Flush();
    }

    // Give time for connection attempt to fail.
    for (int i = 0; i < 500 && !done.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("TCP client connect invalid IP") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpClient client(io);
    auto* task = client.Connect("invalid.ip.address", 8080);
    CHECK(task == nullptr);
    CHECK_FALSE(client.LastError().empty());

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("TCP client connect with handler args") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpClient client(io);

    std::atomic<bool> called{false};
    auto handler = [&](KernelBuf* buf) {
        called.store(true, std::memory_order_release);
        int r = buf->BytesTransferred();
        (void)r;
    };

    auto* task = client.Connect("127.0.0.1", 19997, handler);
    if (task != nullptr) {
        io.AddTask(task);
        io.Flush();
    }

    for (int i = 0; i < 500 && !called.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    io.RequestShutdown();
    io.Join();
}

TEST_CASE("TCP server accept task creation") {
    IOHandler io;
    REQUIRE(io.Initialized());

    TcpServer server(io);
    REQUIRE(server.Start(19996));

    std::atomic<bool> accepted{false};
    auto accept_handler = [&](KernelBuf* buf) {
        int client_fd = buf->BytesTransferred();
        if (client_fd >= 0) {
            accepted.store(true, std::memory_order_release);
            close(client_fd);
        }
    };

    auto* at = server.CreateAcceptTask(accept_handler);
    CHECK(at != nullptr);
    CHECK(at->Type() == TaskType::kAccept);
    CHECK(at->RepeatForever());
    io.AddTask(at);
    io.Flush();

    // Try to connect (just one attempt).
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd >= 0) {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(19996);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connect(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        // Give event loop time to process.
        for (int i = 0; i < 200 && !accepted.load(std::memory_order_acquire); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        close(sockfd);
    }

    io.RequestShutdown();
    io.Join();
}
