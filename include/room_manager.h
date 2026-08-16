#ifndef ROOM_MANAGER_H
#define ROOM_MANAGER_H

#include "room.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class RoomManager
{
public:
    enum class States
    {
        success,
        invalid_connection,
        room_not_found,
        room_full,
        room_not_joinable,
        not_in_room,
        invalid_player_name,
        invalid_message,
        player_id_exhausted,
        room_not_running,
        already_submitted,
        internal_error
    };
    enum class Bindingstates
    {
        success,
        invalid_connection,
        room_not_found,
        player_not_found,
        already_bound,
        not_bound,
        connection_mismatch,
        internal_error
    };
    struct JoinResult
    {
        States state = States::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<MemberInfo> members;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    struct LeaveResult
    {
        States state = States::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    struct ChatResult
    {
        States state = States::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    struct CommandResult
    {
        States state = States::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct BindingResult
    {
        Bindingstates state = Bindingstates::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        Roomstatemachine::States room_state = Roomstatemachine::States::waiting;
        std::uint64_t tick_id = 0;
        std::vector<MemberInfo> members;
        std::vector<PlayerGameState> snapshot;
    };
    struct DetachResult
    {
        Bindingstates state = Bindingstates::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct TickResult
    {
        std::uint32_t room_id = 0;
        std::uint64_t tick_id = 0;
        std::size_t processed_commands = 0;
        std::size_t successful_commands = 0;
        std::vector<PlayerGameState> snapshot;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    bool add_room(std::uint32_t room_id, std::size_t capacity);                          // 创建房间
    std::size_t room_count() const;                                                      // 查询：房间总数
    bool contains_room(std::uint32_t room_id) const;                                     // 查询：房间是否存在
    JoinResult join(const Connection::ConnectionPtr &connection, std::uint32_t room_id, const std::string &player_name);   // 加入：创建房间成员，但不自动开局
    States start_if_full(std::uint32_t room_id);                                         // 开局：房间满员时从 waiting 转为 running
    BindingResult bind_connection(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection);   // 重连：恢复成员连接并返回恢复数据
    DetachResult detach_connection(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection); // 断线：仅解绑匹配连接
    LeaveResult leave(std::uint32_t room_id, std::uint64_t player_id);                    // 永久离开：删除成员和权威状态
    ChatResult chat(std::uint32_t room_id, std::uint64_t player_id, const std::string &message) const;   // 稳定身份：发言
    CommandResult move(std::uint32_t room_id, std::uint64_t player_id, std::int32_t dx, std::int32_t dy);   // 稳定身份：移动
    CommandResult attack(std::uint32_t room_id, std::uint64_t player_id, std::uint64_t target_player_id);   // 稳定身份：攻击
    std::vector<TickResult> tick_rooms();                                                // 推进所有运行中房间
private:
    std::unordered_map<std::uint32_t, std::unique_ptr<Room>> rooms_;
};

#endif
