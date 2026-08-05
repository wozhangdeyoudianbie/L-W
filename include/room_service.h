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
    RoomService(EventLoop *base_loop);
    RoomService(const RoomService &) = delete;
    RoomService &operator=(const RoomService &) = delete;
    bool add_room(std::uint32_t room_id, std::size_t capacity);
    bool handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer);
    void handle_connection_closed(const Connection::ConnectionPtr &connection);
    void handle_tick(std::uint64_t expirations);   // 定时触发：结算所有运行中房间并广播快照
private:
    void handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload);
    void handle_frame_in_loop(const Connection::ConnectionPtr &connection, std::uint16_t type, std::string payload);
    void handle_join(const Connection::ConnectionPtr &connection, const std::string &payload);
    void handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload);
    void handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload);
    void handle_move(const Connection::ConnectionPtr &connection, const std::string &payload);    // 处理移动命令
    void handle_attack(const Connection::ConnectionPtr &connection, const std::string &payload);  // 处理攻击命令
    bool send_frame(const Connection::ConnectionPtr &connection, MessageType type, const std::string &payload);
    bool broadcast_frame(const std::vector<Connection::ConnectionPtr> &connections, MessageType type, const std::string &payload);
    bool send_error(const Connection::ConnectionPtr &connection, MessageType request_type, ErrorCode error_code);
    EventLoop *base_loop_;
    RoomManager room_manager_;
};

#endif
