#ifndef TALON_ASYNC_IO_HPP
#define TALON_ASYNC_IO_HPP
#include <arpa/inet.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#define __DEBUG
#ifdef __DEBUG
#define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) (void)0
#endif

namespace talon {
const char* const _VERSION = "v1.0.0";
namespace task {
struct IOResult;
}
}  // namespace talon

namespace talon {
class IOHandler;

}
namespace talon {
namespace task {
template <typename Ret, typename... UserArgs>
class AsyncTask;
struct IOResult {
    template <typename Ret, typename... UserArgs>
    friend class talon::task::AsyncTask;
    friend class talon::IOHandler;
    IOResult(int io_ret, int event_fd, bool iodone, std::string err_msg = "")
        : io_ret_(io_ret),
          event_fd_(event_fd),
          iodone_(iodone),
          err_msg_(std::move(err_msg)) {}
    inline int io_ret() const noexcept { return this->io_ret_; }
    inline int event_fd() const noexcept { return this->event_fd_; }
    inline bool iodone() const noexcept { return this->iodone_; }
    inline const std::string err_msg() const noexcept { return this->err_msg_; }

   private:
    int io_ret_{};
    int event_fd_{};
    bool iodone_{false};
    std::string err_msg_;
};
enum class TaskType {
    NONE = 0,
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

enum class TaskState { READY = 1000, SUBMITED, SUCCESS, FAILED, TIMEOUT };

struct KernelBuf {
    friend class talon::IOHandler;
    KernelBuf(size_t size) : size_(size) { buf_.resize(size); }

    KernelBuf(const KernelBuf&) = delete;
    KernelBuf& operator=(const KernelBuf&) = delete;
    KernelBuf(KernelBuf&& other) noexcept {
        this->buf_ = std::move(other.buf_);
        this->size_ = other.size_;
        other.size_ = 0;
    }
    KernelBuf& operator=(KernelBuf&& other) noexcept {
        if (this != &other) {
            this->buf_ = std::move(other.buf_);
            this->size_ = other.size_;
            other.size_ = 0;
            this->offset_ = other.offset_;
        }
        return *this;
    }
    ~KernelBuf() { this->buf_.clear(); }

    inline void set_fd_offset(off64_t oft) noexcept { this->offset_ = oft; }
    inline off64_t get_fd_offset() const noexcept { return this->offset_; }
    inline int io_ret_v() const noexcept { return this->io_ret_.value_; }
    inline int io_ret_fd() const noexcept { return this->io_ret_.ret_fd_; }
    std::vector<char> buf_{};

    size_t size_{0};
    off64_t offset_{0};

   private:
    struct IORet {
        int value_{};
        int ret_fd_{};  // which fd be actived
    };
    void set_io_ret_v_(int ret) noexcept { this->io_ret_.value_ = ret; }
    void set_io_ret_fd_(int fd) noexcept { this->io_ret_.ret_fd_ = fd; }
    IORet io_ret_{0, 0};
};

using KernelBufPtr = std::shared_ptr<KernelBuf>;

struct Task {
    virtual ~Task() = default;
};
static std::unordered_map<Task*, int> tasks_try_count{};
struct DefaultHandler;

template <typename Ret = void, typename... UserArgs>
class AsyncTask : public Task {
   private:
    using FunctionType = std::function<Ret(KernelBufPtr, UserArgs...)>;
    using CheckBuffer_Fun_ = std::function<bool(const KernelBufPtr&)>;
    friend class IOHandler;

    template <typename Func, typename... Args>
    friend auto createTaskWithHandler(int fd, Func&&, Args... args);

   private:
    explicit AsyncTask(int fd, FunctionType&& func)
        : handler_(std::move(func)) {
        this->fd_ = fd;
        this->buffer_sptr_ = std::make_shared<KernelBuf>(2048);
        this->is_done_ = new std::promise<bool>;
    }
    friend class talon::IOHandler;
    Ret excute_after_op() {
        if constexpr (std::is_same_v<Ret, void>) {
            std::apply(this->handler_, std::make_tuple(this->buffer_sptr_));
        } else {
            return std::apply(this->handler_,
                              std::make_tuple(this->buffer_sptr_));
        }
    }
    inline void __set_task_state(TaskState task_state) noexcept {
        this->task_state_.store(task_state);
    }
    inline void __set_detach_and_done(bool detach) noexcept {
        if (detach) {
            this->set_repeat_forever(false);
            this->is_done_->set_value(true);
            if (tasks_try_count.find(this) != tasks_try_count.end()) {
                tasks_try_count.erase(this);
            }
        }
        this->detach_ = detach;
    }

