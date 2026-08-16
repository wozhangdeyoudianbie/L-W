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

    // Session 状态→重连协议错误码（无对应码返回 false）
    bool session_state_to_error_code(SessionManager::States state, ErrorCode &error_code)
    {
        switch (state)
        {
            case SessionManager::States::invalid_token:
                {
                    error_code = ErrorCode::invalid_token;
                    return true;
                }
            case SessionManager::States::session_online:
                {
                    error_code = ErrorCode::session_online;
                    return true;
                }
            case SessionManager::States::session_expired:
                {
                    error_code = ErrorCode::session_expired;
                    return true;
                }
            case SessionManager::States::success:
            case SessionManager::States::wrong_thread:
            case SessionManager::States::invalid_connection:
            case SessionManager::States::already_bound:
            case SessionManager::States::session_not_expired:
            case SessionManager::States::not_bound:
            case SessionManager::States::internal_error:
                {
                    return false;
                }
        }
        return false;
    }
}

// 构造：保存 base 循环并创建会话管理器
RoomService::RoomService(EventLoop *base_loop, std::chrono::milliseconds reconnect_timeout)
    :base_loop_(base_loop), session_manager_(reconnect_timeout)
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

// 连接断开（base 线程）：Session 与 Room 转为离线，不广播，不删除成员
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
        const SessionManager::LookupResult lookup_result = session_manager_.lookup(connection);
        if (lookup_result.state != SessionManager::States::success)
        {
            return;
        }
        const RoomManager::DetachResult detach_result = room_manager_.detach_connection(lookup_result.room_id, lookup_result.player_id, connection);
        if (detach_result.state != RoomManager::Bindingstates::success)
        {
            return;
        }
        session_manager_.detach(connection, SessionManager::Clock::now());
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
        case MessageType::resume:
            {
                handle_resume(connection, payload);
                return;
            }
        default:
            {
                return;
            }
    }
}

