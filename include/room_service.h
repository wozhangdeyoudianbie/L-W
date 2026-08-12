#ifndef ROOM_SERVICE_H
#define ROOM_SERVICE_H

#include "buffer.h"
#include "connection.h"
#include "event_loop.h"
#include "protocol.h"
#include "room_manager.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class RoomService
{
public:
    RoomService(EventLoop *base_loop);                     // 构造：保存 base 循环
    RoomService(const RoomService &) = delete;
    RoomService &operator=(const RoomService &) = delete;
    bool add_room(std::uint32_t room_id, std::size_t capacity);   // 创建房间（须在 base 线程）
    bool handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer);   // 消息入口：拆帧后分发
    void handle_connection_closed(const Connection::ConnectionPtr &connection);         // 连接断开清理
    void handle_tick(std::uint64_t expirations);   // 定时触发：结算所有运行中房间并广播快照
private:
    bool handle_decoded_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload);   // 拆帧回调（I/O 工作线程）：类型合法则投递 base 线程
    bool handle_heartbeat(const Connection::ConnectionPtr &connection, const std::string &payload);   // 处理心跳请求：原样回 ack
    void handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload);           // 帧分发（跨线程投递到 base 线程）
    void handle_frame_in_loop(const Connection::ConnectionPtr &connection, std::uint16_t type, std::string payload);          // 帧分发（在 base 线程执行）
    void handle_join(const Connection::ConnectionPtr &connection, const std::string &payload);   // 处理加入请求
    void handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload);  // 处理离开请求
    void handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload);   // 处理聊天请求
    void handle_move(const Connection::ConnectionPtr &connection, const std::string &payload);    // 处理移动命令
    void handle_attack(const Connection::ConnectionPtr &connection, const std::string &payload);  // 处理攻击命令
    bool send_frame(const Connection::ConnectionPtr &connection, MessageType type, const std::string &payload);                // 给单个连接发帧
    bool broadcast_frame(const std::vector<Connection::ConnectionPtr> &connections, MessageType type, const std::string &payload);   // 给一组连接广播帧（只编码一次）
    bool send_error(const Connection::ConnectionPtr &connection, MessageType request_type, ErrorCode error_code);              // 发送错误帧
    EventLoop *base_loop_;
    RoomManager room_manager_;
};

#endif
