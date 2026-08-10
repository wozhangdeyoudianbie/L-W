#include "room_service.h"
#include "codec.h"
#include "event_loop.h"
#include <utility>

namespace
{
    // 状态码→协议错误码（无对应码返回 false）
    bool state_to_error_code(RoomManager::States state, ErrorCode &error_code)
    {
        switch (state)
        {
            case RoomManager::States::room_not_found:
                {
                    error_code = ErrorCode::room_not_found;
                    return true;
                }
            case RoomManager::States::room_full:
                {
                    error_code = ErrorCode::room_full;
                    return true;
                }
            case RoomManager::States::room_not_joinable:
                {
                    error_code = ErrorCode::room_not_joinable;
                    return true;
                }
            case RoomManager::States::already_in_room:
                {
                    error_code = ErrorCode::already_in_room;
                    return true;
                }
            case RoomManager::States::not_in_room:
                {
                    error_code = ErrorCode::not_in_room;
                    return true;
                }
            case RoomManager::States::invalid_player_name:
                {
                    error_code = ErrorCode::invalid_player_name;
                    return true;
                }
            case RoomManager::States::invalid_message:
                {
                    error_code = ErrorCode::invalid_message;
                    return true;
                }
            case RoomManager::States::player_id_exhausted:
                {
                    error_code = ErrorCode::player_id_exhausted;
                    return true;
                }
            case RoomManager::States::room_not_running:
                {
                    error_code = ErrorCode::room_not_running;
                    return true;
                }
            case RoomManager::States::already_submitted:
                {
                    error_code = ErrorCode::already_submitted;
                    return true;
                }
            case RoomManager::States::success:
            case RoomManager::States::invalid_connection:
            case RoomManager::States::internal_error:
                {
                    return false;
                }
        }
        return false;
    }
}

RoomService::RoomService(EventLoop *base_loop)
    :base_loop_(base_loop)
{
}

// 创建房间（须在 base 线程）
bool RoomService::add_room(std::uint32_t room_id, std::size_t capacity)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return false;
    }
    return room_manager_.add_room(room_id, capacity);
}

// 消息入口：拆帧后分发
bool RoomService::handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer)
{
    if (!connection || !connection->loop())
    {
        return false;
    }
    if (!connection->loop()->is_in_loop_thread())
    {
        return false;
    }
    return Codec::decode(buffer, [this, connection](std::uint16_t type, const std::string &payload)
    {
        return handle_decoded_frame(connection, type, payload);
    });
}

// 连接断开清理
void RoomService::handle_connection_closed(const Connection::ConnectionPtr &connection)
{
    if (!base_loop_ || !connection)
    {
        return;
    }
    base_loop_->run_in_loop([this, connection]()
    {
        if (!base_loop_->is_in_loop_thread())
        {
            return;
        }
        RoomManager::LeaveResult result = room_manager_.disconnect(connection);
        if (result.state != RoomManager::States::success || result.player_id == 0)
        {
            return;
        }
        std::string payload;
        if (!Protocol::encode_player_left(result.room_id, result.player_id, payload))
        {
            return;
        }
        broadcast_frame(result.notify_connections, MessageType::player_left, payload);
    });
}

// 帧分发（跨线程投递到 base 线程）
void RoomService::handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload)
{
    if (!base_loop_ || !connection)
    {
        return;
    }
    base_loop_->run_in_loop([this, connection, type, payload]() mutable
    {
        handle_frame_in_loop(connection, type, std::move(payload));
    });
}

// 帧分发（在 base 线程执行）
void RoomService::handle_frame_in_loop(const Connection::ConnectionPtr &connection, std::uint16_t type, std::string payload)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread() || !connection)
    {
        return;
    }
    const MessageType message_type = static_cast<MessageType>(type);
    switch (message_type)
    {
        case MessageType::join:
            {
                handle_join(connection, payload);
                return;
            }
        case MessageType::leave:
            {
                handle_leave(connection, payload);
                return;
            }
        case MessageType::chat:
            {
                handle_chat(connection, payload);
                return;
            }
        case MessageType::move:
            {
                handle_move(connection, payload);
                return;
            }
        case MessageType::attack:
            {
                handle_attack(connection, payload);
                return;
            }
        default:
            {
                return;
            }
    }
}

// 处理加入请求
void RoomService::handle_join(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    JoinRequest request;
    if (!Protocol::decode_join_request(payload, request))
    {
        send_error(connection, MessageType::join, ErrorCode::invalid_player_name);
        return;
    }
    RoomManager::JoinResult result = room_manager_.join(connection, request.room_id, request.player_name);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::invalid_player_name;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::join, error_code);
        }
        return;
    }
    std::string response_payload;
    if (!Protocol::encode_join_ok(result.room_id, result.player_id, result.members, response_payload))
    {
        return;
    }
    send_frame(connection, MessageType::join_ok, response_payload);
    std::string event_payload;
    if (!Protocol::encode_player_joined(result.room_id, result.player_id, request.player_name, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::player_joined, event_payload);
}

