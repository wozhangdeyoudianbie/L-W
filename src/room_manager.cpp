#include "room_manager.h"
#include <utility>

// 创建房间
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

// 查询：房间总数
std::size_t RoomManager::room_count() const
{
    return rooms_.size();
}

// 查询：房间是否存在
bool RoomManager::contains_room(std::uint32_t room_id) const
{
    return rooms_.find(room_id) != rooms_.end();
}

// 加入：只创建房间成员，不再自动开局
RoomManager::JoinResult RoomManager::join(const Connection::ConnectionPtr &connection, std::uint32_t room_id, const std::string &player_name)
{
    JoinResult temp_;
    if (!connection)
    {
        temp_.state = States::invalid_connection;
        return temp_;
    }
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = States::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    Room::JoinResult room_result = room.join(connection, player_name);
    switch (room_result.state)
    {
        case Room::Joinstates::success:
            {
                temp_.members = std::move(room_result.members);
                temp_.notify_connections = room.connections(room_result.player_id);
                temp_.state = States::success;
                temp_.room_id = room_id;
                temp_.player_id = room_result.player_id;
                return temp_;
            }
        case Room::Joinstates::invalid_connection:
            {
                temp_.state = States::invalid_connection;
                return temp_;
            }
        case Room::Joinstates::room_full:
            {
                temp_.state = States::room_full;
                return temp_;
            }
        case Room::Joinstates::invalid_state:
            {
                temp_.state = States::room_not_joinable;
                return temp_;
            }
        case Room::Joinstates::invalid_player_name:
            {
                temp_.state = States::invalid_player_name;
                return temp_;
            }
        case Room::Joinstates::player_id_exhausted:
            {
                temp_.state = States::player_id_exhausted;
                return temp_;
            }
        default:
            {
                temp_.state = States::internal_error;
                return temp_;
            }
    }
}

// 满员启动：未满员保持等待，恰好满员时启动房间
RoomManager::States RoomManager::start_if_full(std::uint32_t room_id)
{
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        return States::room_not_found;
    }
    Room &room = *room_it->second;
    if (room.member_count() < room.capacity())
    {
        return States::success;   // 未满：直接成功，不修改状态
    }
    if (room.member_count() > room.capacity())
    {
        return States::internal_error;   // 超员：内部不变量已损坏
    }
    const Roomstatemachine::Transitionstates transition_state = room.start(true);
    switch (transition_state)
    {
        case Roomstatemachine::Transitionstates::success:
            return States::success;
        case Roomstatemachine::Transitionstates::invalid_state:
            return States::room_not_joinable;   // 已开局或不可开局
        case Roomstatemachine::Transitionstates::condition_not_met:
            return States::internal_error;
    }
    return States::internal_error;
}

// 离开：清除成员关系
RoomManager::LeaveResult RoomManager::leave(std::uint32_t room_id, std::uint64_t player_id)
{
    LeaveResult temp_;
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = States::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    if (!room.contains(player_id))
    {
        temp_.state = States::not_in_room;
        return temp_;
    }
    if (!room.leave(player_id))
    {
        temp_.state = States::internal_error;
        return temp_;
    }
    temp_.notify_connections = room.connections();
    temp_.state = States::success;
    temp_.room_id = room_id;
    temp_.player_id = player_id;
    return temp_;
}

// 发言：校验消息并返回同房其他连接
RoomManager::ChatResult RoomManager::chat(std::uint32_t room_id, std::uint64_t player_id, const std::string &message) const
{
    ChatResult temp_;
    if (message.empty() || message.size() > Protocol::MAX_CHAT_MESSAGE_SIZE)
    {
        temp_.state = States::invalid_message;
        return temp_;
    }
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = States::room_not_found;
        return temp_;
    }
    const Room &room = *room_it->second;
    if (!room.contains(player_id))
    {
        temp_.state = States::not_in_room;
        return temp_;
    }
    temp_.state = States::success;
    temp_.room_id = room_id;
    temp_.player_id = player_id;
    temp_.notify_connections = room.connections();
    return temp_;
}


// 提交移动命令（仅运行中）
RoomManager::CommandResult RoomManager::move(std::uint32_t room_id, std::uint64_t player_id, std::int32_t dx, std::int32_t dy)
{
    CommandResult temp_;
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = States::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    if (!room.contains(player_id))
    {
        temp_.state = States::not_in_room;
        return temp_;
    }
    const Room::Commandstates command_state = room.submit_move(player_id, dx, dy);
    switch (command_state)
    {
        case Room::Commandstates::success:
            {
                temp_.state = States::success;
                temp_.room_id = room_id;
                temp_.player_id = player_id;
                return temp_;
            }
        case Room::Commandstates::invalid_state:
            {
                temp_.state = States::room_not_running;
                return temp_;
            }
        case Room::Commandstates::already_submitted:
            {
                temp_.state = States::already_submitted;
                return temp_;
            }
        default:
            {
                temp_.state = States::internal_error;
                return temp_;
            }
    }
}


