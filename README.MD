# Talon Async IO: High-Performance Async I/O Framework on io_uring

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)]()
[![Linux](https://img.shields.io/badge/Kernel-%E2%89%A55.11-orange)](https://www.kernel.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![CI](https://img.shields.io/badge/CI-Ubuntu%20%7C%20Alpine%20%7C%20CentOS%20%7C%20Fedora-brightgreen)]()

**Talon Async IO** is a modern C++17 async I/O library built on Linux `io_uring` (kernel >= 5.11).
It uses a handler-driven programming model with a single-threaded event loop, providing
high-throughput, low-latency I/O for file operations, TCP servers, and TCP clients.

---

## Features

- **Lock-free memory pools** — `ObjectPool<T>` for task objects, `BufferPool` for I/O buffers
  (11 power-of-two size classes, per-thread caching, zero `shared_ptr` atomic overhead on hot path)
- **DoneState** — inline promise replacement; zero heap allocations per `addTask()` call
- **KernelBuf SBO** — inline storage for buffers <= 256 bytes (no heap allocation)
- **InlineFunction** — type-erased callable with 64-byte SBO (eliminates `std::function` heap alloc)
- **Batched submission & completion** — amortizes `io_uring_enter` syscall overhead
- **Linked SQE timeout** — correct `IOSQE_IO_LINK` + `io_uring_prep_link_timeout` pattern
- **Graceful shutdown** — `request_shutdown()` + `join()` (no detached threads)
- **Zero `exit()` calls** — all errors reported via return values
- **No C++ exceptions** — `std::optional`, status codes, `DoneState` for error propagation
- **Google C++ Style** — `snake_case` functions, `PascalCase` classes, `kConstantName` enums
- **Cache-line optimized** — `alignas(64)` hot fields, hot/cold field separation, prefetch hints

---

## Supported I/O Operations

| Task Type | Description |
|-----------|-------------|
| `TaskType::kRead` | File / socket read |
| `TaskType::kWrite` | File / socket write |
| `TaskType::kAccept` | TCP accept (server) |
| `TaskType::kConnect` | TCP connect (client) |
| `TaskType::kTimeout` | Linked timeout |
| `TaskType::kCancel` | Cancel pending I/O |

### Task Patterns

| Pattern | Description | API |
|---------|-------------|-----|
| One-shot | Single execution (default) | `addTask(task)` |
| Repeat-forever | Long-lived (accept/read loops) | `task->set_repeat_forever(true)` |
| Task chaining | Sequential execution graph | `task1->set_next_task(task2)` |
| Retry on failure | Automatic retry with count limit | `task->set_max_retry_count(3)` |
| Timeout | Linked timeout for any I/O | `task->set_timeout(5000)` |
| Cancel | Cancel pending I/O | `task->cancel(io.get_iouring())` |

---

## Quick Start

### Build Requirements

- Linux kernel >= 5.11
- C++17 compiler (GCC >= 10 or Clang >= 14)
- `liburing-dev` (io_uring userspace library)
- CMake >= 3.16

### Installation

```bash
# Install liburing (Ubuntu/Debian)
sudo apt-get install liburing-dev

# Build and install async_io
git clone https://github.com/taloncdy/async_io.git
cd async_io
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### CMake Integration (as third-party dependency)

```cmake
find_package(talon-async-io REQUIRED)
target_link_libraries(my_app PRIVATE talon_async_io)
```

Or use FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    talon-async-io
    GIT_REPOSITORY https://github.com/taloncdy/async_io.git
    GIT_TAG v2.1.0
)
FetchContent_MakeAvailable(talon-async-io)
target_link_libraries(my_app PRIVATE talon_async_io)
```

### Minimal Example — File Read

```cpp
#include <fcntl.h>
#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;

void read_handler(KernelBuf* buf) {
    int bytes = buf->bytes_transferred();
    int fd = buf->active_file_descriptor();
    printf("Read %d bytes from fd %d: %.*s\n",
           bytes, fd, bytes, buf->data());
}

int main() {
    IOHandler io;
    if (!io.initialized()) {
        fprintf(stderr, "Init failed: %s\n", io.init_error().c_str());
        return 1;
    }

    int fd = open("/etc/hostname", O_RDONLY);
    auto* task = createTaskWithHandler(fd, read_handler);
    task->set_task_type(TaskType::kRead);
    io.addTask(task);

    sleep(1);          // Wait for completion
    io.request_shutdown();
    io.join();
    close(fd);
    return 0;
}
```

### TCP Echo Server

```cpp
void echo_handler(KernelBuf* buf) {
    int bytes = buf->bytes_transferred();
    int fd = buf->active_file_descriptor();
    // Echo data back to client
    auto* write_task = createTaskWithHandler(fd);
    write_task->set_task_type(TaskType::kWrite);
    write_task->set_buffer_soft(
        std::make_unique<KernelBuf>(bytes));
    std::memcpy(write_task->get_buffer()->data(),
                buf->data(), bytes);
    io.addTask(write_task);
}

void accept_handler(KernelBuf* buf) {
    int client_fd = buf->bytes_transferred();
    auto* read_task = createTaskWithHandler(
        client_fd, echo_handler);
    read_task->set_task_type(TaskType::kRead);
    read_task->set_repeat_forever(true);
    io.addTask(read_task);
}

int main() {
    IOHandler io;
    io.init_tcp_server(8080);
    auto* accept_task = createTaskWithHandler(
        io.listen_fd(), accept_handler);
    accept_task->set_task_type(TaskType::kAccept);
    accept_task->set_repeat_forever(true);
    io.addTask(accept_task);

    sleep(INT_MAX);  // Run forever
    return 0;
}
```

---

## Project Structure

```
async_io/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── include/
│   └── talon/
│       ├── async_io.hpp              # Umbrella header (include this)
│       ├── async_io_fwd.hpp          # Forward declarations
│       ├── async_io_constants.hpp    # Enums, constants, debug logging
│       ├── async_io_result.hpp       # IOResult + DoneState
│       ├── async_io_kernelbuf.hpp    # KernelBuf with SBO + pool
│       ├── async_io_pool.hpp         # ObjectPool<T>, BufferPool
│       ├── async_io_traits.hpp       # FunctionTraits
│       ├── async_io_task.hpp         # AsyncTask + createTaskWithHandler
│       └── async_io_handler.hpp      # IOHandler event loop
├── examples/
│   ├── read_file_example.cc
│   ├── read_large_file_example.cc
│   ├── server_tcp_example.cc
│   └── tcp_wr_example.cc
├── tests/
│   ├── unit/           (7 tests: kernelbuf, ioresult, pools, done_state, ...)
│   ├── integration/    (7 tests: file_io, tcp, timeout, cancel, retry, ...)
│   ├── stress/         (3 tests: high_throughput, pool_stress, buffer_reuse)
│   └── adversarial/    (7 tests: null_ptr, invalid_fd, double_submit, ...)
└── .github/workflows/
    └── ci.yml          (CI: Ubuntu, Alpine, CentOS, Fedora × GCC, Clang)
```

---

## Handler API

All user-provided handlers follow the signature:

```cpp
void my_handler(KernelBuf* buf);
// or with return value:
Ret my_handler(KernelBuf* buf);
// or with user arguments:
Ret my_handler(KernelBuf* buf, Arg1 arg1, Arg2 arg2);
```

- `buf->data()` — pointer to the I/O buffer
- `buf->bytes_transferred()` — bytes read/written (or negative error code)
- `buf->active_file_descriptor()` — fd that triggered the event
- `buf->get_fd_offset()` / `buf->set_fd_offset()` — file seek offset

**Important**: `KernelBuf*` is valid ONLY during the handler invocation. Do NOT store the pointer beyond the handler call.

---

## Performance Architecture

### Memory Allocation Path

| Operation | Before (v1.x) | After (v2.1) |
|-----------|---------------|--------------|
| Task creation | `operator new` (~256B) | `ObjectPool::acquire()` (lock-free) |
| Buffer creation (<=256B) | `std::vector` heap (2048B) | SBO inline (0 heap alloc) |
| Buffer creation (256-65536) | `std::vector` heap | `BufferPool` (lock-free per-class) |
| Promise per `addTask()` | `make_unique<promise>` (2 allocs) | `DoneState::reset()` (0 allocs) |
| Handler invocation | `shared_ptr` atomic inc/dec (4 ops) | Raw `KernelBuf*` (0 ops) |
| Handler storage | `std::function` heap alloc | `InlineFunction` SBO (64B inline) |

### Cache Line Layout (AsyncTask)

```
[ Hot Line 1 (alignas 64): task_state_ | detach_ | done_state_ | buffer_ | user_data_ | fd_ ]
[ Warm Line:   task_type_ | handler_ | next_ | is_cancel_ | try_count_                     ]
[ Cold Lines:  timeout_ms_ | max_retry_count_ | repeat_* | args_tuple_ | debug_str_          ]
```

---

## Testing

```bash
# Build with tests
cmake -B build -DTALON_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure

# Run by category
ctest --test-dir build -R "^unit/"
ctest --test-dir build -R "^integration/"
ctest --test-dir build -R "^adversarial/"
ctest --test-dir build -R "^stress/"

# With sanitizers (Debug mode)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
# ASAN+UBSAN enabled automatically in Debug builds
```

---

## Platform Compatibility

| Distribution | libc | Compiler | Status |
|-------------|------|----------|--------|
| Ubuntu 22.04 | glibc 2.35 | GCC 10/12, Clang 14/16 | CI verified |
| Ubuntu 24.04 | glibc 2.39 | GCC 10/12, Clang 16 | CI verified |
| Alpine 3.20 | musl 1.2 | GCC | CI verified |
| CentOS Stream 9 | glibc 2.34 | GCC | CI verified |
| Fedora 40 | glibc 2.39 | GCC | CI verified |

---

## License

MIT © Taloncdy
