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

template <typename Ret, typename... Args>
class AsyncTask {
   private:
    using FunctionType = std::function<Ret(Args...)>;
    using CheckBuffer_Fun_ = std::function<bool(KernelBufPtr&)>;
    friend class AsyncTcpServer;
    // 声明友元函数
    template <typename R, typename Tuple, typename Func, std::size_t... I>
    friend auto unpack_and_instantiate_handler_wrapper_impl(
        int fd, Func&& func, std::index_sequence<I...>);

   private:
    explicit AsyncTask(int fd, FunctionType&& func) : handler_(func) {
        this->fd_ = fd;
        this->buffer_sptr_ = std::make_shared<KernelBuf>(1024);
    }

   public:
    AsyncTask(const AsyncTask& other) = default;
    AsyncTask& operator=(const AsyncTask& other) = default;
    KernelBufPtr& operator()() noexcept { return this->buffer_sptr_; }

    template <typename... CallArgs>
    Ret excute_after_op(CallArgs&&... args) noexcept {
        if constexpr (sizeof...(CallArgs) != sizeof...(Args)) {
            static_assert(sizeof...(CallArgs) == sizeof...(Args),
                          "Number of arguments does not match");
        } else {
            if constexpr (std::is_same_v<Ret, void>) {
                handler_(std::forward<CallArgs>(args)...);
            } else {
                return handler_(std::forward<CallArgs>(args)...);
            }
        }
    }

    inline void set_task_type(TaskType task_type) noexcept {
        this->task_type_ = task_type;
    }

    inline void set_buffer_hardly(const KernelBufPtr buffer) noexcept {
        this->buffer_sptr_ = std::move(buffer);
    }

    inline void set_check_buffer(CheckBuffer_Fun_ check_buffer) noexcept {
        this->check_buffer_ = check_buffer;
    }

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
    CheckBuffer_Fun_ check_buffer_ = [](KernelBufPtr&) noexcept {
        return true;
    };
};

// 1. 用于提取函数类型的类型萃取模板结构
template <typename T>
struct FunctionTraits;  // 先声明

// 特化：处理普通函数类型
template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using ReturnType = R;                            // 返回值类型
    using ArgsTuple = std::tuple<Args...>;           // 参数类型的元组
    static constexpr size_t ArgCount = sizeof...(Args);  // 参数个数
    TaskType task_type{TaskType::READ};
};

// 特化：处理 std::function 类型
template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

// 特化：处理 lambda 表达式
template <typename Func>
struct FunctionTraits : public FunctionTraits<decltype(&Func::operator())> {};

// 特化：处理成员函数指针
template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

// 特化：处理 const 成员函数指针
template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...) const> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t ArgCount = sizeof...(Args);
};

// 辅助函数来解包 ArgsTuple 并传递给 HandlerWrapper
template <typename Ret, typename Tuple, typename Func>
auto unpack_and_instantiate_handler_wrapper(Func&& func);

template <typename Ret, typename Tuple, typename Func, std::size_t... I>
auto unpack_and_instantiate_handler_wrapper_impl(int fd, Func&& func,
                                                 std::index_sequence<I...>) {
    AsyncTask<Ret, std::tuple_element_t<I, Tuple>...> wrapper(
        fd, std::forward<Func>(func));
    return wrapper;
}

template <typename Ret, typename Tuple, typename Func>
auto unpack_and_instantiate_handler_wrapper(int fd, Func&& func) {
    return unpack_and_instantiate_handler_wrapper_impl<Ret, Tuple>(
        fd, std::forward<Func>(func),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

// instantiate class template with function
template <typename KeyType = KernelBufPtr,typename Func=std::function<void(KeyType)> >
auto createTaskWithHandler(int fd, Func&& func=Func{}) {
    using ReturnType = typename FunctionTraits<std::decay_t<Func>>::ReturnType;
    using ArgsTuple = typename FunctionTraits<std::decay_t<Func>>::ArgsTuple;

    using FirstArgType = typename std::tuple_element<0, ArgsTuple>::type;
    // use static_assert to check the first argument type
    static_assert(std::is_same_v<FirstArgType, KeyType>,
                  "First argument must be of type KeyType");

    // unpack ArgsTuple and pass to HandlerWrapper
    return unpack_and_instantiate_handler_wrapper<ReturnType, ArgsTuple>(
        fd, std::forward<Func>(func));
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
