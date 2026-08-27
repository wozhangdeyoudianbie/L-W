# 压测与手工压力工具

`benchmarks/` 只保存容量准备、压测器和需要人工编排的压力场景。

能够独立运行并自动判断成功或失败的功能测试继续放在 `tests/`。

## 文件说明

| 文件 | 作用 |
| --- | --- |
| `load_checkpoint_generator.cpp` | 生成大量空房间检查点 |
| `load_gen.h` | LoadGen 配置、客户端状态、阶段、指标与失败结构 |
| `load_gen.cpp` | 单线程 epoll 压测器、负载阶段、统计和报告输出 |
| `load_gen_main.cpp` | 命令行参数解析与 LoadGen 入口 |
| `slow_echo_client.cpp` | 暂停读取响应，用于观察背压和慢连接隔离 |

## 构建

```bash
make load-checkpoint-generator
make load-gen
make slow-echo-client
```

## 生成房间检查点

```bash
./build/load_checkpoint_generator \
    <检查点路径> \
    <房间数量> \
    <每个房间容量>
```

例如生成 1150 个、每个容量为 4 的空房间：

```bash
mkdir -p data
./build/load_checkpoint_generator data/server.checkpoint 1150 4
```

## 运行 LoadGen

先启动服务器，再运行：

```bash
./build/load_gen \
    <报告目录> \
    [服务器地址] \
    [端口] \
    [客户端数量] \
    [房间容量] \
    [每秒连接数]
```

示例：

```bash
mkdir -p benchmarks/results/manual-4600

./build/load_gen \
    benchmarks/results/manual-4600 \
    127.0.0.1 \
    8080 \
    4600 \
    4 \
    100
```

LoadGen 依次执行：

```text
create
  -> connect
  -> join
  -> warm-up
  -> measure
  -> drain
  -> finished / failed
```

它使用固定节拍产生 MOVE，并记录计划时间与实际发送时间之间的调度延迟。

## 报告文件

| 文件 | 内容 |
| --- | --- |
| `summary.txt` | 配置、状态、第一次失败、连接、JOIN、MOVE、心跳、快照和正确性汇总 |
| `heartbeat_rtt_us.csv` | 心跳往返时间，单位微秒 |
| `snapshot_interval_us.csv` | 连续快照间隔，单位微秒 |
| `scheduler_lag_us.csv` | MOVE 计划时间与实际发送时间之差，单位微秒 |

`summary.txt` 在成功和失败路径都会尝试生成。

第一次失败只记录一次，避免后续清理错误覆盖根因。

失败原因可以区分：

- 连接错误和连接超时；
- JOIN 错误和 JOIN 超时；
- heartbeat ACK 超时；
- 协议错误；
- 服务器 ERROR 帧；
- `epoll_wait`；
- `getsockopt(SO_ERROR)`；
- `recv`；
- `send`；
- 系统调用对应的 `errno`；
- 意外关闭；
- 内部状态错误。

## 慢读场景

服务器启动后运行：

```bash
./build/slow_echo_client
```

该工具用于制造不及时读取响应的客户端，观察：

- `Connection` 部分写和 `EPOLLOUT`；
- 写缓冲上限和容量预留；
- 高频快照丢弃；
- 关键消息无法排队时关闭慢连接；
- 单个慢连接是否影响其他连接。

## 结果存放

原始 CSV、日志和临时检查点统一放入：

```text
benchmarks/results/
```

该目录默认不进入 Git。

需要长期保存的结果应整理为 Markdown 报告，放入：

```text
docs/results/
```

长期报告必须记录：

- 代码版本；
- 机器与操作系统；
- 客户端模型；
- 负载参数；
- 完整正确性计数；
- 延迟分位数；
- 资源采样方法；
- 实验限制。

不要把同机单次结果描述为生产容量或稳定最大在线人数。
