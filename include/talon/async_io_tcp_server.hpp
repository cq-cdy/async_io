#ifndef TALON_ASYNC_IO_TCP_SERVER_HPP_
#define TALON_ASYNC_IO_TCP_SERVER_HPP_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>

#include "async_io_constants.hpp"
#include "async_io_task.hpp"

namespace talon {
inline namespace v2_2_0 {

class IOHandler;

class TcpServer {
public:
    // Constructs a TcpServer bound to the given IOHandler.  The IOHandler must
    // outlive the TcpServer.  All server I/O goes through io's event loop.
    explicit TcpServer(IOHandler& io, const AsyncIoConfig& config = {})
        : io_(io), config_(config) {}

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    ~TcpServer() {
        if (listen_fd_ >= 0) close(listen_fd_);
    }

    // Starts listening on the given port.  Returns true on success.
    // On failure, last_error() contains the error message.
    [[nodiscard]] bool Start(int port) {
        port_ = port;
        return Init();
    }

    // Returns the listen file descriptor, or -1 if not started.
    [[nodiscard]] int ListenFd() const noexcept { return listen_fd_; }

    // Returns the last error message.
    [[nodiscard]] const std::string& LastError() const noexcept { return last_error_; }

    // Creates a pre-configured accept task for this server.  The task type is
    // set to kAccept and repeat_forever is enabled.  Call io.AddTask() to
    // submit it.
    template <typename... Args>
    [[nodiscard]] task::AsyncTask<void>* CreateAcceptTask(Args&&... handler_args) {
        auto* task = task::CreateTaskWithHandler(
            listen_fd_, std::forward<Args>(handler_args)...);
        task.SetTaskType(task::TaskType::kAccept);
        task.SetRepeatForever(true);
        return task;
    }

private:
    bool Init() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (listen_fd_ < 0) {
            last_error_ = std::string("socket() failed: ") + strerror(errno);
            return false;
        }

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            last_error_ = std::string("bind() failed: ") + strerror(errno);
            close(listen_fd_); listen_fd_ = -1; return false;
        }

        if (listen(listen_fd_, config_.tcp_accept_backlog) < 0) {
            last_error_ = std::string("listen() failed: ") + strerror(errno);
            close(listen_fd_); listen_fd_ = -1; return false;
        }

        return true;
    }

    IOHandler& io_;
    AsyncIoConfig config_;
    int listen_fd_{-1};
    int port_{8080};
    std::string last_error_;
};

}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_TCP_SERVER_HPP_
