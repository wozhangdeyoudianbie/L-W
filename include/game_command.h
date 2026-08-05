#ifndef GAME_COMMAND_H
#define GAME_COMMAND_H

#include <cstdint>
#include <variant>

struct Movecommand
{
    std::uint64_t player_id;
    std::int32_t dx;
    std::int32_t dy;
};

struct Attackcommand
{
    std::uint64_t player_id;
    std::uint64_t target_player_id;
};

using Gamecommand = std::variant<Movecommand, Attackcommand>;   // 一条待结算命令：移动或攻击

#endif
