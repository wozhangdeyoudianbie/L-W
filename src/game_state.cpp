#include "game_state.h"
#include<algorithm>
#include<cstdlib>

Gamestate::Gamestate()
{
}

std::size_t Gamestate::player_count() const
{
    return players_.size();
}

bool Gamestate::contains(std::uint64_t player_id) const
{
    auto it = players_.find(player_id);
    if (it == players_.end())
    {
        return false;
    }
    return true;
}

Gamestate::States Gamestate::initialize(const std::vector<std::uint64_t> &player_ids)
{
    if (player_ids.empty())
    {
        return States::empty_players;
    }
    if (player_ids.size() > MAX_PLAYERS)
    {
        return States::too_many_players;
    }
    std::unordered_map<std::uint64_t, bool> check;
    for (auto id : player_ids)
    {
        if (id == 0)
        {
            return States::invalid_player_id;
        }
        if (check.find(id) != check.end())
        {
            return States::duplicate_player;
        }
        check.emplace(id, true);
    }
    std::vector<std::uint64_t> temp_ = player_ids;
    std::sort(temp_.begin(), temp_.end());
    std::unordered_map<std::uint64_t, PlayerGameState> temp_players;
    std::size_t location = 0;
    for (auto &id : temp_)
    {
        temp_players[id].hp = INITIAL_HP;
        temp_players[id].player_id = id;
        temp_players[id].x = location++;
        temp_players[id].y = 0;
    }
    players_.swap(temp_players);
    return States::success;
}

Gamestate::States Gamestate::remove_player(std::uint64_t player_id)
{
    if (player_id == 0)
    {
        return States::invalid_player_id;
    }
    if (players_.find(player_id) == players_.end())
    {
        return States::player_not_found;
    }
    players_.erase(player_id);
    return States::success;
}

Gamestate::States Gamestate::move_player(std::uint64_t player_id, std::int32_t dx, std::int32_t dy)
{
    if (player_id == 0)
    {
        return States::invalid_player_id;
    }
    auto it = players_.find(player_id);
    if (it == players_.end())
    {
        return States::player_not_found;
    }
    bool valid_x_move = (dx == 1 || dx == -1) && dy == 0;
    bool valid_y_move = (dy == 1 || dy == -1) && dx == 0;
    if (!valid_x_move && !valid_y_move)
    {
        return States::invalid_move;
    }
    std::int32_t new_x = it->second.x + dx;
    std::int32_t new_y = it->second.y + dy;
    if (new_x < MIN_POSITION || new_x > MAX_POSITION || new_y < MIN_POSITION || new_y > MAX_POSITION)
    {
        return States::out_of_bounds;
    }
    it->second.x = new_x;
    it->second.y = new_y;
    return States::success;
}

bool Gamestate::player_state(std::uint64_t player_id, PlayerGameState &state) const
{
    auto it = players_.find(player_id);
    if (it == players_.end())
    {
        return false;
    }
    state = it->second;
    return true;
}

std::vector<PlayerGameState> Gamestate::snapshot() const
{
    std::vector<PlayerGameState> snapshot_;
    snapshot_.reserve(players_.size());
    for (const auto &entry : players_)
    {
        snapshot_.push_back(entry.second);
    }
    std::sort(snapshot_.begin(), snapshot_.end(), [](const PlayerGameState &a, const PlayerGameState &b)
    {
        return a.player_id < b.player_id;
    });
    return snapshot_;
}

Gamestate::Attackresult Gamestate::attack_player(std::uint64_t attacker_id, std::uint64_t target_player_id)
{
    Attackresult result
    {
        States::success, attacker_id, target_player_id, 0, false
    };
    result.attacker_id = attacker_id;
    result.target_player_id = target_player_id;
    if (attacker_id == 0 || target_player_id == 0)
    {
        result.state = States::invalid_player_id;
        return result;
    }
    if (attacker_id == target_player_id)
    {
        result.state = States::invalid_target;
        return result;
    }
    auto attacker_it = players_.find(attacker_id);
    if (attacker_it == players_.end())
    {
        result.state = States::attacker_not_found;
        return result;
    }
    auto target_it = players_.find(target_player_id);
    if (target_it == players_.end())
    {
        result.state = States::target_not_found;
        return result;
    }
    if (attacker_it->second.hp <= 0)
    {
        result.state = States::attacker_dead;
        return result;
    }
    if (target_it->second.hp <= 0)
    {
        result.state = States::target_dead;
        return result;
    }
    std::int32_t distance = std::abs(attacker_it->second.x - target_it->second.x) + std::abs(attacker_it->second.y - target_it->second.y);
    if (distance > 1)
    {
        result.state = States::target_out_of_range;
        return result;
    }
    target_it->second.hp -= ATTACK_DAMAGE;
    result.state = States::success;
    result.remaining_hp = target_it->second.hp;
    result.target_eliminated = target_it->second.hp <= 0;
    return result;
}

