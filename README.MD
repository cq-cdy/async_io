# Talon Async IO: 基于 io_uring 的高性能异步 I/O 框架

[![Header-only](https://img.shields.io/badge/Header--only-Efficient-brightgreen)]()
[![Linux](https://img.shields.io/badge/Platform-Linux%20Kernel≥5.11-blue)](https://www.kernel.org/)

**Talon Async IO** 是一个基于 Linux io_uring 子系统构建的现代异步 I/O 开发框架，采用Handler驱动创建任务的编程模型和单线程事件循环架构，为开发者提供高性能、低延迟的 I/O 操作解决方案。

## 🌟 核心特性

### 🚀 零依赖高性能
- 纯头文件实现，无额外编译依赖
- Handler驱动编程模型，同一式IO编程模型，减少传统异步函数回调地狱
- 封装 io_uring 用户态-内核态零拷贝系统调用
- 单线程事件循环设计，极致减少上下文切换开销

### 🧩 统一抽象层
- 文件 I/O 与网络 I/O 统一操作语义
- 支持多种 I/O 模式：
  | 模式类型        | 描述                      |
  |----------------|--------------------------|
  | 短连接任务      | 默认的单次执行模式        |
  | 长连接任务      | 支持永久循环执行          |
  | 链式任务        | 通过 next 指针构建任务拓扑 |

### ⚡ 现代化开发范式

```cpp
talon::IOHandler io;
// 示例：异步读处理器
Ret custom_read_handler(KernelBufPtr kbf, out_res* ptr) {
    auto num = kbf->io_ret_v();
    auto fd = kbf->io_ret_fd();
    printf("in example_read_handler ret = %d, fd = %d\n", num, fd);
    for (int i = 0; i < num; i++) {
        printf("%c", kbf->buf_[i]);
    }
    ptr = some excute result;
    return 
}

// 任务创建与提交
auto task = createTaskWithHandler(
    fd, 
    custom_read_handler, 
    ptr_arg
);
task->set_timeout(100ms);
readtask->set_task_type(task::TaskType::READ);
accpet_task->set_repeat_forever(true);
io.addTask(task);
```

### ⚡其他
其更多使用方式，如客户端服务端的网络IO通讯、本地文件读取、大文件的读取等相关操作见相关example.cc文件。
### 后续会提供相关性能测试

continue...