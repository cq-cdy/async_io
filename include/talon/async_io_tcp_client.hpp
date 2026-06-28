#ifndef TALON_ASYNC_IO_TCP_CLIENT_HPP_
#define TALON_ASYNC_IO_TCP_CLIENT_HPP_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "async_io_constants.hpp"
#include "async_io_task.hpp"

namespace talon {
inline namespace v2_2_0 {

class IOHandler;

class TcpClient {
public:
    explicit TcpClient(IOHandler& io, const AsyncIoConfig& config = {})
        : io_(io), config_(config) {}

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // Creates a connect task for the given IP and port.  Returns nullptr on
    // socket creation failure (check with last_error()).
    //
    // The returned task has type kConnect and the sockaddr_in stored in its
    // buffer.  Call io.AddTask(task) to submit it.
    template <typename... Args>
    [[nodiscard]] task::AsyncTask<void>* Connect(const std::string& ip, int port,
                                   Args&&... handler_args) {
        int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) {
            last_error_ = std::string("socket() failed: ") + strerror(errno);
            return nullptr;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            last_error_ = "inet_pton() failed for " + ip;
            close(sockfd);
            return nullptr;
        }

        auto handler = [](task::KernelBuf* buf) {
            int r = buf->bytes_transferred();
            int fd = buf->active_file_descriptor();
            if (r < 0) DebugLog("Connect failed fd=%d: %s\n", fd, strerror(-r));
            else       DebugLog("Connected fd=%d\n", fd);
        };

        auto* task = task::CreateTaskWithHandler(sockfd, handler,
                                                  std::forward<Args>(handler_args)...);
        task->set_task_type(task::TaskType::kConnect);
        task->set_timeout(config_.connect_timeout_ms);
        task->buffer()->resize(sizeof(addr));
        std::memcpy(task->buffer()->data(), &addr, sizeof(addr));
        return task;
    }

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    IOHandler& io_;
    AsyncIoConfig config_;
    std::string last_error_;
};

}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_TCP_CLIENT_HPP_
