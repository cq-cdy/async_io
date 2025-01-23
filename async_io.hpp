#ifndef TALON_ASYNC_IO_HPP
#define TALON_ASYNC_IO_HPP
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <vector>
namespace talon {
namespace tcp {};
namespace file {};
namespace task {

enum class TaskType {
    READ,
    WRITE,
    ACCEPT,
    CONNECT,
    CLOSE,
    TIMEOUT,
    TIMEOUT_REMOVE,
    TIMEOUT_UPDATE,
    FALLOCATE,
    OPENAT,
    OPENAT_DIRECT,
    STATX,
    FADVISE,
    ACCEPT_DIRECT,
    CANCEL,
    LINK_TIMEOUT
};
using KernelBuf = std::vector<char>;
using KernelBufPtr = std::shared_ptr<KernelBuf>;

struct Task {};

template <typename Ret = void, typename... UserArgs>
class AsyncTask : public Task {
   private:
    using FunctionType = std::function<Ret(KernelBufPtr, UserArgs...)>;
    using CheckBuffer_Fun_ = std::function<bool(KernelBufPtr&)>;
    friend class AsyncTcpServer;

    template <typename R, typename Tuple, typename Func, std::size_t... I>
    friend auto unpack_and_instantiate_handler_wrapper_impl(
        int fd, Func&& func, Tuple&& args_tuple, std::index_sequence<I...>);

   private:
    explicit AsyncTask(int fd, FunctionType&& func, std::tuple<UserArgs...>&& args)
        : handler_(std::move(func)), args_tuple_(std::move(args)) {
        this->fd_ = fd;
        this->buffer_sptr_ = std::make_shared<KernelBuf>(1024);
    }

   public:
    AsyncTask(const AsyncTask& other) = default;
    AsyncTask& operator=(const AsyncTask& other) {
        if (this != &other) {
            this->fd_ = other.fd_;
            this->buffer_sptr_ = other.buffer_sptr_;
            this->handler_ = other.handler_;
            this->check_buffer_ = other.check_buffer_;
            this->done_ = other.done_;
            this->task_type_ = other.task_type_;
            this->args_tuple_ = other.args_tuple_;
        }
        return *this;
    }

    KernelBufPtr& operator()() noexcept { return this->buffer_sptr_; }

    Ret excute_after_op() noexcept {
        auto new_args = std::tuple_cat(std::make_tuple(this->buffer_sptr_), args_tuple_);
        auto buf = this->buffer_sptr_;

        if constexpr (std::is_same_v<Ret, void>) {
            std::apply(handler_, new_args);
        } else {
            return std::apply(handler_, new_args);
        }
    }

    inline void set_task_type(TaskType task_type) noexcept { this->task_type_ = task_type; }
    inline void set_buffer_hardly(KernelBufPtr buffer) noexcept { this->buffer_sptr_ = std::move(buffer); }
    inline void set_check_buffer(CheckBuffer_Fun_ check_buffer) noexcept { this->check_buffer_ = check_buffer; }
    inline KernelBufPtr get_buffer() noexcept { return this->buffer_sptr_; }
    void reset_buffer() noexcept { this->buffer_sptr_->resize(0); }
    inline bool iodone() const noexcept { return this->done_; }
    inline void set_done(bool done) noexcept { this->done_ = done; }
    int get_fd() const noexcept { return this->fd_; }
    void set_next_task(AsyncTask* next) { this->next_ = next; }
    void set_prev_task(AsyncTask* prev) { this->prev_ = prev; }

