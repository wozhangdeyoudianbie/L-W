#include "room_service.h"
#include "codec.h"
#include "event_loop.h"
#include <utility>

namespace
{
    bool status_to_error_code(RoomManager::Status status, ErrorCode &error_code)
    {
        switch (status)
        {
            case RoomManager::Status::room_not_found:
                {
                    error_code = ErrorCode::room_not_found;
                    return true;
                }
            case RoomManager::Status::room_full:
                {
                    error_code = ErrorCode::room_full;
                    return true;
                }
            case RoomManager::Status::room_not_joinable:
                {
                    error_code = ErrorCode::room_not_joinable;
                    return true;
                }
            case RoomManager::Status::already_in_room:
                {
                    error_code = ErrorCode::already_in_room;
                    return true;
                }
            case RoomManager::Status::not_in_room:
                {
                    error_code = ErrorCode::not_in_room;
                    return true;
                }
            case RoomManager::Status::invalid_player_name:
                {
                    error_code = ErrorCode::invalid_player_name;
                    return true;
                }
            case RoomManager::Status::invalid_message:
                {
                    error_code = ErrorCode::invalid_message;
                    return true;
                }
            case RoomManager::Status::player_id_exhausted:
                {
                    error_code = ErrorCode::player_id_exhausted;
                    return true;
                }
            case RoomManager::Status::success:
            case RoomManager::Status::invalid_connection:
            case RoomManager::Status::internal_error:
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

bool RoomService::add_room(std::uint32_t room_id, std::size_t capacity)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return false;
    }
    return room_manager_.add_room(room_id, capacity);
}

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
        handle_frame(connection, type, payload);
        return true;
    });
}

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
        if (result.status != RoomManager::Status::success || result.player_id == 0)
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
        default:
            {
                return;
            }
    }
}

void RoomService::handle_join(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    JoinRequest request;
    if (!Protocol::decode_join_request(payload, request))
    {
        send_error(connection, MessageType::join, ErrorCode::invalid_player_name);
        return;
    }
    RoomManager::JoinResult result = room_manager_.join(connection, request.room_id, request.player_name);
    if (result.status != RoomManager::Status::success)
    {
        ErrorCode error_code = ErrorCode::invalid_player_name;
        if (status_to_error_code(result.status, error_code))
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
    if (!send_frame(connection, MessageType::join_ok, response_payload))
    {
        return;
    }
    std::string event_payload;
    if (!Protocol::encode_player_joined(result.room_id, result.player_id, request.player_name, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::player_joined, event_payload);
}

void RoomService::handle_leave(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    if (!Protocol::decode_leave_request(payload))
    {
        send_error(connection, MessageType::leave, ErrorCode::invalid_message);
        return;
    }
    RoomManager::LeaveResult result = room_manager_.leave(connection);
    if (result.status != RoomManager::Status::success)
    {
        ErrorCode error_code = ErrorCode::not_in_room;
        if (status_to_error_code(result.status, error_code))
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
    if (!send_frame(connection, MessageType::leave_ok, response_payload))
    {
        return;
    }
    std::string event_payload;
    if (!Protocol::encode_player_left(result.room_id, result.player_id, event_payload))
    {
        return;
    }
    broadcast_frame(result.notify_connections, MessageType::player_left, event_payload);
}

void RoomService::handle_chat(const Connection::ConnectionPtr &connection, const std::string &payload)
{
    ChatRequest request;
    if (!Protocol::decode_chat_request(payload, request))
    {
        send_error(connection, MessageType::chat, ErrorCode::invalid_message);
        return;
    }
    RoomManager::ChatResult result = room_manager_.chat(connection, request.message);
    if (result.status != RoomManager::Status::success)
    {
        ErrorCode error_code = ErrorCode::invalid_message;
        if (status_to_error_code(result.status, error_code))
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
    connection->send(std::move(frame));
    return true;
}

bool RoomService::broadcast_frame(const std::vector<Connection::ConnectionPtr> &connections, MessageType type, const std::string &payload)
{
    std::string frame;
    if (!Codec::encode(static_cast<std::uint16_t>(type), payload, frame))
    {
        return false;
    }
    for (const auto &connection : connections)
    {
        if (connection)
        {
            connection->send(frame);
        }
    }
    return true;
}

bool RoomService::send_error(const Connection::ConnectionPtr &connection, MessageType request_type, ErrorCode error_code)
{
    std::string payload;
    if (!Protocol::encode_error(request_type, error_code, payload))
    {
        return false;
    }
    return send_frame(connection, MessageType::error, payload);
}
