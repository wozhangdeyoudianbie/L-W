# L-W

L-W 是一个基于 C++17 和 Linux Reactor 模型实现的实时多人房间服务器。

项目使用 TCP 二进制协议、非阻塞 I/O、epoll ET 和 one-loop-per-thread 模型，包含房间状态机、权威 Tick、心跳与重连、背压、持久化及独立压测工具。

## 项目目标

本项目用于实现并验证一个单机实时房间服务器的完整执行链：

- 网络连接与非阻塞 I/O；
- 二进制协议编解码；
- 跨线程任务投递；
- 房间业务与权威状态；
- 周期 Tick 与快照广播；
- 心跳、重连和慢客户端处理；
- 检查点持久化与优雅关闭；
- 压测、失败定位和工程回归。

## 主要功能

- 非阻塞 TCP 连接与部分读写
- epoll ET 和 eventfd 唤醒
- one-loop-per-thread I/O 模型
- 半包、粘包和多帧处理
- TCP 二进制协议与 Codec
- JOIN、LEAVE、CHAT 房间业务
- 房间状态机与固定容量开局
- MOVE、ATTACK 权威状态更新
- 一名玩家每个 Tick 最多提交一条命令
- 周期 Tick 与状态快照广播
- 心跳检测、超时关闭和 token 重连
- 慢客户端隔离与写缓冲背压
- 检查点保存、恢复和优雅关闭
- 独立 LoadGen 压测与失败报告

## 核心执行链

```text
客户端
  ↓ TCP字节
I/O工作线程 / Connection / Codec
  ↓ 完整协议帧
base EventLoop
  ↓
RoomService
  ↓
RoomManager / Room
  ↓
pending_commands_
  ↓ 50ms Tick
权威 Gamestate
  ↓
状态快照
  ↓
Connection所属I/O工作线程
  ↓ TCP字节
客户端
```

Connection 的 socket、读写缓冲和 I/O 生命周期由所属 I/O EventLoop 管理。

房间成员、Session、权威状态、命令提交和 Tick 结算由 base EventLoop 串行协调。

## 构建与运行

项目要求：

- Linux
- 支持 C++17 的 g++
- GNU Make
- pthread

构建服务器：

```bash
make
```

运行服务器：

```bash
make run
```

服务器默认监听 8080 端口。

运行时数据保存在：

```text
data/server.checkpoint
logs/server.log
```

`data/` 和 `logs/` 是运行时目录，不属于源代码。

## 测试

执行全部自包含自动化测试：

```bash
make test-all
```

只执行 LoadGen 失败报告测试：

```bash
make test-load-gen-failure
```

真实服务器入口、心跳超时、优雅关闭和持久化恢复属于需要服务器进程配合的独立验收。

## 压测工具

构建压测工具：

```bash
make load-checkpoint-generator load-gen
```

LoadGen 是一个独立的 TCP 客户端进程。

它负责建立连接、发送协议帧、接收服务器响应并生成压测报告，但不能直接访问服务器内部的 Room、Tick 或任务队列。

详细压测环境、实验结果和限制记录在：

```text
docs/performance.md
```

## 项目边界

当前项目展示的是单机 Reactor 实时房间服务器的完整工程链。

它不是分布式游戏后端，也没有实现数据库集群、服务发现、跨服匹配或生产级部署系统。

服务器和 LoadGen 位于同一台虚拟机时得到的压测结果，只能作为当前实验环境的数据，不能直接解释为生产环境最大在线人数。
