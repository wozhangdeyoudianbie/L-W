#ifndef ROOM_SERVICE_H
#define ROOM_SERVICE_H

#include "buffer.h"
#include "connection.h"
#include "event_loop.h"
#include "protocol.h"
#include "room_manager.h"
#include "session_manager.h"
#include "checkpoint.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class RoomService
{
public:
    RoomService(EventLoop *base_loop, std::chrono::milliseconds reconnect_timeout);   // 构造：保存 base 循环并创建会话管理器
    RoomService(const RoomService &) = delete;
    RoomService &operator=(const RoomService &) = delete;
    bool add_room(std::uint32_t room_id, std::size_t capacity);   // 创建房间（须在 base 线程）
    bool handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer);   // 消息入口：拆帧后分发
    void handle_connection_closed(const Connection::ConnectionPtr &connection);         // 连接断开：Session 和 Room 转为离线
    void handle_tick(std::uint64_t expirations);                                         // 定时结算房间
    void handle_session_timeouts(SessionManager::Clock::time_point now);                 // 清理过期离线会话并永久移除玩家
    bool make_checkpoint(std::uint64_t generation, ServerCheckpoint &checkpoint) const;                    // 生成全局检查点（须在 base 线程）
    bool restore_checkpoint(const ServerCheckpoint &checkpoint, SessionManager::Clock::time_point now);    // 恢复全局检查点（须在 base 线程、监听以前）
private:
    bool handle_decoded_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload);   // I/O 工作线程：校验类型并投递
    bool handle_heartbeat(const Connection::ConnectionPtr &connection, const std::string &payload);   // I/O 工作线程：处理心跳
    void handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload);           // 跨线程投递到 base 线程
    void handle_frame_in_loop(const Connection::ConnectionPtr &connection, std::uint16_t type, std::string payload);          // base 线程：业务分发
    void handle_join(const Connection::ConnectionPtr &connection, const std::string &payload);    // 新玩家加入并创建 Session
    void handle_resume(const Connection::ConnectionPtr &connection, const std::string &payload);  // 旧玩家凭 token 恢复
    void handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload);   // 主动永久离开
    void handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload);    // 处理聊天请求
    void handle_move(const Connection::ConnectionPtr &connection, const std::string &payload);    // 处理移动命令
    void handle_attack(const Connection::ConnectionPtr &connection, const std::string &payload);  // 处理攻击命令
    bool lookup_identity(const Connection::ConnectionPtr &connection, MessageType request_type, std::uint32_t &room_id, std::uint64_t &player_id);   // 从 Session 查询稳定身份
    bool send_frame(const Connection::ConnectionPtr &connection, MessageType type, const std::string &payload);   // 给单个连接发帧
    bool broadcast_frame(const std::vector<Connection::ConnectionPtr> &connections, MessageType type, const std::string &payload);   // 广播帧
    bool send_error(const Connection::ConnectionPtr &connection, MessageType request_type, ErrorCode error_code);   // 发送错误帧
    EventLoop *base_loop_;
    RoomManager room_manager_;
    SessionManager session_manager_;
};

#endif
