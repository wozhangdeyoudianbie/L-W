#include "protocol.h"
#include <arpa/inet.h>
#include <cstring>
#include <utility>
#include <limits>

namespace
{
    bool read_u16(const std::string &data, std::size_t offset, std::uint16_t &value)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint16_t))
        {
            return false;
        }
        std::uint16_t network_value = 0;
        std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
        value = ntohs(network_value);
        return true;
    }
    bool read_u32(const std::string &data, std::size_t offset, std::uint32_t &value)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint32_t))
        {
            return false;
        }
        std::uint32_t network_value = 0;
        std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
        value = ntohl(network_value);
        return true;
    }
    bool read_i32(const std::string &data, std::size_t offset, std::int32_t &value)
    {
        std::uint32_t raw_value = 0;
        if (!read_u32(data, offset, raw_value))
        {
            return false;
        }
        if (raw_value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        {
            value = static_cast<std::int32_t>(raw_value);
            return true;
        }
        const std::uint32_t magnitude = std::numeric_limits<std::uint32_t>::max() - raw_value;
        value = -1 - static_cast<std::int32_t>(magnitude);
        return true;
    }
    bool read_u64(const std::string &data, std::size_t offset, std::uint64_t &value)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint64_t))
        {
            return false;
        }
        std::uint32_t high = 0;
        std::uint32_t low = 0;
        if (!read_u32(data, offset, high) || !read_u32(data, offset + 4, low))
        {
            return false;
        }
        value = (static_cast<std::uint64_t>(high) << 32) | low;
        return true;
    }
    void append_u16(std::string &data, std::uint16_t value)
    {
        const std::uint16_t network_value = htons(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }
    void append_u32(std::string &data, std::uint32_t value)
    {
        const std::uint32_t network_value = htonl(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }
    void append_u64(std::string &data, std::uint64_t value)
    {
        const std::uint32_t high = static_cast<std::uint32_t>(value >> 32);
        const std::uint32_t low = static_cast<std::uint32_t>(value & 0xffffffffULL);
        append_u32(data, high);
        append_u32(data, low);
    }
    void append_i32(std::string &data, std::int32_t value)
    {
        const std::uint32_t network_value = htonl(static_cast<std::uint32_t>(value));
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }
}

// 解析加入请求
bool Protocol::decode_join_request(const std::string &payload, JoinRequest &request)
{
    std::uint32_t room_id = 0;
    if (!read_u32(payload, 0, room_id))
    {
        return false;
    }
    std::uint16_t name_size = 0;
    if (!read_u16(payload, 4, name_size))
    {
        return false;
    }
    const std::size_t offest = 6;
    if (payload.size() - offest != name_size)
    {
        return false;
    }
    JoinRequest temp_;
    temp_.room_id = room_id;
    temp_.player_name = payload.substr(offest, name_size);
    request = std::move(temp_);
    return true;
}

// 解析离开请求
bool Protocol::decode_leave_request(const std::string &payload)
{
    return payload.empty();
}

// 解析聊天请求
bool Protocol::decode_chat_request(const std::string &payload, ChatRequest &request)
{
    std::uint16_t message_size = 0;
    std::size_t offest = 0;
    if (!read_u16(payload, offest, message_size))
    {
        return false;
    }
    offest = 2;
    if (payload.size() - offest != message_size)
    {
        return false;
    }
    ChatRequest temp_;
    temp_.message = payload.substr(offest, message_size);
    request = std::move(temp_);
    return true;
}

// 解析移动请求
bool Protocol::decode_move_request(const std::string &payload, MoveRequest &request)
{
    if (payload.size() != 8)
    {
        return false;
    }
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    if (!read_i32(payload, 0, dx) || !read_i32(payload, 4, dy))
    {
        return false;
    }
    MoveRequest temp_;
    temp_.dx = dx;
    temp_.dy = dy;
    request = std::move(temp_);
    return true;
}

// 解析攻击请求
bool Protocol::decode_attack_request(const std::string &payload, AttackRequest &request)
{
    if (payload.size() != 8)
    {
        return false;
    }
    std::uint64_t target_player_id = 0;
    if (!read_u64(payload, 0, target_player_id))
    {
        return false;
    }
    AttackRequest temp_;
    temp_.target_player_id = target_player_id;
    request = std::move(temp_);
    return true;
}

// 编码加入成功响应
bool Protocol::encode_join_ok(std::uint32_t room_id, std::uint64_t self_player_id, const std::string &token, const std::vector<MemberInfo> &members, std::string &payload)
{
    payload.clear();
    if (token.empty() || token.size() > MAX_TOKEN_SIZE)
    {
        return false;
    }
    if (members.size() > std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    for (const auto &member : members)
    {
        if (member.player_name.empty() || member.player_name.size() > MAX_PLAYER_NAME_SIZE)
        {
            return false;
        }
    }
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, self_player_id);
    append_u16(temp_, static_cast<std::uint16_t>(token.size()));
    temp_.append(token);
    append_u16(temp_, static_cast<std::uint16_t>(members.size()));
    for (const auto &member : members)
    {
        append_u64(temp_, member.player_id);
        append_u16(temp_, static_cast<std::uint16_t>(member.player_name.size()));
        temp_.append(member.player_name);
    }
    payload = std::move(temp_);
    return true;
}

// 编码重连成功响应
bool Protocol::encode_resume_ok(std::uint32_t room_id, std::uint64_t self_player_id, Roomstatemachine::States room_state, std::uint64_t tick_id, const std::vector<MemberInfo> &members, const std::vector<PlayerGameState> &states, std::string &payload)
{
    payload.clear();
    if (members.size() > std::numeric_limits<std::uint16_t>::max() || states.size() > std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    for (const auto &member : members)
    {
        if (member.player_name.empty() || member.player_name.size() > MAX_PLAYER_NAME_SIZE)
        {
            return false;
        }
    }
    for (const auto &state : states)
    {
        if (state.player_id == 0)
        {
            return false;
        }
    }
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, self_player_id);
    append_u16(temp_, static_cast<std::uint16_t>(room_state));
    append_u64(temp_, tick_id);
    append_u16(temp_, static_cast<std::uint16_t>(members.size()));
    for (const auto &member : members)
    {
        append_u64(temp_, member.player_id);
        append_u16(temp_, static_cast<std::uint16_t>(member.player_name.size()));
        temp_.append(member.player_name);
    }
    append_u16(temp_, static_cast<std::uint16_t>(states.size()));
    for (const auto &state : states)
    {
        append_u64(temp_, state.player_id);
        append_i32(temp_, state.x);
        append_i32(temp_, state.y);
        append_i32(temp_, state.hp);
    }
    payload = std::move(temp_);
    return true;
}

// 编码新玩家加入事件
bool Protocol::encode_player_joined(std::uint32_t room_id, std::uint64_t player_id, const std::string &player_name, std::string &payload)
{
    payload.clear();
    if (player_name.empty() || player_name.size() > MAX_PLAYER_NAME_SIZE)
    {
        return false;
    }
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, player_id);
    append_u16(temp_, static_cast<std::uint16_t>(player_name.size()));
    temp_.append(player_name);
    payload = std::move(temp_);
    return true;
}

