#ifndef GAME_STATE_H
#define GAME_STATE_H

#include<cstddef>
#include<cstdint>
#include<unordered_map>
#include<vector>

struct PlayerGameState
{
    std::uint64_t player_id;
    std::int32_t x;
    std::int32_t y;
    std::int32_t hp;
};

class Gamestate
{
public:
    enum class States
    {
        success,
        empty_players,
        invalid_player_id,
        duplicate_player,
        too_many_players,
        player_not_found,
        invalid_move,
        out_of_bounds,
        invalid_target,
        attacker_not_found,
        target_not_found,
        attacker_dead,
        target_dead,
        target_out_of_range
    };
    struct Attackresult
    {
        States status;
        std::uint64_t attacker_id;
        std::uint64_t target_player_id;
        std::int32_t remaining_hp;
        bool target_eliminated;
    };
    Gamestate();
    std::size_t player_count() const;
    bool contains(std::uint64_t player_id) const;
    States initialize(const std::vector<std::uint64_t> &player_ids);
    States remove_player(std::uint64_t player_id);
    States move_player(std::uint64_t player_id, std::int32_t dx, std::int32_t dy);
    bool player_state(std::uint64_t player_id, PlayerGameState &state) const;
    std::vector<PlayerGameState> snapshot() const;
    Attackresult attack_player(std::uint64_t attacker_id, std::uint64_t target_player_id);
private:
    static constexpr std::int32_t INITIAL_HP = 100;
    static constexpr std::int32_t MIN_POSITION = 0;
    static constexpr std::int32_t MAX_POSITION = 100;
    static constexpr std::int32_t ATTACK_DAMAGE = 10;
    static constexpr std::size_t MAX_PLAYERS = 101;
    std::unordered_map<std::uint64_t, PlayerGameState> players_;
};

#endif
