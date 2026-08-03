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
    enum class Status
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
        internal_error
    };
    struct JoinResult
    {
        Status status = Status::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<MemberInfo> members;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    struct LeaveResult
    {
        Status status = Status::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    struct ChatResult
    {
        Status status = Status::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::vector<Connection::ConnectionPtr> notify_connections;
    };
    bool add_room(std::uint32_t room_id, std::size_t capacity);
    std::size_t room_count() const;
    bool contains_room(std::uint32_t room_id) const;
    bool contains_connection(const Connection::ConnectionPtr &connection) const;
    JoinResult join(const Connection::ConnectionPtr &connection, std::uint32_t room_id, const std::string &player_name);
    LeaveResult leave(const Connection::ConnectionPtr &connection);
    ChatResult chat(const Connection::ConnectionPtr &connection, const std::string &message) const;
    LeaveResult disconnect(const Connection::ConnectionPtr &connection);
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
