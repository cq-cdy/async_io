#ifndef TALON_ASYNC_IO_FWD_HPP_
#define TALON_ASYNC_IO_FWD_HPP_

#include <cstdint>
#include <memory>

namespace talon {
inline namespace v1_0_0 {

struct AsyncIoConfig;

namespace task {

enum class TaskType : int;
enum class TaskState : int;
enum class EventFlag : uint64_t;

struct IOResult;
struct KernelBuf;
using KernelBufPtr = std::unique_ptr<KernelBuf>;

template <typename Ret = void, typename... UserArgs>
class AsyncTask;

struct DefaultHandler;
struct Task;

}  // namespace task

class IOHandler;
class TcpServer;
class TcpClient;

namespace memory {

template <typename T>
class ObjectPool;

class BufferPool;

}  // namespace memory

}  // namespace v1_0_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_FWD_HPP_
