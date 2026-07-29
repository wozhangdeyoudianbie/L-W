#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class MessageType : std::uint16_t
{
    join = 1,
    leave = 2,
    chat = 3,
    join_ok = 101,
    player_joined = 102,
    leave_ok = 103,
    player_left = 104,
    chat_event = 105,
    error = 107
};

enum class ErrorCode : std::uint16_t
{
    room_not_found = 1,
    room_full = 2,
    already_in_room = 3,
    not_in_room = 4,
    invalid_player_name = 5,
    invalid_message = 6,
    player_id_exhausted = 7
};

struct JoinRequest
{
    std::uint32_t room_id;
    std::string player_name;
};

struct ChatRequest
{
    std::string message;
};

struct MemberInfo
{
    std::uint64_t player_id;
    std::string player_name;
};

class Protocol
{
public:
    static constexpr std::size_t MAX_PLAYER_NAME_SIZE = 32;
    static constexpr std::size_t MAX_CHAT_MESSAGE_SIZE = 1024;
    static bool decode_join_request(const std::string &payload, JoinRequest &request);
    static bool decode_leave_request(const std::string &payload);
    static bool decode_chat_request(const std::string &payload, ChatRequest &request);
    static bool encode_join_ok(std::uint32_t room_id, std::uint64_t self_player_id, const std::vector<MemberInfo> &members, std::string &payload);
    static bool encode_player_joined(std::uint32_t room_id, std::uint64_t player_id, const std::string &player_name, std::string &payload);
    static bool encode_leave_ok(std::uint32_t room_id, std::string &payload);
    static bool encode_player_left(std::uint32_t room_id, std::uint64_t player_id, std::string &payload);
    static bool encode_chat_event(std::uint32_t room_id, std::uint64_t player_id, const std::string &message, std::string &payload);
    static bool encode_error(MessageType request_type, ErrorCode error_code, std::string &payload);
};

#endif
