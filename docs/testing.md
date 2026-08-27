# 测试与验收

## 1. 测试目标

L-W 的测试不只验证能否编译，还分别覆盖：

- 正常路径；
- 边界路径；
- 异常路径；
- 重复构造与析构；
- 线程所有权；
- 生命周期；
- 内存与未定义行为；
- 数据竞争；
- 真实进程启动和退出。

测试证据分为三类：

1. 自包含自动化测试；
2. 真实进程级验收；
3. Sanitizer 验收。

## 2. 快速回归

```bash
make test-all
```

`test-all` 使用以下严格编译条件：

```text
-std=c++17
-Wall
-Wextra
-Werror
```

专项运行 LoadGen 失败路径：

```bash
make test-load-gen-failure
```

## 3. 测试矩阵

| 领域 | 主要测试 | 关键验证点 |
| --- | --- | --- |
| Buffer | `buffer_test.cpp` | 追加、读取、扩容和边界 |
| EventLoop | `event_loop_test.cpp` | epoll、eventfd、跨线程队列、quit、重复生命周期 |
| EventLoopThread | `event_loop_thread_test.cpp` | 启动同步、owner thread、析构停止 |
| EventLoopThreadPool | `event_loop_thread_pool_test.cpp` | 轮询分配、零线程回退、错误线程拒绝 |
| TcpServer | `tcp_server_test.cpp` | accept、I/O EventLoop 分配、连接关闭和析构 |
| Codec | `codec_test.cpp` | 帧布局、半包、粘包、多帧、非法长度、回调停止 |
| MessageCallback | `message_callback_test.cpp` | 网络字节到完整帧回调的集成链 |
| Protocol | `protocol_test.cpp`、`protocol_encode_test.cpp` | 请求解码、响应编码、二进制内容和异常布局 |
| Room | `room_test.cpp` | JOIN/LEAVE、成员关系、状态机和快照 |
| RoomManager | `room_manager_test.cpp` | 房间路由、稳定身份、断开与重新绑定 |
| RoomService | `room_service_test.cpp` | 业务协议、帧顺序、重连和会话超时 |
| Tick | `game_state_test.cpp`、`tick_test.cpp` | 命令批次、权威更新和 timerfd 生命周期 |
| 连接关闭 | `tcp_server_closed_callback_test.cpp` | 关闭通知只触发一次、base 线程回调 |
| 背压 | `backpressure_test.cpp` | 容量预留、部分写、快照丢弃、关键消息关闭 |
| 持久化 | `persistence_state_test.cpp`、`persistence_store_test.cpp` | 编解码、原子保存、恢复、损坏数据 |
| LoadGen | `load_gen_failure_test.cpp` | 首次失败、系统调用/errno、心跳超时和失败报告 |

## 4. 关键不变量

### 线程所有权

- `Connection` 的 socket 和 Buffer 只在所属 I/O EventLoop 操作；
- Room、Session 和权威状态只在 base EventLoop 修改；
- 跨线程任务通过 EventLoop 队列和 eventfd 唤醒执行。

### 生命周期

- EventLoopThread 析构会停止并等待线程退出；
- 每个运行期连接只向业务层通知一次关闭；
- 旧连接的延迟回调不能删除新连接恢复后的 Session 绑定；
- 定时器、持久化服务和 signal fd 都有明确停止路径。

### 协议与状态

- 半包不会提前消费；
- 多个完整帧保持到达顺序；
- 结构错误不会被误当成业务错误；
- 每名玩家每 Tick 最多提交一条命令；
- 快照表示已经完成的权威 Tick。

### 背压

- 写缓冲容量必须先成功预留再排队；
- 高频快照允许丢弃；
- 关键响应不能静默丢失；
- 慢连接关闭后必须归还写缓冲债务。

## 5. Sanitizer

P1 在 Linux VM 上执行了：

- AddressSanitizer；
- UndefinedBehaviorSanitizer；
- ThreadSanitizer。

LoadGen 失败路径的 ASan/UBSan 和 TSan 运行均未报告错误。

TSan 启动前临时调整了 `vm.mmap_rnd_bits`，测试脚本在结束后恢复并验证：

```text
original_mmap_rnd_bits=32
restored_mmap_rnd_bits=32
```

这些属于项目维护者在 VM 上提供的运行证据，不等同于 GitHub 托管环境中的自动 CI 结果。

## 6. 真实入口验收

除 `make test-all` 外，P1 还验证了：

- 真实服务器启动和监听；
- SIGINT、SIGTERM 正常退出；
- 关闭前最终检查点保存；
- 重启后恢复检查点；
- 损坏检查点拒绝启动；
- 损坏检查点不覆盖原文件；
- 心跳超时关闭；
- 慢读连接和背压隔离；
- 压测结束后服务器和 LoadGen 进程清理。

## 7. 证据边界

编译成功只证明编译和链接。

`make test-all` 只证明它覆盖的自动化测试。

Sanitizer 只证明本次执行路径没有触发对应诊断。

项目结论必须同时标明代码版本、命令、环境和证据来源。
