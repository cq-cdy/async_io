# Talon Async I/O — 开发任务与审查 Prompt 整理

> 本文档汇总了 `talon-async-io` 项目从初始审计到发布准备的全部用户指令与执行范围。

---

## Phase 0 — 项目定位

你是世界级 C++ 系统架构师，专精高性能异步 I/O 库。代码库是即将发布的 **C++17 header-only 异步 I/O 库**，基于 Linux `io_uring` (kernel >= 5.11)，目标用户涵盖企业与汽车行业 (ISO 26262 / ASIL-aware)。严格遵循 **Google C++ Style Guide** 与 **Google C++ Guidelines**。

---

## Phase 1 — 代码库深度理解

1. 通读并映射全部代码库（每个源文件、头文件、测试、构建配置）
2. 输出架构文档：目录结构、公共 API、内部实现层、依赖关系、并发模型、内存管理
3. 生成依赖关系图，标记循环依赖或异常耦合

---

## Phase 2 — 标准与风格审计

- C++17 only，无 C++20/23 特性，无编译器扩展
- Google C++ Style Guide：命名规范、头文件保护 (`PROJECT_PATH_FILE_H_`)、include 顺序、访问修饰符、const 正确性、`explicit` 单参构造、无 `const&` 入参
- Google C++ Guidelines：`std::optional/variant/string_view`、值语义、`[[nodiscard]]` 广泛使用、`= delete` 优于 private 未定义成员
- 接口优雅性：函数名可读、参数顺序一致、错误处理单一显式约定
- **if/for/while 必须有大括号**，不允许省略
- **函数命名 PascalCase**（访问器/修改器也不例外，统一 PascalCase）
- **struct 成员无后缀下划线**（class 成员必须有）

---

## Phase 3 — 安全性与可移植性（最高优先级）

### 3a. 内存安全
- 零原始 `new`/`delete`（除底层分配器内部）
- 全部资源 RAII 包裹（fd、socket、锁、定时器）
- 无未初始化读取、缓冲区边界检查、无隐式窄化转换
- **零内存泄漏**、零 double-free

### 3b. 线程安全
- 识别全部共享可变状态，验证保护机制
- 审计锁顺序，标记死锁隐患
- 验证信号处理器 async-signal-safe

### 3c. 资源安全
- fd 耗尽有上界、错误传播不崩溃
- `std::bad_alloc` 优雅处理、无栈溢出

### 3d. Linux 可移植性
- 避免 glibc 特定行为，测试 musl (Alpine)
- 32/64-bit clean，字节序显式处理

---

## Phase 4 — 性能审计

- 缓存行对齐 + 避免 false sharing (`alignas(64)`)
- 热路径数据分离、prefetch 提示
- 背压传播 —— 慢消费者不能导致无界缓冲增长
- 空闲 CPU 接近零（无忙等）、内存与活跃连接成正比、热路径无隐式分配

---

## Phase 5 — 接口隔离与模块化

- Server/Client 独立可编译链接
- 公共头文件自给自足
- 版本化 ABI（`inline namespace v2_2_0`）

---

## Phase 6 — 测试

- **每条公共函数至少一个测试**
- **每个错误路径都被覆盖**
- 边缘情况：空输入、最大输入、快速开关循环、零字节写入、部分读、超时
- 数据竞争压力测试（ThreadSanitizer）、死锁检测、ABA 压力测试
- **测试失败协议：绝不修改测试来通过测试。修复实现代码。**
- **所有测试必须通过**：单元、集成、压力、对抗

---

## Phase 7 — 可交付物

1. 严重性分级发现摘要 (Critical/High/Medium/Low)
2. 每文件修复提交（单逻辑变更单 commit）
3. 测试覆盖报告
4. **最终验证：clang-format 格式化 + 全部测试通过 + 推送到 GitHub**

---

## 架构修复任务（识别但未修复的问题）

| # | 问题 | 严重性 | 修复 |
|---|------|--------|------|
| 1 | TreiberStack ABA 问题 | Medium | 标记指针 + 128 位 CAS（编译时选择 MutexStack 回退） |
| 2 | 无背压机制 | Low | `max_inflight_ops` + `inflight_count_` + 原子门控 |
| 3 | 事件循环无信号处理 | Low | `InstallSignalHandlers()` + `OnSignal()` + NOP 唤醒 |

---

## 代码风格修复（全局扫描）

- **v1_0_0 → v2_2_0**：第一版使用 `v2_2_0` inline namespace
- **全部函数命名 → PascalCase**：`listen_fd()`→`ListenFd()`，`last_error()`→`LastError()`，`data()`→`Data()`，`resize()`→`Resize()` 等
- **struct 成员去下划线**：`size_`→`size`，`offset_`→`offset`
- **if 条件加大括号**：不允许单行省略
- **指针调用用 `->`**：修复 `task.SetDebugStr` → `task->SetDebugStr`
- **friend 声明正确 namespace**：`IOHandler` 前向声明移出 `task` 子命名空间
- **`[[nodiscard]]` 广泛使用**：但 `AddTask` 除外（内部调用不使用返回值）

---

## 示例程序（12 个）

要求全部示例：
- 自包含、独立编译运行
- 正确错误处理 + 优雅关闭 (`RequestShutdown` + `Join`)
- 资源清理 (close fd, unlink temp files)
- 覆盖全部主要特性

| # | 示例 | 特性 |
|---|------|------|
| 1 | `read_file_example` | 基础读、`WaitForCompletion` |
| 2 | `read_large_file_example` | 分块链式读、`FdOffset` |
| 3 | `file_write_example` | 写 + 读回验证 |
| 4 | `server_tcp_example` | TCP echo 服务器、信号关闭 |
| 5 | `tcp_client_example` | connect → write → read 链 |
| 6 | `tcp_wr_example` | 多消息客户端、repeat-forever |
| 7 | `task_chain_example` | `SetNextTask` 显式链 |
| 8 | `timeout_retry_example` | `SetTimeout` + `SetMaxRetryCount` |
| 9 | `backpressure_example` | `max_inflight_ops` + 重试 |
| 10 | `signal_shutdown_example` | `InstallSignalHandlers` 自定义 |
| 11 | `custom_handler_example` | handler 带用户参数 |
| 12 | `buffer_pool_example` | `BufferPool`/`ObjectPool` 直接使用 |

---

## 测试验证协议

```bash
# 全部测试必须通过
ctest --test-dir build -R "^unit/"        --output-on-failure
ctest --test-dir build -R "^integration/" --output-on-failure
ctest --test-dir build -R "^adversarial/" --output-on-failure
ctest --test-dir build -R "^stress/"      --output-on-failure

# clang-format 格式化
find . -name '*.hpp' -o -name '*.cc' | xargs clang-format -i
```

---

## 最终发布检查清单

- [ ] 零内存泄漏
- [ ] 零 Crash / SIGSEGV
- [ ] 线程安全 (ABA-safe TreiberStack / CAS gate / DoneState)
- [ ] 零死锁 (单锁或无锁路径)
- [ ] 所有 29 个测试文件通过
- [ ] 所有 12 个示例编译运行正确
- [ ] clang-format 格式化
- [ ] README.md 完整（架构、API 参考、配置、示例索引）
- [ ] LICENSE 完整
- [ ] GitHub 推送成功