   public:
    AsyncTask(const AsyncTask& other) = delete;

    AsyncTask& operator=(const AsyncTask& other) {
        if (this != &other) {
            this->fd_ = other.fd_;
            this->buffer_sptr_ = other.buffer_sptr_;
            this->handler_ = other.handler_;
            this->check_buffer_ = other.check_buffer_;
            this->done_ = other.done_;
            this->task_type_ = other.task_type_;
            this->args_tuple_ = other.args_tuple_;
            this->next_ = other->next_;
            this->task_state_ = other->task_state_;
            this->task_type_ = other->task_type_;
            this->is_done_ = other.is_done_;
        }
        return *this;
    }
    virtual ~AsyncTask() {
        /*
         析构的时候 不保证线程安全，用户自行操作
        */
        DEBUG_PRINT("in task ~ fd = %d\n", this->fd_);

        if (tasks_try_count.find(this) != tasks_try_count.end()) {
            tasks_try_count.erase(this);
        }
        delete this->is_done_;
        // todo wait iodone success to delete next
        if (this->next_) {
            delete this->next_;
            this->next_ = nullptr;
        }
    }
    inline task::TaskState getState() const noexcept {
        return this->task_state_.load();
    }

    inline task::TaskType getType() const noexcept { return this->task_type_; }

    inline void set_task_type(TaskType task_type) noexcept {
        this->task_type_ = task_type;
    }

    inline void set_buffer_soft(task::KernelBufPtr buffer) noexcept {
        this->buffer_sptr_ = std::move(buffer);
    }
    IOResult io_done(uint timeout = -1) noexcept {
        bool is_detached = this->detach_.load();

        IOResult ret(-1, this->buffer_sptr_->io_ret_fd(), is_detached,
                     "io done");

        // 如果没有分离任务，返回错误信息
        if (!is_detached) {
            ret.err_msg_ = "task not detached from execute queue now.";
            return ret;
        }

        // 获取 future，并检查是否有效
        auto future = this->is_done_->get_future();

        // 如果 future 无效，表示已经获取过结果，直接返回
        if (!future.valid() || future.wait_for(std::chrono::milliseconds(0)) ==
                                   std::future_status::ready) {
            ret.io_ret_ = this->buffer_sptr_->io_ret_v();
            ret.err_msg_ = "IO Result already retrieved";
            return ret;
        }

        // 如果 timeout 大于0，等待结果或超时
        if (timeout > 0) {
            auto status = future.wait_for(std::chrono::milliseconds(timeout));
            if (status == std::future_status::ready) {
                ret.io_ret_ = this->buffer_sptr_->io_ret_v();
            } else {
                ret.err_msg_ = "get result timeout";
            }
            return ret;
        }

        // 如果没有超时，直接返回
        ret.io_ret_ = this->buffer_sptr_->io_ret_v();
        return ret;
    }

    inline void set_buffer_hardly(const KernelBufPtr& src_buffer) noexcept {
        if (src_buffer) {
            this->buffer_sptr_ = std::make_shared<KernelBuf>(src_buffer->size_);
            this->buffer_sptr_->buf_ = src_buffer->buf_;
        }
    }

    inline void set_check_buffer(CheckBuffer_Fun_ check_buffer) noexcept {
        this->check_buffer_ = check_buffer;
    }
    inline KernelBufPtr get_buffer() noexcept { return this->buffer_sptr_; }

    inline void set_repeat_forever(bool repeat_forever) noexcept {
        if (repeat_forever) {
            this->detach_ = false;
            this->set_timeout(0);
        }
        this->repeat_forever_ = repeat_forever;
    }

    inline void set_repeat_when_failed(bool repeat_when_failed) noexcept {
        this->repeat_when_failed_ = repeat_when_failed;
    }

