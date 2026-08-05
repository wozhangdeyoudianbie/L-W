#include "room.h"

Room::Room(std::uint32_t room_id, std::size_t capacity)
    :room_id_(room_id), capacity_(capacity), next_player_id_(1), tick_id_(0)
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
    if (state_machine_.state() != Roomstatemachine::States::waiting)
    {
        return Roomstatemachine::Transitionstates::invalid_state;
    }
    if (!ready_to_start)
    {
        return Roomstatemachine::Transitionstates::condition_not_met;
    }
    std::vector<std::uint64_t> player_ids;
    player_ids.reserve(members_.size());
    for (auto &user : members_)
    {
        player_ids.push_back(user.second.player_id);
    }
    Gamestate temp_state;
    if (temp_state.initialize(player_ids) != Gamestate::States::success)
    {
        return Roomstatemachine::Transitionstates::condition_not_met;
    }
    state_machine_.start(true);
    game_state_ = std::move(temp_state);
    tick_id_ = 0;
    pending_commands_.clear();
    return Roomstatemachine::Transitionstates::success;
}

Roomstatemachine::Transitionstates Room::finish(bool should_finish)
{
    if (state_machine_.state() != Roomstatemachine::States::running)
    {
        return Roomstatemachine::Transitionstates::invalid_state;
    }
    if (!should_finish)
    {
        return Roomstatemachine::Transitionstates::condition_not_met;
    }
    if (state_machine_.finish(should_finish) != Roomstatemachine::Transitionstates::success)
    {
        return Roomstatemachine::Transitionstates::invalid_state;
    }
    pending_commands_.clear();
    return Roomstatemachine::Transitionstates::success;
}

std::uint64_t Room::tick_id() const
{
    return tick_id_;
}

std::size_t Room::pending_command_count() const
{
    return pending_commands_.size();
}

std::vector<PlayerGameState> Room::game_snapshot() const
{
    return game_state_.snapshot();
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
        temp_.state = Joinstates::invalid_connection;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (player_name.empty() || player_name.size() > Protocol::MAX_PLAYER_NAME_SIZE)
    {
        temp_.state = Joinstates::invalid_player_name;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (!state_machine_.can_join())
    {
        temp_.state = Joinstates::invalid_state;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (members_.size() >= capacity_)
    {
        temp_.state = Joinstates::room_full;
        temp_.player_id = 0;
        temp_.members = {};
        return temp_;
    }
    if (next_player_id_ == 0)
    {
        temp_.state = Joinstates::player_id_exhausted;
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
    temp_.state = Joinstates::success;
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
    if (state_machine_.state() != Roomstatemachine::States::waiting)
    {
        if (game_state_.remove_player(player_id) != Gamestate::States::success)
        {
            return false;
        }
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

bool Room::has_pending_command(std::uint64_t player_id) const
{
    auto it = std::find_if(pending_commands_.begin(), pending_commands_.end(), [player_id](const Gamecommand &command)
    {
        return std::visit([player_id](const auto &pos)
        {
            return pos.player_id == player_id;
        }, command);
    });
    return it != pending_commands_.end();
}

Room::Commandstates Room::submit_move(std::uint64_t player_id, std::int32_t dx, std::int32_t dy)
{
    if (state_machine_.state() != Roomstatemachine::States::running)
    {
        return Commandstates::invalid_state;
    }
    if (!contains(player_id))
    {
        return Commandstates::player_not_found;
    }
    if (has_pending_command(player_id))
    {
        return Commandstates::already_submitted;
    }
    pending_commands_.emplace_back(Movecommand{player_id, dx, dy});
    return Commandstates::success;
}

Room::Commandstates Room::submit_attack(std::uint64_t player_id, std::uint64_t target_player_id)
{
    if (state_machine_.state() != Roomstatemachine::States::running)
    {
        return Commandstates::invalid_state;
    }
    if (!contains(player_id))
    {
        return Commandstates::player_not_found;
    }
    if (has_pending_command(player_id))
    {
        return Commandstates::already_submitted;
    }
    pending_commands_.emplace_back(Attackcommand{player_id, target_player_id});
    return Commandstates::success;
}

std::vector<Gamecommand> Room::take_pending_commands()
{
    std::vector<Gamecommand> temp_;
    pending_commands_.swap(temp_);
    return temp_;
}

bool Room::process_command(const Movecommand &command)
{
    return game_state_.move_player(command.player_id, command.dx, command.dy) == Gamestate::States::success;
}

bool Room::process_command(const Attackcommand &command)
{
    const Gamestate::Attackresult result = game_state_.attack_player(command.player_id, command.target_player_id);
    return result.state == Gamestate::States::success;
}

Room::TickResult Room::tick()
{
    if (state_machine_.state() != Roomstatemachine::States::running)
    {
        return TickResult{Tickstates::invalid_state, tick_id_, 0, 0, {}};
    }
    std::vector<Gamecommand> commands = take_pending_commands();
    const std::size_t processed = commands.size();
    std::size_t pos = 0;
    for (const auto &command : commands)
    {
        const bool command_succeeded = std::visit([this](const auto &current_command)
        {
            return process_command(current_command);
        }, command);
        if (command_succeeded)
        {
            ++pos;
        }
    }
    ++tick_id_;
    return TickResult{Tickstates::success, tick_id_, processed, pos, game_state_.snapshot()};
}
