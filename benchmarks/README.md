# 压测工具

这个目录只放压测、容量准备和手工压力场景，不放普通单元测试。能够独立运行并自动判断通过或失败的功能测试继续放在 `tests/`。

目前包含：

- `load_checkpoint_generator.cpp`：生成包含大量房间的检查点，用于准备恢复和启动压测数据。
- `slow_echo_client.cpp`：发送大块数据后暂停读取，用于观察服务端背压和慢客户端处理。
- `load_gen.h`：LoadGen压测器的完整接口、客户端状态与结果统计定义。

构建命令保持不变：

```bash
make load-checkpoint-generator
make slow-echo-client
```

生成检查点：

```bash
./build/load_checkpoint_generator <检查点路径> <房间数量> <每个房间容量>
```

运行慢读场景前需要先启动服务器，然后执行：

```bash
./build/slow_echo_client
```

压测产生的原始结果统一放在 `benchmarks/results/`，该目录内容默认不提交到 Git。