    inline bool repeat_when_failed() const noexcept {
        return bool(this->max_retry_count_);
    }

    inline bool repeat_forever() const noexcept {
        return this->repeat_forever_;
    }

    inline void reset_buffer() noexcept { this->buffer_sptr_->buf_.resize(0); }

    inline int get_fd() const noexcept { return this->fd_; }

    template <typename Ret_, typename... UserArgs_>
    inline void set_next_task(AsyncTask<Ret_, UserArgs_...>* next) noexcept {
        this->next_ = next;
    }

    inline void set_debug_str(std::string debug_str) noexcept {
        this->debug_str_ = std::move(debug_str);
    }

    inline const std::string get_debug_str() noexcept {
        return this->debug_str_;
    }

    inline void set_timeout(int timeout_ms) noexcept {
        this->timeout_ms_ = timeout_ms;
    }
    inline void set_max_retry_count(int max_retry_count) noexcept {
        this->max_retry_count_ = max_retry_count;
    }

   private:
    std::promise<bool>* is_done_{nullptr};
    std::atomic<bool> detach_ = {false};
    bool repeat_forever_{false};
    int max_retry_count_{-1};
    int fd_{-1};
    int timeout_ms_{0};
    AsyncTask<Ret, UserArgs...>* next_{nullptr};
    KernelBufPtr buffer_sptr_{};

