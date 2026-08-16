#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "game_state.h"
#include "room_state_machine.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class MessageType : std::uint16_t
{
    join = 1,
    leave = 2,
    chat = 3,
    move = 4,
    attack = 5,
    heartbeat = 6,
    resume = 7,
    join_ok = 101,
    player_joined = 102,
    leave_ok = 103,
    player_left = 104,
    chat_event = 105,
    state_snapshot = 106,
    error = 107,
    heartbeat_ack = 108,
    resume_ok = 109
};

enum class ErrorCode : std::uint16_t
{
    room_not_found = 1,
    room_full = 2,
    already_in_room = 3,
    not_in_room = 4,
    invalid_player_name = 5,
    invalid_message = 6,
    player_id_exhausted = 7,
    room_not_joinable = 8,
    room_not_running = 9,
    already_submitted = 10,
    invalid_token = 11,
    session_online = 12,
    session_expired = 13,
    resume_failed = 14
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

struct MoveRequest
{
    std::int32_t dx;
    std::int32_t dy;
};

struct AttackRequest
{
    std::uint64_t target_player_id;
};

struct HeartbeatRequest
{
    std::uint64_t seq;
};

struct ResumeRequest
{
    std::string token;
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
    static constexpr std::size_t MAX_TOKEN_SIZE = 128;
    static bool decode_join_request(const std::string &payload, JoinRequest &request);    // 解析加入请求
    static bool decode_leave_request(const std::string &payload);                         // 解析离开请求（空负载为合法）
    static bool decode_chat_request(const std::string &payload, ChatRequest &request);    // 解析聊天请求
    static bool decode_move_request(const std::string &payload, MoveRequest &request);    // 解析移动请求
    static bool decode_attack_request(const std::string &payload, AttackRequest &request);// 解析攻击请求
    static bool decode_heartbeat_request(const std::string &payload, HeartbeatRequest &request);   // 解析心跳请求
    static bool decode_resume_request(const std::string &payload, ResumeRequest &request);         // 解析重连请求
    static bool encode_join_ok(std::uint32_t room_id, std::uint64_t self_player_id, const std::string &token, const std::vector<MemberInfo> &members, std::string &payload);   // 编码加入成功响应
    static bool encode_player_joined(std::uint32_t room_id, std::uint64_t player_id, const std::string &player_name, std::string &payload);   // 编码新玩家加入事件
    static bool encode_leave_ok(std::uint32_t room_id, std::string &payload);            // 编码离开成功响应
    static bool encode_player_left(std::uint32_t room_id, std::uint64_t player_id, std::string &payload);   // 编码玩家离开事件
    static bool encode_chat_event(std::uint32_t room_id, std::uint64_t player_id, const std::string &message, std::string &payload);   // 编码聊天广播事件
    static bool encode_state_snapshot(std::uint32_t room_id, std::uint64_t tick_id, const std::vector<PlayerGameState> &states, std::string &payload);   // 编码状态快照
    static bool encode_resume_ok(std::uint32_t room_id, std::uint64_t self_player_id, Roomstatemachine::States room_state, std::uint64_t tick_id, const std::vector<MemberInfo> &members, const std::vector<PlayerGameState> &states, std::string &payload);   // 编码重连恢复响应
    static bool encode_error(MessageType request_type, ErrorCode error_code, std::string &payload);   // 编码错误响应
    static bool encode_heartbeat_ack(std::uint64_t seq, std::string &payload);            // 编码心跳确认响应
};

#endif
