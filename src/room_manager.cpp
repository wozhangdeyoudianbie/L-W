#include "room_manager.h"
#include <utility>

bool RoomManager::add_room(std::uint32_t room_id, std::size_t capacity)
{
    if (contains_room(room_id))
    {
        return false;
    }
    auto room = std::make_unique<Room>(room_id, capacity);
    rooms_[room_id] = std::move(room);
    return true;
}

std::size_t RoomManager::room_count() const
{
    return rooms_.size();
}

bool RoomManager::contains_room(std::uint32_t room_id) const
{
    return rooms_.find(room_id) != rooms_.end();
}

bool RoomManager::contains_connection(const Connection::ConnectionPtr &connection) const
{
    if (!connection)
    {
        return false;
    }
    return memberships_.find(connection.get()) != memberships_.end();
}

RoomManager::JoinResult RoomManager::join(const Connection::ConnectionPtr &connection, std::uint32_t room_id, const std::string &player_name)
{
    JoinResult temp_;
    if (!connection)
    {
        temp_.status = Status::invalid_connection;
        return temp_;
    }
    if (contains_connection(connection))
    {
        temp_.status = Status::already_in_room;
        return temp_;
    }
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.status = Status::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    Room::JoinResult room_result = room.join(connection, player_name);
    switch (room_result.status)
    {
        case Room::JoinStatus::success:
            {
                try
                {
                    temp_.members = std::move(room_result.members);
                    temp_.notify_connections = room.connections(room_result.player_id);
                    const bool inserted = memberships_.emplace(connection.get(), Membership{room_id, room_result.player_id}).second;
                    if (!inserted)
                    {
                        room.leave(room_result.player_id);
                        temp_.members.clear();
                        temp_.notify_connections.clear();
                        temp_.status = Status::internal_error;
                        return temp_;
                    }
                }
                catch (...)
                {
                    room.leave(room_result.player_id);
                    throw;
                }
                temp_.status = Status::success;
                temp_.room_id = room_id;
                temp_.player_id = room_result.player_id;
                return temp_;
            }
        case Room::JoinStatus::invalid_connection:
            {
                temp_.status = Status::invalid_connection;
                return temp_;
            }
        case Room::JoinStatus::room_full:
            {
                temp_.status = Status::room_full;
                return temp_;
            }
        case Room::JoinStatus::invalid_player_name:
            {
                temp_.status = Status::invalid_player_name;
                return temp_;
            }
        case Room::JoinStatus::player_id_exhausted:
            {
                temp_.status = Status::player_id_exhausted;
                return temp_;
            }
        default:
            {
                temp_.status = Status::internal_error;
                return temp_;
            }
    }
}

RoomManager::LeaveResult RoomManager::leave(const Connection::ConnectionPtr &connection)
{
    LeaveResult temp_;
    if (!connection)
    {
        temp_.status = Status::invalid_connection;
        return temp_;
    }
    auto membership_it = memberships_.find(connection.get());
    if (membership_it == memberships_.end())
    {
        temp_.status = Status::not_in_room;
        return temp_;
    }
    const Membership membership = membership_it->second;
    auto room_it = rooms_.find(membership.room_id);
    if (room_it == rooms_.end())
    {
        temp_.status = Status::internal_error;
        return temp_;
    }
    Room &room = *room_it->second;
    if (!room.contains(membership.player_id))
    {
        temp_.status = Status::internal_error;
        return temp_;
    }
    if (!room.leave(membership.player_id))
    {
        temp_.status = Status::internal_error;
        return temp_;
    }
    memberships_.erase(membership_it);
    temp_.notify_connections = room.connections();
    temp_.status = Status::success;
    temp_.room_id = membership.room_id;
    temp_.player_id = membership.player_id;
    return temp_;
}

RoomManager::ChatResult RoomManager::chat(const Connection::ConnectionPtr &connection, const std::string &message) const
{
    ChatResult temp_;
    if (!connection)
    {
        temp_.status = Status::invalid_connection;
        return temp_;
    }
    if (message.empty() || message.size() > Protocol::MAX_CHAT_MESSAGE_SIZE)
    {
        temp_.status = Status::invalid_message;
        return temp_;
    }
    auto membership_it = memberships_.find(connection.get());
    if (membership_it == memberships_.end())
    {
        temp_.status = Status::not_in_room;
        return temp_;
    }
    const Membership membership = membership_it->second;
    auto room_it = rooms_.find(membership.room_id);
    if (room_it == rooms_.end())
    {
        temp_.status = Status::internal_error;
        return temp_;
    }
    const Room &room = *room_it->second;
    if (!room.contains(membership.player_id))
    {
        temp_.status = Status::internal_error;
        return temp_;
    }
    temp_.status = Status::success;
    temp_.room_id = membership.room_id;
    temp_.player_id = membership.player_id;
    temp_.notify_connections = room.connections();
    return temp_;
}

RoomManager::LeaveResult RoomManager::disconnect(const Connection::ConnectionPtr &connection)
{
    LeaveResult result = leave(connection);
    if (result.status == Status::not_in_room)
    {
        result.status = Status::success;
    }
    return result;
}