    FunctionType handler_{};
    CheckBuffer_Fun_ check_buffer_ = [](const KernelBufPtr&) noexcept {
        return true;
    };
    std::tuple<UserArgs...> args_tuple_{};
    TaskType task_type_{TaskType::NONE};
    std::atomic<TaskState> task_state_{TaskState::READY};
    std::string debug_str_{};
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
auto unpack_and_instantiate_handler_wrapper(int fd, Func&& func,
                                            Tuple&& args_tuple) {
    return unpack_and_instantiate_handler_wrapper_impl<Ret, Tuple>(
        fd, std::forward<Func>(func), std::forward<Tuple>(args_tuple),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

struct DefaultHandler {
    void operator()(KernelBufPtr) {}
};

template <typename Func = DefaultHandler, typename... Args>
auto createTaskWithHandler(int fd, Func&& func = Func{}, Args... args) {
    using FuncTraits = FunctionTraits<std::decay_t<Func>>;
    using FuncArgsTuple = typename FuncTraits::ArgsTuple;

    static_assert(std::tuple_size_v<FuncArgsTuple> == sizeof...(Args) + 1,
                  "Handler argument count mismatch. Remember to include "
                  "KernelBufPtr as first parameter!");

    using FirstArgType = std::tuple_element_t<0, FuncArgsTuple>;
    static_assert(std::is_same_v<FirstArgType, KernelBufPtr>,
                  "First argument must be KernelBufPtr");

    using ReturnType = typename FuncTraits::ReturnType;
    auto bound_func =
        std::bind(std::forward<Func>(func), std::placeholders::_1, args...);
    return new AsyncTask<ReturnType>(fd, bound_func);
}
};  // namespace task
namespace tcp {};
namespace file {};

class IOHandler {
   public:
    IOHandler(int max_entries = 32) : max_entries_(max_entries) {
        using namespace task;
        this->max_entries_ = max_entries;
        this->uring_ptr_ = new io_uring;
        if (io_uring_queue_init(this->max_entries_, this->uring_ptr_, 0) < 0) {
            perror("io_uring_queue_init failed");
            exit(1);
        }
        __ioloop();
    }

    virtual ~IOHandler() {
        io_uring_queue_exit(this->uring_ptr_);
        delete this->uring_ptr_;
    }

    void init_tcp_server(int port = 8080) {
        this->port_ = port;
        __init_server();
    }

    inline int listen_fd() const noexcept { return this->listen_fd_; }

    io_uring* get_iouring() const noexcept { return this->uring_ptr_; }

    template <typename Ret_, typename... UserArgs_>
    task::AsyncTask<Ret_, UserArgs_...>* createConnectTask(
        const std::string& ip, int port) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket creation failed");
            return nullptr;
        }

        struct sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

        auto handler = [](task::KernelBufPtr buffer) {
            int result = buffer->io_ret_v();
            int fd = buffer->io_ret_fd();
            if (result < 0) {
                DEBUG_PRINT("Connect failed on fd %d: %s\n", fd,
                            strerror(-result));
            } else {
                DEBUG_PRINT("Connected successfully on fd %d\n", fd);
            }
        };

        auto task = task::createTaskWithHandler(sockfd, handler);
        task->set_task_type(task::TaskType::CONNECT);
        // 将地址信息存入buffer
        task->buffer_sptr_->buf_.resize(sizeof(server_addr));
        memcpy(task->buffer_sptr_->buf_.data(), &server_addr,
               sizeof(server_addr));
        return task;
    }
    template <typename Ret_, typename... UserArgs_>
    bool __try_submit(task::AsyncTask<Ret_, UserArgs_...>* task) {
        task::TaskState expected =
            task->task_state_.load(std::memory_order_acquire);
        if (expected == task::TaskState::SUBMITED) {
            return false;
        }
        return task->task_state_.compare_exchange_strong(
            expected, task::TaskState::SUBMITED, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    template <typename Ret_, typename... UserArgs_>
    bool addTask(task::AsyncTask<Ret_, UserArgs_...>* task) {
        [[unlikely]] if (task == nullptr) {
            task->set_debug_str("faild: task ptr is null.");
            return false;
        }
        task->__set_detach_and_done(false);
        if (!__try_submit(task)) {
            task->set_debug_str("faild: task is excuting.");
            task->detach_ = false;  // dont use __set_detach;
            return false;
        }

        DEBUG_PRINT("add TASK ok,fd = %d\n", task->fd_);
        if (task->repeat_when_failed()) {
            if (task::tasks_try_count.find(task) ==
                task::tasks_try_count.end()) {
                task::tasks_try_count[task] = 0;
            }
        }

        if (task->is_done_ != nullptr) {
            delete task->is_done_;
        }
        task->is_done_ = new std::promise<bool>;
        this->__submit_task(task);
        return true;
    }

   private:
    void __ioloop() {
        std::thread([this]() {
            /*
                Try to only deal with general logic here.
                For example, task topology execution, retry, timeout, etc.
                Separated from specific reading and writing IO, protocol
                analysis and other logic
             */
            while (true) {
                io_uring_cqe* cqe;
                io_uring_wait_cqe(uring_ptr_, &cqe);

                io_uring_cqe_seen(uring_ptr_, cqe);

                enum EventFlags : uintptr_t {
                    TIMEOUT_FLAG = 1ULL << 63,
                    ERROR_FLAG = 1ULL << 62,
                    MASK_FLAGS = TIMEOUT_FLAG | ERROR_FLAG
                };

                const auto user_data = cqe->user_data;
                const auto event_type =
                    static_cast<EventFlags>(user_data & MASK_FLAGS);
                auto* task_p = reinterpret_cast<task::AsyncTask<>*>(
                    user_data & ~MASK_FLAGS);

                if (!task_p) {
                    continue;
                }
                if (task_p->getType() == task::TaskType::WRITE) {
                    DEBUG_PRINT("write fd = %d\n", task_p->fd_);
                }
                switch (event_type) {
                    case ERROR_FLAG:
                        __handle_error_event(task_p, cqe->res);
                        break;

                    case TIMEOUT_FLAG:
                        __handle_timeout_event(task_p);
                        break;

                    default:
                        __handle_normal_event(task_p, cqe->res);
                        break;
                }
            }
        }).detach();
    }

    template <typename Ret_, typename... UserArgs_>
    task::AsyncTask<Ret_, UserArgs_...>* __createAccpetTask() {
        auto accpet_task = task::createTaskWithHandler(this->listen_fd_);
        accpet_task->set_task_type(task::TaskType::ACCEPT);
        accpet_task->set_repeat_forever(true);
        return accpet_task;
    }
    template <typename Ret_, typename... UserArgs_>
    void __handle_error_event(task::AsyncTask<Ret_, UserArgs_...>* task_p,
                              int res) {
        task_p->buffer_sptr_->set_io_ret_v_(res);
        task_p->buffer_sptr_->set_io_ret_fd_(task_p->fd_);
        __handle_failed_task(task_p);
    }

    template <typename Ret_, typename... UserArgs_>
    inline void __handle_timeout_event(
        task::AsyncTask<Ret_, UserArgs_...>* task_p) {
        __handle_timeout_task(task_p);
    }
    template <typename Ret_, typename... UserArgs_>
    void __handle_normal_event(task::AsyncTask<Ret_, UserArgs_...>* task_p,
                               int res) {
        DEBUG_PRINT("__handle_normal_event fd = %d, res = %d\n", task_p->fd_,
                    res);

        task_p->buffer_sptr_->set_io_ret_v_(res);
        task_p->buffer_sptr_->set_io_ret_fd_(task_p->fd_);

        if (res < 0) {
            task_p->__set_task_state(task::TaskState::FAILED);
            if (res == -EAGAIN || res == -EWOULDBLOCK) {
                // 无数据/暂时不可用，重新提交任务
                DEBUG_PRINT("Temporary error, resubmitting task on fd %d\n",
                            task_p->fd_);
                addTask(task_p);  // 手动重复
            } else {
                // 永久性错误处理
                __handle_io_failure(task_p);
            }
        } else {
            // 处理连接关闭或正常完成
            if (res == 0) {
                DEBUG_PRINT(
                    "Zero-length response, closing connection on fd %d\n",
                    task_p->fd_);
                __handle_connection_close(task_p);
            } else {
                task_p->excute_after_op();
                if (task_p->repeat_forever()) {
                    task_p->__set_task_state(task::TaskState::SUCCESS);
                    addTask(task_p);
                }
                // 处理正常短连接
                task_p->__set_task_state(task::TaskState::SUCCESS);
                task_p->detach_ =
                    true;  // dont use __set_detach : then dont set is_done_;

                // 处理任务链
                if (task_p->next_) {
                    addTask(task_p->next_);
                }
            }
        }
    }
    template <typename Ret_, typename... UserArgs_>
    void __handle_io_failure(task::AsyncTask<Ret_, UserArgs_...>* task_p) {
        if (!__handle_failed_task(task_p)) {
            DEBUG_PRINT("Final failure on fd %d\n", task_p->fd_);
        }
    }
    template <typename Ret_, typename... UserArgs_>
    void __handle_connection_close(
        task::AsyncTask<Ret_, UserArgs_...>* task_p) {
        if (task_p->fd_ > 0 && task_p->fd_ != this->listen_fd()) {
            DEBUG_PRINT("Closing fd %d\n", task_p->fd_);
            close(task_p->fd_);
            task_p->fd_ = -1;
        }
        task_p->__set_detach_and_done(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        task_p->__set_task_state(task::TaskState::SUCCESS);
    }
    template <typename Ret_, typename... UserArgs_>
    bool __handle_timeout_task(task::AsyncTask<Ret_, UserArgs_...>* task_p) {
        task_p->set_debug_str("Task Time Out");
        DEBUG_PRINT("Task timed out and retries exhausted\n");

        task_p->__set_detach_and_done(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        task_p->__set_task_state(task::TaskState::TIMEOUT);
        return false;
    }
    template <typename Ret_, typename... UserArgs_>
    bool __handle_failed_task(task::AsyncTask<Ret_, UserArgs_...>* task_p) {
        if (task_p->repeat_when_failed()) {
            task::tasks_try_count[task_p]++;
            if (task::tasks_try_count[task_p] < task_p->max_retry_count_) {
                task_p->__set_task_state(task::TaskState::READY);
                this->addTask(task_p);
                return true;
            }
        }
        task_p->set_debug_str("Task excute faild after retry");
        task::tasks_try_count[task_p] == 0;
        task_p->__set_detach_and_done(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        task_p->__set_task_state(task::TaskState::FAILED);
        return false;
    }

   private:
    void __init_server() {
        this->listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
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
        // __accpet_util();
    }

   private:
    template <typename Ret_, typename... UserArgs_>
    bool __validate_connect_task(task::AsyncTask<Ret_, UserArgs_...>* task) {
        if (task->fd_ <= 0) {
            fprintf(stderr, "[CONNECT VALIDATION] Invalid socket fd: %d\n",
                    task->fd_);
            return false;
        }

        if (task->buffer_sptr_->buf_.size() < sizeof(sockaddr_in)) {
            fprintf(stderr,
                    "[CONNECT VALIDATION] Buffer too small: %zu < %zu\n",
                    task->buffer_sptr_->buf_.size(), sizeof(sockaddr_in));
            return false;
        }

        auto* addr =
            reinterpret_cast<sockaddr_in*>(task->buffer_sptr_->buf_.data());

        if (addr->sin_family != AF_INET) {
            fprintf(stderr, "[CONNECT VALIDATION] Invalid address family: %d\n",
                    addr->sin_family);
            return false;
        }

        if (ntohs(addr->sin_port) <= 0 || ntohs(addr->sin_port) > 65535) {
            fprintf(stderr, "[CONNECT VALIDATION] Invalid port: %d\n",
                    ntohs(addr->sin_port));
            return false;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
        if (strcmp(ip_str, "0.0.0.0") == 0) {
            fprintf(stderr,
                    "[CONNECT VALIDATION] Invalid IP address: 0.0.0.0\n");
            return false;
        }

        return true;
    }
    template <typename Ret_, typename... UserArgs_>
    void __queue_failed_task(task::AsyncTask<Ret_, UserArgs_...>* task) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(uring_ptr_);
        if (sqe) {
            struct __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 0};
            io_uring_prep_timeout(sqe, &ts, 0, 0);

            constexpr uintptr_t ERROR_FLAG = 1ULL << 62;
            sqe->user_data = reinterpret_cast<uintptr_t>(task) | ERROR_FLAG;

            io_uring_submit(uring_ptr_);
        } else {
            __handle_failed_task(task);
        }
    }

    template <typename Ret_, typename... UserArgs_>
    void __handle_connect(task::AsyncTask<Ret_, UserArgs_...>* task,
                          struct io_uring_sqe* sqe) {
        if (!__validate_connect_task(task)) {
            task->buffer_sptr_->set_io_ret_v_(-EINVAL);
            task->buffer_sptr_->set_io_ret_fd_(task->fd_);
            __queue_failed_task(task);

        } else {
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(
                task->buffer_sptr_->buf_.data());
            io_uring_prep_connect(sqe, task->fd_, (struct sockaddr*)addr,
                                  sizeof(struct sockaddr_in));
        }
    }

    // todo expand to other task
    template <typename Ret_, typename... UserArgs_>
    void __submit_task(task::AsyncTask<Ret_, UserArgs_...>* task) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(this->get_iouring());
        switch (task->task_type_) {
            case task::TaskType::READ:
                io_uring_prep_read(sqe, task->fd_,
                                   task->buffer_sptr_->buf_.data(),
                                   task->buffer_sptr_->buf_.size(),
                                   task->buffer_sptr_->get_fd_offset());
                break;
            case task::TaskType::WRITE:
                DEBUG_PRINT("write task\n");
                io_uring_prep_write(sqe, task->fd_,
                                    task->buffer_sptr_->buf_.data(),
                                    task->buffer_sptr_->buf_.size(),
                                    task->buffer_sptr_->get_fd_offset());
                break;
            case task::TaskType::ACCEPT:
                io_uring_prep_accept(sqe, task->fd_, NULL, NULL, SOCK_NONBLOCK);
                break;
            case task::TaskType::CONNECT: {
                __handle_connect(task, sqe);
                break;
            }
            default:
                DEBUG_PRINT("task type not supported\n");
                break;
        }
        if (task->timeout_ms_ > 0) {
            struct io_uring_sqe* timeout_sqe = io_uring_get_sqe(uring_ptr_);
            if (timeout_sqe) {
                struct __kernel_timespec ts;
                ts.tv_sec = task->timeout_ms_ / 1000;
                ts.tv_nsec = (task->timeout_ms_ % 1000) * 1000000LL;
                io_uring_prep_timeout(timeout_sqe, &ts, 0, 0);
                constexpr uintptr_t TIMEOUT_FLAG = 1ULL << 63;
                timeout_sqe->user_data =
                    reinterpret_cast<uintptr_t>(task) | TIMEOUT_FLAG;
            }
        }
        sqe->user_data = reinterpret_cast<decltype(sqe->user_data)>(task);
        sqe->fd = task->fd_;
        io_uring_submit(this->get_iouring());
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
