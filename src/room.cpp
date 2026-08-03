#include "room.h"

Room::Room(std::uint32_t room_id, std::size_t capacity)
    :room_id_(room_id), capacity_(capacity), next_player_id_(1)
{
}

std::uint32_t Room::id() const
{
    return room_id_;
}

std::size_t Room::capacity() const
{
    return capacity_;
}

std::size_t Room::member_count() const
{
    return members_.size();
}

Roomstatemachine::States Room::state() const
{
    return state_machine_.state();
}

Roomstatemachine::Transitionstates Room::start(bool ready_to_start)
{
    return state_machine_.start(ready_to_start);
}

Roomstatemachine::Transitionstates Room::finish(bool should_finish)
{
    return state_machine_.finish(should_finish);
}

bool Room::contains(std::uint64_t player_id) const
{
    if (members_.find(player_id) == members_.end())
    {
        return false;
    }
    return true;
}

Room::JoinResult Room::join(const Connection::ConnectionPtr &connection, const std::string &player_name)
{
    JoinResult temp_;
    if (!connection)
    {
        temp_.status = JoinStatus::invalid_connection;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (player_name.empty() || player_name.size() > Protocol::MAX_PLAYER_NAME_SIZE)
    {
        temp_.status = JoinStatus::invalid_player_name;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (!state_machine_.can_join())
    {
        temp_.status = JoinStatus::invalid_state;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (members_.size() >= capacity_)
    {
        temp_.status = JoinStatus::room_full;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (next_player_id_ == 0)
    {
        temp_.status = JoinStatus::player_id_exhausted;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    temp_.members.reserve(members_.size());
    for (auto &user : members_)
    {
        Member &member = user.second;
        temp_.members.push_back({member.player_id, member.player_name});
    }
    std::uint64_t player_id = next_player_id_;
    members_.emplace(player_id, Member{player_id, player_name, connection});
    ++next_player_id_;
    temp_.status = JoinStatus::success;
    temp_.player_id = player_id;
    return temp_;
}

bool Room::leave(std::uint64_t player_id)
{
    auto it = members_.find(player_id);
    if (it == members_.end())
    {
        return false;
    }
    members_.erase(it);
    return true;
}

std::vector<Connection::ConnectionPtr> Room::connections(std::uint64_t excluded_player_id) const
{
    std::vector<Connection::ConnectionPtr> conns;
    conns.reserve(members_.size());
    for (auto &it : members_)
    {
        if (it.first == excluded_player_id)
        {
            continue;
        }
        auto conn = it.second.connection.lock();
        if (conn)
        {
            conns.push_back(conn);
        }
    }
    return conns;
}