   private:
    bool done_{true};
    int fd_{-1};
    AsyncTask* prev_{nullptr};
    AsyncTask* next_{nullptr};
    KernelBufPtr buffer_sptr_{};
    FunctionType handler_{};
    CheckBuffer_Fun_ check_buffer_ = [](KernelBufPtr&) noexcept { return true; };
    std::tuple<UserArgs...> args_tuple_{};
    TaskType task_type_;
};

// FunctionTraits to extract function signature
template <typename T>
struct FunctionTraits;

template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

template <typename Func>
struct FunctionTraits : FunctionTraits<decltype(&Func::operator())> {};

template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...) const> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

template <typename Tuple>
struct split_args;

template <typename First, typename... Rest>
struct split_args<std::tuple<First, Rest...>> {
    using rest_type = std::tuple<Rest...>;
};

template <typename Ret, typename Tuple, typename Func, std::size_t... I>
auto unpack_and_instantiate_handler_wrapper_impl(int fd, Func&& func,
                                                 Tuple&& args_tuple,
                                                 std::index_sequence<I...>) {
    return new AsyncTask<Ret, std::tuple_element_t<I, Tuple>...>(
        fd, std::forward<Func>(func), std::forward<Tuple>(args_tuple));
}

template <typename Ret, typename Tuple, typename Func>
auto unpack_and_instantiate_handler_wrapper(int fd, Func&& func, Tuple&& args_tuple) {
    return unpack_and_instantiate_handler_wrapper_impl<Ret, Tuple>(
        fd, std::forward<Func>(func), std::forward<Tuple>(args_tuple),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Func, typename... Args>
auto createTaskWithHandler(int fd, Func&& func, Args&&... args) {
    using FuncTraits = FunctionTraits<std::decay_t<Func>>;
    using FuncArgsTuple = typename FuncTraits::ArgsTuple;
    static_assert(std::tuple_size_v<FuncArgsTuple> >= 1, "Function must have at least one argument");
    using FirstArgType = std::tuple_element_t<0, FuncArgsTuple>;
    static_assert(std::is_same_v<FirstArgType, KernelBufPtr>, "First argument must be KernelBufPtr");

    using UserArgsTuple = typename split_args<FuncArgsTuple>::rest_type;
    static_assert(sizeof...(Args) == std::tuple_size_v<UserArgsTuple>,
                  "Number of arguments provided does not match required count");

    return unpack_and_instantiate_handler_wrapper<typename FuncTraits::ReturnType, UserArgsTuple>(
        fd, std::forward<Func>(func), std::make_tuple(std::forward<Args>(args)...));
}
};  // namespace task

class AsyncTcpServer {
   public:
    AsyncTcpServer(int max_entries = 32, int port = 8080)
        : max_entries_(max_entries) {
        this->port_ = port;
        using namespace task;
        this->max_entries_ = max_entries;
        this->uring_ptr_ = new io_uring;
        if (io_uring_queue_init(this->max_entries_, this->uring_ptr_, 0) < 0) {
            perror("io_uring_queue_init failed");
            exit(1);
        }
        this->__init_server();
    }
    void addTask(task::Task* task) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(this->uring_ptr_);
        if (!sqe) {
            fprintf(stderr, "Failed to get SQE\n");
            return;
        }
        sqe->user_data = reinterpret_cast<uint64_t>(task);

        io_uring_submit(this->uring_ptr_);
    }
    virtual ~AsyncTcpServer() {
        io_uring_queue_exit(this->uring_ptr_);
        delete this->uring_ptr_;
    }

   private:
    void __init_server() {
        this->listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (this->listen_fd_ < 0) {
            perror("socket creation failed");
            exit(1);
        }

        struct sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(this->port_);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        int opt = 1;
        setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt,
                   sizeof(opt));

        if (bind(this->listen_fd_, (struct sockaddr*)&server_addr,
                 sizeof(server_addr)) < 0) {
            perror("bind failed");
            exit(1);
        }

        if (listen(this->listen_fd_, 10) < 0) {
            perror("listen failed");
            exit(1);
        }
    }

   private:
    io_uring* uring_ptr_{};
    int fd_{-1};
    int max_entries_{32};
    int listen_fd_{-1};
    int port_{8080};
};
};  // namespace talon

#endif
