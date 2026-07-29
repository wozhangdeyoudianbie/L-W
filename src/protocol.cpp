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
}

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

bool Protocol::decode_leave_request(const std::string &payload)
{
    return payload.empty();
}

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

bool Protocol::encode_join_ok(std::uint32_t room_id, std::uint64_t self_player_id, const std::vector<MemberInfo> &members, std::string &payload)
{
    payload.clear();
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

bool Protocol::encode_leave_ok(std::uint32_t room_id, std::string &payload)
{
    payload.clear();
    std::string temp_;
    append_u32(temp_, room_id);
    payload = std::move(temp_);
    return true;
}

bool Protocol::encode_player_left(std::uint32_t room_id, std::uint64_t player_id, std::string &payload)
{
    payload.clear();
    std::string temp_;
    append_u32(temp_, room_id);
    append_u64(temp_, player_id);
    payload = std::move(temp_);
    return true;
}

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

bool Protocol::encode_error(MessageType request_type, ErrorCode error_code, std::string &payload)
{
    payload.clear();
    switch (request_type)
    {
        case MessageType::join:
        case MessageType::leave:
        case MessageType::chat:
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

