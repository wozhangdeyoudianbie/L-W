#ifndef ROOM_H
#define ROOM_H

#include "connection.h"
#include "protocol.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Room
{
public:
    enum class JoinStatus
    {
        success,
        invalid_connection,
        invalid_player_name,
        room_full,
        player_id_exhausted
    };
    struct JoinResult
    {
        JoinStatus status;
        std::uint64_t player_id;
        std::vector<MemberInfo> members;
    };
    Room(std::uint32_t room_id, std::size_t capacity);
    std::uint32_t id() const;
    std::size_t capacity() const;
    std::size_t member_count() const;
    bool contains(std::uint64_t player_id) const;
    JoinResult join(const Connection::ConnectionPtr &connection, const std::string &player_name);
    bool leave(std::uint64_t player_id);
    std::vector<Connection::ConnectionPtr> connections(std::uint64_t excluded_player_id = 0) const;
private:
    struct Member
    {
        std::uint64_t player_id;
        std::string player_name;
        std::weak_ptr<Connection> connection;
    };
    std::uint32_t room_id_;
    std::size_t capacity_;
    std::uint64_t next_player_id_;
    std::unordered_map<std::uint64_t, Member> members_;
};

#endif
