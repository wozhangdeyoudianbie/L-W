#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "game_state.h"
#include "room_state_machine.h"
#include<cstdint>
#include<string>
#include<vector>

struct CheckpointMember
{
    std::uint64_t player_id = 0;
    std::string player_name;
};

struct RoomCheckpoint
{
    std::uint32_t room_id = 0;
    std::uint64_t capacity = 0;
    std::uint64_t next_player_id = 0;
    Roomstatemachine::States state = Roomstatemachine::States::waiting;
    std::uint64_t tick_id = 0;
    std::vector<CheckpointMember> members;
    std::vector<PlayerGameState> game_states;
};

struct SessionCheckpoint
{
    std::string token;
    std::uint32_t room_id = 0;
    std::uint64_t player_id = 0;
};

struct ServerCheckpoint
{
    std::uint64_t generation = 0;
    std::vector<RoomCheckpoint> rooms;
    std::vector<SessionCheckpoint> sessions;
};

#endif
