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
        already_in_room,
        not_in_room,
        invalid_player_name,
        invalid_message,
        player_id_exhausted,
        room_not_running,
        already_submitted,
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
    bool contains_connection(const Connection::ConnectionPtr &connection) const;         // 查询：连接是否已在某房间
    JoinResult join(const Connection::ConnectionPtr &connection, std::uint32_t room_id, const std::string &player_name);   // 加入：校验后登记成员关系
    LeaveResult leave(const Connection::ConnectionPtr &connection);                      // 离开：清除成员关系
    ChatResult chat(const Connection::ConnectionPtr &connection, const std::string &message) const;   // 发言：校验消息并返回同房其他连接
    CommandResult move(const Connection::ConnectionPtr &connection, std::int32_t dx, std::int32_t dy);  // 提交移动命令（仅运行中）
    CommandResult attack(const Connection::ConnectionPtr &connection, std::uint64_t target_player_id);  // 提交攻击命令（仅运行中）
    std::vector<TickResult> tick_rooms();                                                // 结算：推进所有运行中房间
    LeaveResult disconnect(const Connection::ConnectionPtr &connection);                 // 断线清理：等价 leave，未在房视为成功
private:
    struct Membership
    {
        std::uint32_t room_id;
        std::uint64_t player_id;
    };
    std::unordered_map<std::uint32_t, std::unique_ptr<Room>> rooms_;
    std::unordered_map<Connection *, Membership> memberships_;
};

#endif