// 提交攻击命令（仅运行中）
RoomManager::CommandResult RoomManager::attack(std::uint32_t room_id, std::uint64_t player_id, std::uint64_t target_player_id)
{
    CommandResult temp_;
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = States::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    if (!room.contains(player_id))
    {
        temp_.state = States::not_in_room;
        return temp_;
    }
    const Room::Commandstates command_state = room.submit_attack(player_id, target_player_id);
    switch (command_state)
    {
        case Room::Commandstates::success:
            {
                temp_.state = States::success;
                temp_.room_id = room_id;
                temp_.player_id = player_id;
                return temp_;
            }
        case Room::Commandstates::invalid_state:
            {
                temp_.state = States::room_not_running;
                return temp_;
            }
        case Room::Commandstates::already_submitted:
            {
                temp_.state = States::already_submitted;
                return temp_;
            }
        default:
            {
                temp_.state = States::internal_error;
                return temp_;
            }
    }
}


// 结算：推进所有运行中房间并汇总结果
std::vector<RoomManager::TickResult> RoomManager::tick_rooms()
{
    std::vector<RoomManager::TickResult> results;
    for (auto &pos : rooms_)
    {
        Room &room = *pos.second;
        if (room.state() != Roomstatemachine::States::running)
        {
            continue;
        }
        Room::TickResult tick_result = room.tick();
        if (tick_result.state != Room::Tickstates::success)
        {
            continue;
        }
        results.push_back(RoomManager::TickResult{});
        RoomManager::TickResult &out = results.back();
        out.room_id = room.id();
        out.tick_id = tick_result.tick_id;
        out.processed_commands = tick_result.processed_commands;
        out.successful_commands = tick_result.successful_commands;
        out.snapshot = std::move(tick_result.snapshot);
        out.notify_connections = room.connections();
    }
    return results;
}

RoomManager::BindingResult RoomManager::bind_connection(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection)
{
    BindingResult temp_;
    if (!connection)
    {
        temp_.state = Bindingstates::invalid_connection;
        return temp_;
    }
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = Bindingstates::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    const Room::Bindingstates room_state = room.bind_connection(player_id, connection);
    switch (room_state)
    {
        case Room::Bindingstates::success:
            {
                try
                {
                    temp_.room_state = room.state();
                    temp_.tick_id = room.tick_id();
                    temp_.members = room.member_snapshot();
                    temp_.snapshot = room.game_snapshot();
                }
                catch (...)
                {
                    room.detach_connection(player_id, connection);
                    throw;
                }
                temp_.state = Bindingstates::success;
                temp_.room_id = room_id;
                temp_.player_id = player_id;
                return temp_;
            }
        case Room::Bindingstates::invalid_connection:
            {
                temp_.state = Bindingstates::invalid_connection;
                return temp_;
            }
        case Room::Bindingstates::player_not_found:
            {
                temp_.state = Bindingstates::player_not_found;
                return temp_;
            }
        case Room::Bindingstates::already_bound:
            {
                temp_.state = Bindingstates::already_bound;
                return temp_;
            }
        default:
            {
                temp_.state = Bindingstates::internal_error;
                return temp_;
            }
    }
}

RoomManager::DetachResult RoomManager::detach_connection(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection)
{
    DetachResult temp_;
    if (!connection)
    {
        temp_.state = Bindingstates::invalid_connection;
        return temp_;
    }
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end())
    {
        temp_.state = Bindingstates::room_not_found;
        return temp_;
    }
    Room &room = *room_it->second;
    const Room::Bindingstates room_state = room.detach_connection(player_id, connection);
    switch (room_state)
    {
        case Room::Bindingstates::success:
            {
                temp_.state = Bindingstates::success;
                temp_.room_id = room_id;
                temp_.player_id = player_id;
                return temp_;
            }
        case Room::Bindingstates::invalid_connection:
            {
                temp_.state = Bindingstates::invalid_connection;
                return temp_;
            }
        case Room::Bindingstates::player_not_found:
            {
                temp_.state = Bindingstates::player_not_found;
                return temp_;
            }
        case Room::Bindingstates::not_bound:
            {
                temp_.state = Bindingstates::not_bound;
                return temp_;
            }
        case Room::Bindingstates::connection_mismatch:
            {
                temp_.state = Bindingstates::connection_mismatch;
                return temp_;
            }
        default:
            {
                temp_.state = Bindingstates::internal_error;
                return temp_;
            }
    }
}





