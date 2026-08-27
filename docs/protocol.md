# 二进制协议

## 1. 帧格式

所有整数使用网络字节序。

每个 TCP 应用层帧由长度、消息类型和负载组成：

```text
+----------------------+----------------------+-------------------+
| frame_length (4B)    | message_type (2B)    | payload (N B)     |
+----------------------+----------------------+-------------------+
```

- `frame_length`：`message_type + payload` 的总长度，不包含自身 4 字节；
- 最小帧长度：2 字节；
- 最大帧长度：64 KiB；
- `message_type`：无符号 16 位整数；
- `payload`：按具体消息定义编码，可以包含二进制数据。

`Codec::decode()` 只在 Buffer 中存在完整帧时调用业务回调。

半包会继续保留在 Buffer 中，粘包和多个完整帧会按顺序继续解析。

## 2. 消息类型

### 客户端请求

| 名称 | 值 | 负载 |
| --- | ---: | --- |
| `join` | 1 | room_id、player_name |
| `leave` | 2 | 空 |
| `chat` | 3 | message |
| `move` | 4 | dx、dy |
| `attack` | 5 | target_player_id |
| `heartbeat` | 6 | seq |
| `resume` | 7 | token |

### 服务器响应与事件

| 名称 | 值 | 语义 |
| --- | ---: | --- |
| `join_ok` | 101 | 加入成功、玩家身份、token 和成员列表 |
| `player_joined` | 102 | 房间成员加入事件 |
| `leave_ok` | 103 | 主动离开成功 |
| `player_left` | 104 | 房间成员离开事件 |
| `chat_event` | 105 | 聊天广播 |
| `state_snapshot` | 106 | Tick 完成后的权威状态快照 |
| `error` | 107 | 请求类型和业务错误码 |
| `heartbeat_ack` | 108 | 心跳序号确认 |
| `resume_ok` | 109 | 重连成功后的完整恢复状态 |

## 3. 请求负载

| 请求 | 二进制布局 |
| --- | --- |
| JOIN | `u32 room_id` + `u16 name_size` + `name_size` 字节名称 |
| LEAVE | 空负载 |
| CHAT | `u16 message_size` + `message_size` 字节消息 |
| MOVE | `i32 dx` + `i32 dy` |
| ATTACK | `u64 target_player_id` |
| HEARTBEAT | `u64 seq` |
| RESUME | `u16 token_size` + `token_size` 字节 token |

输入限制：

- 玩家名长度：1～32 字节；
- 聊天消息长度：1～1024 字节；
- token 长度：1～128 字节；
- MOVE 和 ATTACK 必须使用精确固定长度；
- LEAVE 只接受空负载；
- 长度字段必须与剩余负载完全一致。

## 4. 关键响应

- `join_ok`：room_id、self_player_id、重连 token 和当前成员列表；
- `state_snapshot`：room_id、tick_id、玩家数量，以及每名玩家的 player_id、x、y、hp；
- `resume_ok`：room_id、self_player_id、房间状态、tick_id、成员列表和当前游戏状态；
- `error`：原请求 MessageType 与 ErrorCode；
- `heartbeat_ack`：回显客户端发送的 seq。

## 5. 错误码

| 名称 | 值 | 含义 |
| --- | ---: | --- |
| `room_not_found` | 1 | 房间不存在 |
| `room_full` | 2 | 房间已满 |
| `already_in_room` | 3 | 当前连接已经加入房间 |
| `not_in_room` | 4 | 当前玩家不在房间 |
| `invalid_player_name` | 5 | 玩家名不合法 |
| `invalid_message` | 6 | 消息结构或内容不合法 |
| `player_id_exhausted` | 7 | 玩家 ID 无法继续分配 |
| `room_not_joinable` | 8 | 当前房间状态不允许加入 |
| `room_not_running` | 9 | 房间尚未运行 |
| `already_submitted` | 10 | 玩家本 Tick 已经提交过命令 |
| `invalid_token` | 11 | 重连 token 无效 |
| `session_online` | 12 | 对应 Session 已在线 |
| `session_expired` | 13 | 重连窗口已经结束 |
| `resume_failed` | 14 | 重连恢复失败 |

## 6. 协议错误与业务错误

结构错误与业务错误必须区分：

### 协议结构错误

包括：

- 帧长度非法；
- 固定字段缺失；
- 声明长度与实际长度不一致；
- 固定长度请求带有多余字节；
- 消息类型无法解析。

结构错误后不能继续相信字节边界，应停止解析并关闭连接。

### 业务错误

包括：

- 房间不存在；
- 房间已满；
- 房间状态不允许操作；
- 玩家不在房间；
- 本 Tick 已经提交命令；
- 重连 token 失效。

业务错误使用合法的 `error` 帧返回，连接仍然可以继续使用。

`already_submitted` 保护“一名玩家每个逻辑 Tick 最多一条命令”的权威状态约束，不能为了提高压测数字而删除。