// 处理加入请求（base 线程）：多对象事务
void RoomService::handle_join(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    JoinRequest request;
    if (!Protocol::decode_join_request(payload, request))
    {
        send_error(connection, MessageType::join, ErrorCode::invalid_player_name);
        return;
    }
    const SessionManager::LookupResult lookup_result = session_manager_.lookup(connection);
    if (lookup_result.state == SessionManager::States::success)
    {
        send_error(connection, MessageType::join, ErrorCode::already_in_room);
        return;
    }
    if (lookup_result.state != SessionManager::States::not_bound)
    {
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
    const SessionManager::CreateResult session_result = session_manager_.create(connection, result.room_id, result.player_id);
    if (session_result.state != SessionManager::States::success)
    {
        room_manager_.leave(result.room_id, result.player_id);
        return;
    }
    std::string response_payload;
    std::string event_payload;
    if (!Protocol::encode_join_ok(result.room_id, result.player_id, session_result.token, result.members, response_payload) ||
        !Protocol::encode_player_joined(result.room_id, result.player_id, request.player_name, event_payload))
    {
        const RoomManager::LeaveResult rollback_result = room_manager_.leave(result.room_id, result.player_id);
        if (rollback_result.state == RoomManager::States::success)
        {
            session_manager_.erase_by_connection(connection);
        }
        return;
    }
    const RoomManager::States start_state = room_manager_.start_if_full(result.room_id);
    if (start_state != RoomManager::States::success)
    {
        const RoomManager::LeaveResult rollback_result = room_manager_.leave(result.room_id, result.player_id);
        if (rollback_result.state == RoomManager::States::success)
        {
            session_manager_.erase_by_connection(connection);
        }
        ErrorCode error_code = ErrorCode::invalid_player_name;
        if (state_to_error_code(start_state, error_code))
        {
            send_error(connection, MessageType::join, error_code);
        }
        return;
    }
    send_frame(connection, MessageType::join_ok, response_payload);
    broadcast_frame(result.notify_connections, MessageType::player_joined, event_payload);
}

// 处理重连请求（base 线程）：Session 与 Room 同时绑定新连接
void RoomService::handle_resume(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    ResumeRequest request;
    if (!Protocol::decode_resume_request(payload, request))
    {
        send_error(connection, MessageType::resume, ErrorCode::invalid_token);
        return;
    }
    const SessionManager::ResumeResult session_result = session_manager_.resume(connection, request.token, SessionManager::Clock::now());
    if (session_result.state != SessionManager::States::success)
    {
        ErrorCode error_code = ErrorCode::resume_failed;
        if (session_state_to_error_code(session_result.state, error_code))
        {
            send_error(connection, MessageType::resume, error_code);
        }
        return;
    }
    const RoomManager::BindingResult room_result = room_manager_.bind_connection(session_result.room_id, session_result.player_id, connection);
    if (room_result.state != RoomManager::Bindingstates::success)
    {
        session_manager_.rollback_resume(connection);
        send_error(connection, MessageType::resume, ErrorCode::resume_failed);
        return;
    }
    std::string response_payload;
    if (!Protocol::encode_resume_ok(room_result.room_id, room_result.player_id, room_result.room_state, room_result.tick_id, room_result.members, room_result.snapshot, response_payload))
    {
        room_manager_.detach_connection(room_result.room_id, room_result.player_id, connection);
        session_manager_.rollback_resume(connection);
        return;
    }
    send_frame(connection, MessageType::resume_ok, response_payload);
}

// 从 Session 查询稳定身份（仅 base 线程，用于普通业务请求）
bool RoomService::lookup_identity(const Connection::ConnectionPtr &connection, MessageType request_type, std::uint32_t &room_id, std::uint64_t &player_id)
{
    const SessionManager::LookupResult result = session_manager_.lookup(connection);
    if (result.state == SessionManager::States::success)
    {
        room_id = result.room_id;
        player_id = result.player_id;
        return true;
    }
    if (result.state == SessionManager::States::not_bound)
    {
        send_error(connection, request_type, ErrorCode::not_in_room);
    }
    return false;
}

// 处理离开请求（base 线程）：永久离开
void RoomService::handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    if (!Protocol::decode_leave_request(payload))
    {
        send_error(connection, MessageType::leave, ErrorCode::invalid_message);
        return;
    }
    std::uint32_t room_id = 0;
    std::uint64_t player_id = 0;
    if (!lookup_identity(connection, MessageType::leave, room_id, player_id))
    {
        return;
    }
    const RoomManager::LeaveResult result = room_manager_.leave(room_id, player_id);
    if (result.state != RoomManager::States::success)
    {
        ErrorCode error_code = ErrorCode::not_in_room;
        if (state_to_error_code(result.state, error_code))
        {
            send_error(connection, MessageType::leave, error_code);
        }
        return;
    }
    session_manager_.erase_by_connection(connection);
    std::string response_payload;
    if (!Protocol::encode_leave_ok(room_id, response_payload))
    {
        return;
    }
    send_frame(connection, MessageType::leave_ok, response_payload);
    std::string event_payload;
    if (!Protocol::encode_player_left(room_id, player_id, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::player_left, event_payload);
}

// 处理聊天请求（base 线程）
void RoomService::handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    ChatRequest request;
    if (!Protocol::decode_chat_request(payload, request))
    {
        send_error(connection, MessageType::chat, ErrorCode::invalid_message);
        return;
    }
    std::uint32_t room_id = 0;
    std::uint64_t player_id = 0;
    if (!lookup_identity(connection, MessageType::chat, room_id, player_id))
    {
        return;
    }
    const RoomManager::ChatResult result = room_manager_.chat(room_id, player_id, request.message);
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
    if (!Protocol::encode_chat_event(room_id, player_id, request.message, event_payload))
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

// 清理过期离线会话（base 线程）：永久移除超时玩家并广播
void RoomService::handle_session_timeouts(SessionManager::Clock::time_point now)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return;
    }
    const std::vector<SessionManager::ExpiredSession> expired = session_manager_.expired_sessions(now);
    for (const auto &item : expired)
    {
        const RoomManager::LeaveResult leave_result = room_manager_.leave(item.room_id, item.player_id);
        if (leave_result.state != RoomManager::States::success)
        {
            continue;
        }
        if (session_manager_.erase_expired(item.token, now) != SessionManager::States::success)
        {
            continue;
        }
        std::string event_payload;
        if (!Protocol::encode_player_left(item.room_id, item.player_id, event_payload))
        {
            continue;
        }
        broadcast_frame(leave_result.notify_connections, MessageType::player_left, event_payload);
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

// 处理移动命令（base 线程）
void RoomService::handle_move(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    MoveRequest request;
    if (!Protocol::decode_move_request(payload, request))
    {
        send_error(connection, MessageType::move, ErrorCode::invalid_message);
        return;
    }
    std::uint32_t room_id = 0;
    std::uint64_t player_id = 0;
    if (!lookup_identity(connection, MessageType::move, room_id, player_id))
    {
        return;
    }
    const RoomManager::CommandResult result = room_manager_.move(room_id, player_id, request.dx, request.dy);
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

// 处理攻击命令（base 线程）
void RoomService::handle_attack(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    AttackRequest request;
    if (!Protocol::decode_attack_request(payload, request))
    {
        send_error(connection, MessageType::attack, ErrorCode::invalid_message);
        return;
    }
    std::uint32_t room_id = 0;
    std::uint64_t player_id = 0;
    if (!lookup_identity(connection, MessageType::attack, room_id, player_id))
    {
        return;
    }
    const RoomManager::CommandResult result = room_manager_.attack(room_id, player_id, request.target_player_id);
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
        case MessageType::resume:
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
