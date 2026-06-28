# Talon Async I/O — High-Performance C++17 Async I/O on io_uring

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)]()
[![Linux](https://img.shields.io/badge/Kernel-%E2%89%A55.11-orange)](https://www.kernel.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![CI](https://img.shields.io/badge/CI-Ubuntu%20%7C%20Alpine%20%7C%20CentOS%20%7C%20Fedora-brightgreen)]()

**Talon Async I/O** (`talon-async-io`) is a header-only C++17 async I/O library built on Linux
[`io_uring`](https://kernel.dk/io_uring.pdf) (kernel >= 5.11).  It uses a handler-driven,
single-threaded event-loop model to deliver high-throughput, low-latency I/O for file
operations, TCP servers, and TCP clients.

---

## 1. Architecture Overview

### Dependency Graph

```
async_io.hpp  (umbrella header)
 ├── async_io_fwd.hpp         Forward declarations
 ├── async_io_constants.hpp   Config, enums, DebugLog
 ├── async_io_result.hpp      IOResult + DoneState (lock-free promise replacement)
 ├── async_io_traits.hpp      Compile-time function signature introspection
 ├── async_io_pool.hpp        ObjectPool<T> + BufferPool (lock-free, TLS-cached)
 ├── async_io_kernelbuf.hpp   KernelBuf — inline SBO → pool → heap tiered buffer
 ├── async_io_task.hpp        InlineFunction (SBO type-erasure) + AsyncTask<T>
 ├── async_io_handler.hpp     IOHandler — event loop, submission, dispatching
 ├── async_io_tcp_server.hpp  TcpServer
 └── async_io_tcp_client.hpp  TcpClient
```

### Concurrency Model

| Component | Strategy |
|-----------|----------|
| **Event Loop** | Single-threaded `io_uring` poll/wait loop in `IOHandler::RunEventLoop()` |
| **Task submission** | Multi-threaded safe; CAS gate prevents double-submit |
| **Memory pools** | Lock-free `TreiberStack` (ABA-safe tagged-pointer on x86\_64/ARMv8.1+); per-thread caches |
| **Cross-thread signalling** | `DoneState` — atomic + mutex + condition-variable, zero heap allocs |

### Memory Allocation Path

| Operation | Mechanism | Heap Allocs |
|-----------|-----------|-------------|
| Task creation | `ObjectPool<AsyncTask<void>>::Acquire()` (lock-free) | 0 (pool hit) |
| Buffer ≤ 256 B | KernelBuf **SBO** (inline `char[256]`) | 0 |
| Buffer 256–65536 B | `BufferPool` (11 power-of-two classes, TLS cache) | 0 (pool hit) |
| Buffer > 65536 B | `std::vector<char>` (heap fallback) | 1 |
| Handler storage | `InlineFunction` with 64-byte SBO | 0 for small captures |
| Completion wait | `DoneState` (atomically waits; reuses storage) | 0 |

### Cache-Line Layout (`AsyncTask<T>`)

```
[ Hot  line  (alignas 64) ]: task_state_ | detach_ | done_state_ | buffer_ | user_data_ | fd_
[ Warm line               ]: task_type_ | handler_ | next_ | is_cancel_ | try_count_
[ Cold lines               ]: timeout_ms_ | max_retry_count_ | repeat_* | args_tuple_ | debug_str_
```

---

## 2. Quick Start

### System Requirements

- **Linux kernel >= 5.11** (for `io_uring`)
- **C++17 compiler**: GCC >= 10 or Clang >= 14
- **liburing-dev** (io_uring userspace library)
- **CMake >= 3.16**

### Build & Install

```bash
# Install liburing
sudo apt-get install liburing-dev

# Build (header-only library — nothing to compile)
git clone https://github.com/taloncdy/async_io.git
cd async_io
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build       # builds tests & examples
sudo cmake --install build
```

### CMake Integration

```cmake
# find_package
find_package(talon-async-io REQUIRED)
target_link_libraries(my_app PRIVATE talon_async_io)

# or FetchContent
include(FetchContent)
FetchContent_Declare(
    talon-async-io
    GIT_REPOSITORY https://github.com/taloncdy/async_io.git
    GIT_TAG v2.2.0
)
FetchContent_MakeAvailable(talon-async-io)
target_link_libraries(my_app PRIVATE talon_async_io)
```

### Minimal Example — File Read

```cpp
#include <fcntl.h>
#include <cstdio>
#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

void ReadHandler(KernelBuf* buf) {
    int bytes = buf->BytesTransferred();
    printf("Read %d bytes: %.*s\n", bytes, bytes, buf->Data());
}

int main() {
    IOHandler io;
    if (!io.Initialized()) return 1;

    int fd = open("/etc/hostname", O_RDONLY);
    auto* task = CreateTaskWithHandler(fd, ReadHandler);
    task->SetTaskType(TaskType::kRead);
    io.AddTask(task);

    task->WaitForCompletion();
    io.RequestShutdown(); io.Join();
    close(fd);
    return 0;
}
```

---

## 3. API Reference

### 3.1 IOHandler — Event Loop

```cpp
IOHandler io;                                  // Default config
IOHandler io(AsyncIoConfig{...});              // Custom config

io.Initialized();                              // true if event loop started
io.AddTask(task);                              // Submit to event loop (thread-safe)
io.Flush();                                    // Force submission
io.RequestShutdown();                          // Async-signal-safe shutdown request
io.Join();                                     // Block until event loop exits

// Backpressure (v2.2.0)
io.InflightCount();                            // Current in-flight operations
io.BackpressureActive();                       // True if inflight limit reached

// Signal handling (v2.2.0)
io.InstallSignalHandlers();                    // Register SIGINT/SIGTERM
IOHandler::OnSignal(signum);                   // Static, async-signal-safe
```

### 3.2 AsyncTask — I/O Task

```cpp
auto* task = CreateTaskWithHandler(fd, handler, user_args...);

// Configuration (call BEFORE AddTask)
task->SetTaskType(TaskType::kRead);            // kRead|kWrite|kAccept|kConnect
task->SetTimeout(5000);                        // Linked timeout (ms), 0 = none
task->SetMaxRetryCount(3);                     // Auto-retry on failure
task->SetRepeatForever(true);                  // Re-submit after each completion
task->SetBuffer(std::move(kernel_buf));        // Attach custom buffer
task->SetNextTask(next_task);                  // Chain a subsequent task

// After submission
task->WaitForCompletion(timeout_ms);           // Block until completion (returns IOResult)
task->Cancel(io.IoUring());                    // Cancel in-flight operation

// Accessors
task->State();          // kReady | kSubmitted | kSuccess | kFailed | ...
task->Type();           // kRead | kWrite | ...
task->Fd();             // File descriptor
task->Buffer();         // KernelBuf (valid during handler invocation)
```

### 3.3 KernelBuf — I/O Buffer

```cpp
auto kb = MakeKernelBuffer(4096);              // Factory (unique_ptr<KernelBuf>)

kb->Data();              // char* to buffer data
kb->size;                // Buffer capacity (public member)
kb->BytesTransferred();  // Result: bytes read/written or negative errno
kb->ActiveFileDescriptor(); // fd that triggered the event
kb->FdOffset();          // File seek offset
kb->SetFdOffset(off);    // Set seek offset
kb->Resize(newsz);       // Resize (preserves data)
```

### 3.4 TcpServer / TcpClient

```cpp
// Server
TcpServer server(io, config);
server.Start(8080);                        // Returns bool
server.ListenFd();                         // Listening socket fd
auto* accept_task = server.CreateAcceptTask(handler);
accept_task->SetRepeatForever(true);       // Keep accepting
io.AddTask(accept_task);

// Client
TcpClient client(io, config);
auto* conn = client.Connect("127.0.0.1", 8080, handler);
io.AddTask(conn);
```

### 3.5 Memory Subsystem (Advanced)

```cpp
// BufferPool (11 size classes, power-of-two)
auto& bp = BufferPool::Instance();
void* buf = bp.Allocate(4096);             // Get pooled buffer
bp.Deallocate(buf, 4096);                  // Return to pool

// ObjectPool<T> (for custom objects)
ObjectPool<MyTask> pool(1024, 256);
auto* obj = pool.Acquire();                // Get from pool
pool.Release(obj);                         // Return to pool

// GetTaskPool<T>() (per-type singleton)
auto& task_pool = GetTaskPool<AsyncTask<void>>();
```

### 3.6 Task Patterns

| Pattern | API | Description |
|---------|-----|-------------|
| **One-shot** | `io.AddTask(task)` | Single execution (default) |
| **Repeat-forever** | `task->SetRepeatForever(true)` | Long-lived (accept/read loops) |
| **Task chaining** | `task1->SetNextTask(task2)` | Sequential execution, any number of links |
| **Retry on failure** | `task->SetMaxRetryCount(3)` | Automatic retry with configurable count |
| **Timeout** | `task->SetTimeout(5000)` | Linked timeout via `IOSQE_IO_LINK` |
| **Cancel** | `task->Cancel(io.IoUring())` | Cancel in-flight I/O |
| **Backpressure** | `config.max_inflight_ops = N` | Limit concurrent operations |
| **User arguments** | `CreateTaskWithHandler(fd, fn, a, b)` | Bind args to handler |

---

## 4. Examples

| Example | Demonstrates |
|---------|-------------|
| [`read_file_example.cc`](examples/read_file_example.cc) | Basic read, WaitForCompletion, graceful shutdown |
| [`read_large_file_example.cc`](examples/read_large_file_example.cc) | Chunked streaming read via handler chaining |
| [`file_write_example.cc`](examples/file_write_example.cc) | Write + read-back verification |
| [`server_tcp_example.cc`](examples/server_tcp_example.cc) | TCP echo server, repeat-forever accept, signal shutdown |
| [`tcp_client_example.cc`](examples/tcp_client_example.cc) | TCP connect → write → read chain |
| [`tcp_wr_example.cc`](examples/tcp_wr_example.cc) | Multi-message TCP client with repeat-forever read |
| [`task_chain_example.cc`](examples/task_chain_example.cc) | Explicit SetNextTask chaining |
| [`timeout_retry_example.cc`](examples/timeout_retry_example.cc) | SetTimeout + SetMaxRetryCount |
| [`backpressure_example.cc`](examples/backpressure_example.cc) | max_inflight_ops limit + retry |
| [`signal_shutdown_example.cc`](examples/signal_shutdown_example.cc) | InstallSignalHandlers + custom sigaction |
| [`custom_handler_example.cc`](examples/custom_handler_example.cc) | Handler with user-provided arguments |
| [`buffer_pool_example.cc`](examples/buffer_pool_example.cc) | Direct BufferPool/ObjectPool/KernelBuf usage |

Build with `-DTALON_BUILD_EXAMPLES=ON` (default).

---

## 5. Configuration

```cpp
struct AsyncIoConfig {
    // io_uring ring size
    int max_entries = 256;

    // Buffer defaults
    size_t default_buffer_size = 2048;
    size_t kernel_buf_sbo_size = 256;
    size_t task_pool_capacity  = 4096;
    size_t task_pool_grow_batch = 1024;

    // Auto-flush: submit when SQ is this % full
    int auto_flush_threshold_percent = 75;

    // Task defaults
    int default_timeout_ms       = 0;    // 0 = no timeout
    int default_max_retry_count  = -1;   // -1 = no retry

    // TCP
    int tcp_accept_backlog = SOMAXCONN;
    int tcp_default_port   = 8080;
    int connect_timeout_ms = 5000;

    // Backpressure (v2.2.0)
    int max_inflight_ops     = 0;     // 0 = unlimited
    int backpressure_retry_us = 100;  // suggested retry interval
};
```

---

## 6. Testing

```bash
# Build tests
cmake -B build -DTALON_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run by category
ctest --test-dir build -R "^unit/"        --output-on-failure
ctest --test-dir build -R "^integration/" --output-on-failure
ctest --test-dir build -R "^adversarial/" --output-on-failure
ctest --test-dir build -R "^stress/"      --output-on-failure

# Debug builds automatically enable ASAN + UBSAN
```

### Test Coverage (29 files, ~145 test cases)

| Category | Files | Focus |
|----------|-------|-------|
| **unit/** (8) | kernelbuf, ioresult, done_state, function_traits, task_state, user_data, pools | Component isolation, no io_uring |
| **integration/** (7) | file_io, tcp_basic, task_chain, timeout, cancel, retry, repeat_forever | Real I/O with io_uring |
| **stress/** (5) | high_throughput, pool_stress, buffer_reuse, aba_stress, pool_hammer | Multi-threaded concurrency |
| **adversarial/** (9) | null_pointer, invalid_fd, double_submit, shutdown_stress, queue_full, lifetime, memory_leak, backpressure, signal_shutdown | Error paths and edge cases |

---

## 7. Platform Compatibility

| Distribution | libc | Compiler | CI Status |
|-------------|------|----------|-----------|
| Ubuntu 22.04 | glibc 2.35 | GCC 10/12, Clang 14/16 | Verified |
| Ubuntu 24.04 | glibc 2.39 | GCC 10/12, Clang 16 | Verified |
| Alpine 3.20 | musl 1.2 | GCC | Verified |
| CentOS Stream 9 | glibc 2.34 | GCC | Verified |
| Fedora 40 | glibc 2.39 | GCC | Verified |

Additional CI checks: ASAN + UBSAN, Valgrind (memcheck), clang-tidy.

---

## 8. Project Structure

```
async_io/
├── CMakeLists.txt                    # Top-level CMake (header-only INTERFACE target)
├── README.md                         # This file
├── LICENSE                           # MIT
├── .clang-format                     # Clang-format config (Google style)
├── .github/
│   ├── workflows/ci.yml              # Multi-platform CI matrix
│   └── valgrind.supp                 # Valgrind suppression for liburing
├── include/
│   └── talon/
│       ├── async_io.hpp              # Umbrella header
│       ├── async_io_fwd.hpp          # Forward declarations
│       ├── async_io_constants.hpp    # Config, enums, DebugLog
│       ├── async_io_result.hpp       # IOResult + DoneState
│       ├── async_io_traits.hpp       # FunctionTraits
│       ├── async_io_pool.hpp         # ObjectPool<T> + BufferPool + TreiberStack
│       ├── async_io_kernelbuf.hpp    # KernelBuf (SBO → pool → heap)
│       ├── async_io_task.hpp         # InlineFunction + AsyncTask<T>
│       ├── async_io_handler.hpp      # IOHandler event loop
│       ├── async_io_tcp_server.hpp   # TcpServer
│       └── async_io_tcp_client.hpp   # TcpClient
├── examples/                         # 12 self-contained example programs
│   └── CMakeLists.txt
├── tests/                            # 29 test files
│   ├── CMakeLists.txt
│   ├── unit/                         # 8 component tests
│   ├── integration/                  # 7 io_uring integration tests
│   ├── stress/                       # 5 concurrency/throughput tests
│   └── adversarial/                  # 9 edge-case tests
```

---

## 9. Handler API

All handlers follow the signature:

```cpp
void MyHandler(KernelBuf* buf);                           // void return
Ret  MyHandler(KernelBuf* buf);                           // with return value
void MyHandler(KernelBuf* buf, Arg1 a, Arg2 b);           // with user arguments
```

Inside the handler:
- `buf->Data()` — pointer to I/O buffer
- `buf->BytesTransferred()` — bytes read/written (negative = errno)
- `buf->ActiveFileDescriptor()` — fd that triggered the event
- `buf->FdOffset()` — file seek offset (for read/write at position)

**Important:** `KernelBuf*` is valid ONLY during handler invocation.  Do not store the
pointer beyond the handler call.

---

## 10. Design Principles

1. **Zero-exception** — all errors propagate via return values and `DoneState`
2. **No `exit()` calls** — the library never terminates the process
3. **RAII everywhere** — fds, buffers, locks all released deterministically
4. **Header-only** — zero link-time dependencies beyond `liburing` and `pthread`
5. **Cache-line optimized** — `alignas(64)` hot fields, hot/cold separation, `__builtin_prefetch`
6. **Google C++ Style** — PascalCase functions, `kConstantName` enums, `trailing_underscore_` class members
7. **ABA-safe lock-free pools** — tagged-pointer TreiberStack on x86\_64/ARMv8.1+, mutex fallback otherwise
8. **Backpressure** — configurable inflight limit prevents unbounded memory growth

---

## 11. License

MIT © Taloncdy — see [LICENSE](LICENSE) for full text.