// 处理离开请求
void RoomService::handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    if (!Protocol::decode_leave_request(payload))
    {
        send_error(connection, MessageType::leave, ErrorCode::invalid_message);
        return;
    }
    RoomManager::LeaveResult result = room_manager_.leave(connection);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::not_in_room;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::leave, error_code);
        }
        return;
    }
    std::string response_payload;
    if (!Protocol::encode_leave_ok(result.room_id, response_payload))
    {
        return;
    }
    send_frame(connection, MessageType::leave_ok, response_payload);
    std::string event_payload;
    if (!Protocol::encode_player_left(result.room_id, result.player_id, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::player_left, event_payload);
}

// 处理聊天请求
void RoomService::handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    ChatRequest request;
    if (!Protocol::decode_chat_request(payload, request))
    {
        send_error(connection, MessageType::chat, ErrorCode::invalid_message);
        return;
    }
    RoomManager::ChatResult result = room_manager_.chat(connection, request.message);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::invalid_message;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::chat, error_code);
        }
        return;
    }
    std::string event_payload;
    if (!Protocol::encode_chat_event(result.room_id, result.player_id, request.message, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::chat_event, event_payload);
}

// 给单个连接发帧
bool RoomService::send_frame(const Connection::ConnectionPtr &connection, MessageType type, const std::string &payload)
{
    if (!connection)
    {
        return false;
    }
    std::string frame;
    if (!Codec::encode(static_cast<std::uint16_t>(type), payload, frame))
    {
        return false;
    }
    if (!connection->send(std::move(frame)))
    {
        connection->request_close();
        return false;
    }
    return true;
}

// 给一组连接广播帧（只编码一次）
bool RoomService::broadcast_frame(const std::vector<Connection::ConnectionPtr> &connections, MessageType type, const std::string &payload)
{
    std::string frame;
    if (!Codec::encode(static_cast<std::uint16_t>(type), payload, frame))
    {
        return false;
    }
    const bool flag = (type == MessageType::state_snapshot);
    bool ans = true;
    for (const auto &connection : connections)
    {
        if (!connection)
        {
            continue;
        }
        if (flag)
        {
            if (connection->under_backpressure())
            {
                continue;
            }
            if (!connection->send(frame))
            {
                continue;
            }
        }
        else
        {
            if (!connection->send(frame))
            {
                connection->request_close();
                ans = false;
            }
        }
    }
    return ans;
}

// 定时结算：推进所有运行中房间并广播快照
void RoomService::handle_tick(std::uint64_t expirations)
{
    if (!base_loop_)
    {
        return;
    }
    if (!base_loop_->is_in_loop_thread())
    {
        return;
    }
    if (expirations == 0)
    {
        return;
    }
    const std::vector<RoomManager::TickResult> results = room_manager_.tick_rooms();
    for (const auto &result : results)
    {
        if (result.notify_connections.empty())
        {
            continue;
        }
        std::string payload;
        if (!Protocol::encode_state_snapshot(result.room_id, result.tick_id, result.snapshot, payload))
        {
            continue;
        }
        broadcast_frame(result.notify_connections, MessageType::state_snapshot, payload);
    }
}

// 发送错误帧
bool RoomService::send_error(const Connection::ConnectionPtr &connection, MessageType request_type, ErrorCode error_code)
{
    std::string payload;
    if (!Protocol::encode_error(request_type, error_code, payload))
    {
        return false;
    }
    return send_frame(connection, MessageType::error, payload);
}

// 处理移动命令
void RoomService::handle_move(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    MoveRequest request;
    if (!Protocol::decode_move_request(payload, request))
    {
        send_error(connection, MessageType::move, ErrorCode::invalid_message);
        return;
    }
    RoomManager::CommandResult result = room_manager_.move(connection, request.dx, request.dy);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::invalid_message;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::move, error_code);
        }
        return;
    }
}

// 处理攻击命令
void RoomService::handle_attack(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    AttackRequest request;
    if (!Protocol::decode_attack_request(payload, request))
    {
        send_error(connection, MessageType::attack, ErrorCode::invalid_message);
        return;
    }
    RoomManager::CommandResult result = room_manager_.attack(connection, request.target_player_id);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::invalid_message;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::attack, error_code);
        }
        return;
    }
}

// 拆帧回调（I/O 工作线程） 类型合法则投递到 base 线程 非法类型中止拆帧并断开
bool RoomService::handle_decoded_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload)
{
    if (!connection)
    {
        return false;
    }
    const MessageType message_type = static_cast<MessageType>(type);
    switch (message_type)
    {
        case MessageType::heartbeat:
            {
                return handle_heartbeat(connection, payload);
            }
        case MessageType::join:
        case MessageType::leave:
        case MessageType::chat:
        case MessageType::move:
        case MessageType::attack:
            {
                connection->refresh_peer_activity();
                handle_frame(connection, type, payload);
                return true;
            }
        default:
            {
                return false;
            }
    }
}

// 处理心跳请求（I/O 工作线程） 原样回 ack
bool RoomService::handle_heartbeat(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    if (!connection)
    {
        return false;
    }
    HeartbeatRequest request{};
    if (!Protocol::decode_heartbeat_request(payload, request))
    {
        return false;
    }
    connection->refresh_peer_activity();
    std::string temp_;
    if (!Protocol::encode_heartbeat_ack(request.seq, temp_))
    {
        return false;
    }
    return send_frame(connection, MessageType::heartbeat_ack, temp_);
}
