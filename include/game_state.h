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
        States state;
        std::uint64_t attacker_id;
        std::uint64_t target_player_id;
        std::int32_t remaining_hp;
        bool target_eliminated;
    };
    Gamestate();                                         // 构造：空权威状态
    std::size_t player_count() const;                    // 查询：玩家数
    bool contains(std::uint64_t player_id) const;        // 查询：玩家是否存在
    States initialize(const std::vector<std::uint64_t> &player_ids);   // 用玩家列表初始化权威状态（失败不改原状态）
    States remove_player(std::uint64_t player_id);                     // 删除玩家
    States move_player(std::uint64_t player_id, std::int32_t dx, std::int32_t dy);  // 玩家移动一格（上下左右）
    bool player_state(std::uint64_t player_id, PlayerGameState &state) const;       // 查询单个玩家状态
    std::vector<PlayerGameState> snapshot() const;                    // 全部玩家快照（按 id 排序）
    Attackresult attack_player(std::uint64_t attacker_id, std::uint64_t target_player_id);  // 攻击：扣目标 10 点血
    bool restore(const std::vector<PlayerGameState> &states);  // 从持久化状态全有或全无地恢复
private:
    static constexpr std::int32_t INITIAL_HP = 100;
    static constexpr std::int32_t MIN_POSITION = 0;
    static constexpr std::int32_t MAX_POSITION = 100;
    static constexpr std::int32_t ATTACK_DAMAGE = 10;
    static constexpr std::size_t MAX_PLAYERS = 101;
    std::unordered_map<std::uint64_t, PlayerGameState> players_;
};

#endif
