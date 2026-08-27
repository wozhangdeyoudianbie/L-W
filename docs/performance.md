## 执行摘要

> [!NOTE]
> 本结果是固定 4 vCPU 单 VM 回环环境中的一次成功观测，
> 不表示生产容量或稳定最大在线人数。

| 负载 | 结果 |
| --- | ---: |
| 客户端 / 房间 | 4600 / 1150 |
| MOVE / 快照吞吐 | 46,001 / 91,999 帧每秒 |
| 心跳 RTT p99 | 26.390 ms |
| 协议错误 / 意外断开 / Tick 缺口 | 0 / 0 / 0 |


# 性能与压测报告

## 1. 测试目标

P1 压测用于验证：

- 大量 TCP 客户端能否完成连接和 JOIN；
- 持续 MOVE 和状态快照下是否出现协议错误；
- 是否发生意外关闭或 Tick 缺口；
- 心跳 RTT、快照间隔和 LoadGen 调度延迟是否可观测；
- 失败时能否保留第一次失败上下文；
- 服务器与 LoadGen 同机运行时受到哪些环境因素影响。

本报告不用于声明生产环境最大在线人数。

## 2. 固定负载模型

| 项目 | 配置 |
| --- | --- |
| 系统 | Linux 虚拟机 |
| 逻辑 CPU | 4 |
| 部署 | 服务器与 LoadGen 同一台 VM |
| 网络 | TCP 回环地址 `127.0.0.1:8080` |
| 文件描述符上限 | 1,048,576 |
| 房间容量 | 4 名玩家 |
| 建连速率 | 100 connections/s |
| MOVE 周期 | 每客户端 100ms |
| 心跳周期 | 2000ms |
| warm-up | 10s |
| measure | 60s |
| drain | 2s |
| 服务器 Tick | 50ms |

LoadGen 是独立的单线程 epoll TCP 客户端进程。

它只能观察已发送帧、服务器响应、时序和 socket 结果，不能直接读取服务器内部队列或 Room 状态。

## 3. 实验演进

| 实验 | 结果 | 观察 |
| --- | --- | --- |
| 2000～4300 客户端历史扫描 | 完整工作负载通过 | 连接和 JOIN 成功，未发现协议错误、意外断开或 Tick 缺口 |
| 旧 4600 客户端实验 | 失败但没有失败报告 | 当时 LoadGen 失败后不写 `summary.txt`，无法定位阶段和原因 |
| 4000 客户端性能采集 | `already_submitted` | 同机调度抖动使两条 MOVE 进入同一逻辑 Tick |
| 2026-08-26，4600 客户端实验 | 完整工作负载通过 | 第一次失败为空，全部正确性约束通过 |

结果并不随客户端数量单调变化。

这说明单次同机实验受到调度、采样工具和进程资源竞争影响。

历史失败是真实现象，但不是确定容量边界。

## 4. 失败可观测性

LoadGen 现在无论成功或失败都会尝试写入 `summary.txt`。

第一次失败包含：

- LoadGen 阶段；
- 失败原因；
- 客户端编号；
- 失败时 active client 数量；
- 失败系统调用和立即保存的 `errno`；
- 服务器 ERROR 帧中的 request type；
- 服务器 ERROR 帧中的 error code。

心跳 ACK 过期单独记录为 `heartbeat_timeout`，不再与普通 `socket_error` 混为一类。

## 5. 4600 客户端结果

本次测试工作区随后提交为：

```text
d00f3a27c1f0a92a53ff9a678a747706af4700f6
```

### 正确性

| 指标 | 结果 |
| --- | ---: |
| connection attempts / successes / failures | 4600 / 4600 / 0 |
| JOIN successes / failures | 4600 / 0 |
| planned / sent MOVE frames | 2,760,130 / 2,760,130 |
| missed MOVE deadlines | 0 |
| heartbeat frames / ACKs | 138,009 / 138,009 |
| snapshot frames | 5,520,000 |
| Tick gaps | 0 |
| ERROR frames | 0 |
| protocol errors | 0 |
| unexpected closes | 0 |
| workload completed | true |
| correctness passed | true |

### 吞吐与延迟

| 指标 | p50 | p95 | p99 | 最大值 |
| --- | ---: | ---: | ---: | ---: |
| heartbeat RTT | 4.825ms | 20.622ms | 26.390ms | 72.251ms |
| snapshot interval | 50.140ms | 67.151ms | 75.557ms | 109.285ms |
| LoadGen scheduler lag | 0.820ms | 2.311ms | 3.112ms | 18.702ms |

吞吐结果：

- MOVE：46,001.430 帧/秒；
- 快照：91,998.527 帧/秒；
- 总运行时间：约 119.992 秒。

详细计数见 [2026-08-26 单 VM 4600 客户端记录](results/2026-08-26-single-vm-4600.md)。

## 6. 资源数据

自定义进程采样摘要记录：

- server：RSS 约 50,560 KiB、6 个线程、4619 个 fd；
- LoadGen：RSS 峰值约 153,228 KiB、1 个线程、4604 个 fd。

同一日志中的 `process peaks` 与 `pidstat` 属于不同采样口径。

`pidstat` 的 1 秒区间中，server 和 LoadGen 都曾接近一个 CPU 核。

不能把生命周期累计百分比与区间瞬时百分比混合成一个统一的“CPU 峰值”。

## 7. 结论

在固定的 4 vCPU 单 VM 回环环境、100 connections/s、每客户端 10 MOVE/s、50ms Tick 条件下，本项目完成了一次 4600 客户端正确性运行。

该结果证明：

- 当前工作负载在该环境中完成过一次完整运行；
- 失败观测链可以区分连接、JOIN、服务器错误、心跳超时和 socket 系统调用错误；
- 4600 不是历史 4300～4600 区间中的确定失败点。

该结果不能证明：

- 4600 是稳定容量或最大在线人数；
- 服务器是唯一瓶颈；
- 相同数字可以迁移到公网或生产机器。

如果继续研究容量，应当：

1. 将服务器与 LoadGen 分机部署；
2. 固定代码、硬件和负载参数；
3. 多次重复相同实验；
4. 记录服务器内部任务等待和 Tick 处理时间；
5. 报告稳定区间而不是单次最大值。