// 编码离开成功响应
bool Protocol::encode_leave_ok(std::uint32_t room_id, std::string &payload)
{
    payload.clear();
    std::string temp_;
    append_u32(temp_, room_id);
    payload = std::move(temp_);
    return true;
}

// 编码玩家离开事件
bool Protocol::encode_player_left(std::uint32_t room_id, std::uint64_t player_id, std::string &payload)
{
    payload.clear();
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, player_id);
    payload = std::move(temp_);
    return true;
}

// 编码聊天广播事件
bool Protocol::encode_chat_event(std::uint32_t room_id, std::uint64_t player_id, const std::string &message, std::string &payload)
{
    payload.clear();
    if (message.empty() || message.size() > MAX_CHAT_MESSAGE_SIZE)
    {
        return false;
    }
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, player_id);
    append_u16(temp_, static_cast<std::uint16_t>(message.size()));
    temp_.append(message);
    payload = std::move(temp_);
    return true;
}

// 编码错误响应
bool Protocol::encode_error(MessageType request_type, ErrorCode error_code, std::string &payload)
{
    payload.clear();
    switch (request_type)
    {
        case MessageType::join:
        case MessageType::leave:
        case MessageType::chat:
        case MessageType::move:
        case MessageType::attack:
        case MessageType::resume:
            break;
        default:
            return false;
    }
    switch (error_code)
    {
        case ErrorCode::room_not_found:
        case ErrorCode::room_full:
        case ErrorCode::already_in_room:
        case ErrorCode::not_in_room:
        case ErrorCode::invalid_player_name:
        case ErrorCode::invalid_message:
        case ErrorCode::player_id_exhausted:
        case ErrorCode::room_not_joinable:
        case ErrorCode::room_not_running:
        case ErrorCode::already_submitted:
        case ErrorCode::invalid_token:
        case ErrorCode::session_online:
        case ErrorCode::session_expired:
        case ErrorCode::resume_failed:
            break;
        default:
            return false;
    }
    std::string temp_;
    append_u16(temp_, static_cast<std::uint16_t>(request_type));
    append_u16(temp_, static_cast<std::uint16_t>(error_code));
    payload = std::move(temp_);
    return true;
}

// 解析心跳请求
bool Protocol::decode_heartbeat_request(const std::string &payload, HeartbeatRequest &request)
{
    if (payload.size() != 8)
    {
        return false;
    }
    std::uint64_t seq = 0;
    if (!read_u64(payload, 0, seq))
    {
        return false;
    }
    HeartbeatRequest temp_{};
    temp_.seq = seq;
    request = std::move(temp_);
    return true;
}

// 解析重连请求
bool Protocol::decode_resume_request(const std::string &payload, ResumeRequest &request)
{
    std::uint16_t token_size = 0;
    if (!read_u16(payload, 0, token_size))
    {
        return false;
    }
    const std::size_t offset = 2;
    if (token_size == 0 || token_size > MAX_TOKEN_SIZE || payload.size() - offset != token_size)
    {
        return false;
    }
    ResumeRequest temp_;
    temp_.token = payload.substr(offset, token_size);
    request = std::move(temp_);
    return true;
}


// 编码心跳确认响应
bool Protocol::encode_heartbeat_ack(std::uint64_t seq, std::string &payload)
{
    payload.clear();
    std::string temp_;
    append_u64(temp_, seq);
    payload = std::move(temp_);
    return true;
}

// 编码状态快照（房间+帧号+玩家状态）
bool Protocol::encode_state_snapshot(std::uint32_t room_id, std::uint64_t tick_id, const std::vector<PlayerGameState> &states, std::string &payload)
{
    payload.clear();
    if (states.size() > std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    for (const auto &state : states)
    {
        if (state.player_id == 0)
        {
            return false;
        }
    }
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, tick_id);
    append_u16(temp_, static_cast<std::uint16_t>(states.size()));
    for (const auto &state : states)
    {
        append_u64(temp_, state.player_id);
        append_i32(temp_, state.x);
        append_i32(temp_, state.y);
        append_i32(temp_, state.hp);
    }
    payload = std::move(temp_);
    return true;
}
