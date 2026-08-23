#include "checkpoint.h"
#include "checkpoint_store.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>

namespace
{
    bool parse_positive_u64(const char *text, std::uint64_t &value)
    {
        if (!text || *text == '\0')
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0' || parsed == 0)
        {
            return false;
        }
        if (parsed > std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool verify_checkpoint(const ServerCheckpoint &checkpoint, std::uint64_t room_count, std::uint64_t room_capacity)
    {
        if (checkpoint.generation != 1 ||
            checkpoint.rooms.size() != room_count ||
            !checkpoint.sessions.empty())
        {
            return false;
        }
        for (std::size_t i = 0; i < checkpoint.rooms.size(); ++i)
        {
            const RoomCheckpoint &room = checkpoint.rooms[i];
            if (room.room_id != static_cast<std::uint32_t>(i + 1) ||
                room.capacity != room_capacity ||
                room.next_player_id != 1 ||
                room.state != Roomstatemachine::States::waiting ||
                room.tick_id != 0 ||
                !room.members.empty() ||
                !room.game_states.empty())
            {
                return false;
            }
        }
        return true;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cerr
            << "usage: " << argv[0]
            << " <checkpoint-path> <room-count> <room-capacity>\n";
        return 1;
    }

    std::uint64_t room_count = 0;
    std::uint64_t room_capacity = 0;
    if (!parse_positive_u64(argv[2], room_count) ||
        !parse_positive_u64(argv[3], room_capacity))
    {
        std::cerr << "room-count and room-capacity must be positive integers\n";
        return 1;
    }
    if (room_count > std::numeric_limits<std::uint32_t>::max() ||
        room_count > std::numeric_limits<std::size_t>::max() ||
        room_capacity > std::numeric_limits<std::size_t>::max())
    {
        std::cerr << "room-count or room-capacity is too large\n";
        return 1;
    }

    const std::filesystem::path checkpoint_path(argv[1]);
    const std::filesystem::path temporary_path(
        checkpoint_path.string() + ".tmp");

    std::error_code filesystem_error;
    const bool checkpoint_exists =
        std::filesystem::exists(
            checkpoint_path,
            filesystem_error);
    if (filesystem_error)
    {
        std::cerr
            << "cannot inspect checkpoint path: "
            << filesystem_error.message() << '\n';
        return 1;
    }
    const bool temporary_exists =
        std::filesystem::exists(
            temporary_path,
            filesystem_error);
    if (filesystem_error)
    {
        std::cerr
            << "cannot inspect temporary checkpoint path: "
            << filesystem_error.message() << '\n';
        return 1;
    }
    if (checkpoint_exists || temporary_exists)
    {
        std::cerr << "refusing to overwrite an existing checkpoint\n";
        return 1;
    }

    if (!checkpoint_path.parent_path().empty())
    {
        std::filesystem::create_directories(
            checkpoint_path.parent_path(),
            filesystem_error);
        if (filesystem_error)
        {
            std::cerr
                << "cannot create checkpoint directory: "
                << filesystem_error.message() << '\n';
            return 1;
        }
    }

    ServerCheckpoint checkpoint;
    checkpoint.generation = 1;
    try
    {
        checkpoint.rooms.reserve(
            static_cast<std::size_t>(room_count));
        for (std::uint64_t i = 1; i <= room_count; ++i)
        {
            RoomCheckpoint room;
            room.room_id = static_cast<std::uint32_t>(i);
            room.capacity = room_capacity;
            room.next_player_id = 1;
            room.state = Roomstatemachine::States::waiting;
            room.tick_id = 0;
            checkpoint.rooms.push_back(std::move(room));
        }
    }
    catch (...)
    {
        std::cerr << "cannot allocate checkpoint rooms\n";
        return 1;
    }

    CheckpointStore store(checkpoint_path.string());
    if (!store.start())
    {
        std::cerr << "cannot start checkpoint store\n";
        return 1;
    }
    if (!store.submit(std::move(checkpoint)))
    {
        store.stop();
        std::cerr << "cannot submit checkpoint\n";
        return 1;
    }
    if (!store.flush())
    {
        store.stop();
        std::cerr << "cannot flush checkpoint\n";
        return 1;
    }
    if (!store.stop())
    {
        std::cerr << "cannot stop checkpoint store cleanly\n";
        return 1;
    }

    CheckpointStore verifier(checkpoint_path.string());
    const CheckpointStore::LoadResult result = verifier.load();
    if (result.state != CheckpointStore::Loadstates::success ||
        !verify_checkpoint(
        result.checkpoint,
        room_count,
        room_capacity))
    {
        std::cerr << "generated checkpoint verification failed\n";
        return 1;
    }

    std::cout
        << "generated " << room_count
        << " empty rooms with capacity "
        << room_capacity << '\n';
    return 0;
}
