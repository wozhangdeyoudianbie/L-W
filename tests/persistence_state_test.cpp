#include "checkpoint.h"
#include "checkpoint_codec.h"
#include "checkpoint_store.h"
#include "event_loop.h"
#include "game_state.h"
#include "room.h"
#include "room_manager.h"
#include "room_service.h"
#include "session.h"
#include "session_manager.h"
#include<algorithm>
#include<cerrno>
#include<cassert>
#include<chrono>
#include<cstdint>
#include<cstdlib>
#include<fcntl.h>
#include<iostream>
#include<string>
#include<sys/stat.h>
#include<thread>
#include<unistd.h>
#include<vector>

static bool same_player(
    const PlayerGameState &lhs,
    const PlayerGameState &rhs)
{
    return lhs.player_id == rhs.player_id &&
        lhs.x == rhs.x &&
        lhs.y == rhs.y &&
        lhs.hp == rhs.hp;
}

static bool same_players(
    const std::vector<PlayerGameState> &lhs,
    const std::vector<PlayerGameState> &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (!same_player(lhs[i], rhs[i]))
        {
            return false;
        }
    }
    return true;
}

static bool same_member(
    const CheckpointMember &lhs,
    const CheckpointMember &rhs)
{
    return lhs.player_id == rhs.player_id &&
        lhs.player_name == rhs.player_name;
}

static bool same_members(
    const std::vector<CheckpointMember> &lhs,
    const std::vector<CheckpointMember> &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (!same_member(lhs[i], rhs[i]))
        {
            return false;
        }
    }
    return true;
}

static bool same_room(
    const RoomCheckpoint &lhs,
    const RoomCheckpoint &rhs)
{
    return lhs.room_id == rhs.room_id &&
        lhs.capacity == rhs.capacity &&
        lhs.next_player_id == rhs.next_player_id &&
        lhs.state == rhs.state &&
        lhs.tick_id == rhs.tick_id &&
        same_members(lhs.members, rhs.members) &&
        same_players(lhs.game_states, rhs.game_states);
}

static bool same_rooms(
    const std::vector<RoomCheckpoint> &lhs,
    const std::vector<RoomCheckpoint> &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (!same_room(lhs[i], rhs[i]))
        {
            return false;
        }
    }
    return true;
}

static bool same_session(
    const SessionCheckpoint &lhs,
    const SessionCheckpoint &rhs)
{
    return lhs.token == rhs.token &&
        lhs.room_id == rhs.room_id &&
        lhs.player_id == rhs.player_id;
}

static bool same_sessions(
    const std::vector<SessionCheckpoint> &lhs,
    const std::vector<SessionCheckpoint> &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (!same_session(lhs[i], rhs[i]))
        {
            return false;
        }
    }
    return true;
}

static bool same_server(
    const ServerCheckpoint &lhs,
    const ServerCheckpoint &rhs)
{
    return lhs.generation == rhs.generation &&
        same_rooms(lhs.rooms, rhs.rooms) &&
        same_sessions(lhs.sessions, rhs.sessions);
}

static void canonicalize(ServerCheckpoint &checkpoint)
{
    for (auto &room : checkpoint.rooms)
    {
        std::sort(
            room.members.begin(),
            room.members.end(),
            [](const CheckpointMember &lhs, const CheckpointMember &rhs)
        {
            return lhs.player_id < rhs.player_id;
        });

        std::sort(
            room.game_states.begin(),
            room.game_states.end(),
            [](const PlayerGameState &lhs, const PlayerGameState &rhs)
        {
            return lhs.player_id < rhs.player_id;
        });
    }

    std::sort(
        checkpoint.rooms.begin(),
        checkpoint.rooms.end(),
        [](const RoomCheckpoint &lhs, const RoomCheckpoint &rhs)
    {
        return lhs.room_id < rhs.room_id;
    });

    std::sort(
        checkpoint.sessions.begin(),
        checkpoint.sessions.end(),
        [](const SessionCheckpoint &lhs, const SessionCheckpoint &rhs)
    {
        return lhs.token < rhs.token;
    });
}

static std::string make_token(char suffix)
{
    std::string token(31, '0');
    token.push_back(suffix);
    return token;
}

static RoomCheckpoint make_running_room()
{
    RoomCheckpoint checkpoint;
    checkpoint.room_id = 1;
    checkpoint.capacity = 4;
    checkpoint.next_player_id = 3;
    checkpoint.state = Roomstatemachine::States::running;
    checkpoint.tick_id = 40;
    checkpoint.members =
    {
        CheckpointMember{2, "beta"},
        CheckpointMember{1, "alpha"}
    };
    checkpoint.game_states =
    {
        PlayerGameState{2, 5, 5, 0},
        PlayerGameState{1, 1, 1, 100}
    };
    return checkpoint;
}

static RoomCheckpoint make_waiting_room()
{
    RoomCheckpoint checkpoint;
    checkpoint.room_id = 2;
    checkpoint.capacity = 2;
    checkpoint.next_player_id = 2;
    checkpoint.state = Roomstatemachine::States::waiting;
    checkpoint.tick_id = 0;
    checkpoint.members =
    {
        CheckpointMember{1, "gamma"}
    };
    return checkpoint;
}

static std::vector<SessionCheckpoint> make_sessions()
{
    return
    {
        SessionCheckpoint{make_token('3'), 2, 1},
        SessionCheckpoint{make_token('2'), 1, 2},
        SessionCheckpoint{make_token('1'), 1, 1}
    };
}

static ServerCheckpoint make_server_checkpoint()
{
    ServerCheckpoint checkpoint;
    checkpoint.generation = 7;
    checkpoint.rooms =
    {
        make_waiting_room(),
        make_running_room()
    };
    checkpoint.sessions = make_sessions();
    return checkpoint;
}

static void test_gamestate_restore()
{
    Gamestate state;

    const std::vector<PlayerGameState> valid =
    {
        PlayerGameState{2, 5, 5, 0},
        PlayerGameState{1, 1, 1, 100}
    };

    assert(state.restore(valid));

    const std::vector<PlayerGameState> expected =
    {
        PlayerGameState{1, 1, 1, 100},
        PlayerGameState{2, 5, 5, 0}
    };

    assert(same_players(state.snapshot(), expected));

    const auto check_rejected =
        [&state, &expected](std::vector<PlayerGameState> invalid)
    {
        assert(!state.restore(invalid));
        assert(same_players(state.snapshot(), expected));
    };

    check_rejected(
    {
        PlayerGameState{0, 1, 1, 100}
    });

    check_rejected(
    {
        PlayerGameState{1, 1, 1, 100},
        PlayerGameState{1, 2, 2, 100}
    });

    check_rejected(
    {
        PlayerGameState{1, -1, 1, 100}
    });

    check_rejected(
    {
        PlayerGameState{1, 1, 101, 100}
    });

    check_rejected(
    {
        PlayerGameState{1, 1, 1, -1}
    });

    check_rejected(
    {
        PlayerGameState{1, 1, 1, 101}
    });

    std::vector<PlayerGameState> too_many;
    for (std::uint64_t player_id = 1; player_id <= 102; ++player_id)
    {
        too_many.push_back(PlayerGameState{player_id, 0, 0, 100});
    }
    check_rejected(std::move(too_many));

    Gamestate empty;
    assert(empty.restore({}));
    assert(empty.player_count() == 0);
}

static void test_room_checkpoint()
{
    RoomCheckpoint input = make_running_room();

    Room room(1, 4);
    assert(room.restore_checkpoint(input));
    assert(room.pending_command_count() == 0);

    RoomCheckpoint expected = input;
    ServerCheckpoint wrapper;
    wrapper.rooms.push_back(expected);
    canonicalize(wrapper);
    expected = wrapper.rooms.front();

    RoomCheckpoint output;
    assert(room.make_checkpoint(output));
    assert(same_room(output, expected));

    assert(
        room.submit_move(1, 1, 0) ==
        Room::Commandstates::success);
    assert(room.pending_command_count() == 1);

    RoomCheckpoint with_pending_command;
    assert(room.make_checkpoint(with_pending_command));

    assert(same_room(with_pending_command, expected));
    assert(room.pending_command_count() == 1);

    RoomCheckpoint invalid = input;
    invalid.members.push_back(
        CheckpointMember{1, "duplicate"});

    assert(!room.restore_checkpoint(invalid));
    assert(room.pending_command_count() == 1);

    RoomCheckpoint after_failure;
    assert(room.make_checkpoint(after_failure));
    assert(same_room(after_failure, expected));
}

static void test_room_manager_checkpoint()
{
    std::vector<RoomCheckpoint> input =
    {
        make_waiting_room(),
        make_running_room()
    };

    RoomManager manager;
    assert(manager.restore_checkpoint(input));

    ServerCheckpoint expected_wrapper;
    expected_wrapper.rooms = input;
    canonicalize(expected_wrapper);

    std::vector<RoomCheckpoint> output;
    assert(manager.make_checkpoint(output));
    assert(same_rooms(output, expected_wrapper.rooms));

    std::vector<RoomCheckpoint> invalid = input;
    invalid.front().members.push_back(
        CheckpointMember{1, "duplicate"});

    assert(!manager.restore_checkpoint(invalid));

    std::vector<RoomCheckpoint> after_failure;
    assert(manager.make_checkpoint(after_failure));
    assert(same_rooms(after_failure, expected_wrapper.rooms));
}

static void test_session_restore_constructor()
{
    const auto deadline =
        Session::Clock::now() +
        std::chrono::seconds(30);

    Session session(1, 1, deadline);

    assert(session.room_id() == 1);
    assert(session.player_id() == 1);
    assert(session.state() == Session::States::offline);
    assert(!session.connection());
    assert(session.offline_deadline() == deadline);
}

static void test_session_manager_checkpoint()
{
    const auto now = SessionManager::Clock::now();
    const std::vector<SessionCheckpoint> input =
        make_sessions();

    SessionManager manager(std::chrono::seconds(30));
    assert(manager.restore_checkpoint(input, now));

    std::vector<SessionCheckpoint> expected = input;
    std::sort(
        expected.begin(),
        expected.end(),
        [](const SessionCheckpoint &lhs, const SessionCheckpoint &rhs)
    {
        return lhs.token < rhs.token;
    });

    std::vector<SessionCheckpoint> output;
    assert(manager.make_checkpoint(output));
    assert(same_sessions(output, expected));

    std::vector<SessionCheckpoint> invalid_token = input;
    invalid_token.front().token = "invalid";
    assert(!manager.restore_checkpoint(invalid_token, now));

    std::vector<SessionCheckpoint> duplicate_identity = input;
    duplicate_identity.push_back(
        SessionCheckpoint{make_token('4'), 1, 1});
    assert(!manager.restore_checkpoint(duplicate_identity, now));

    std::vector<SessionCheckpoint> after_failure;
    assert(manager.make_checkpoint(after_failure));
    assert(same_sessions(after_failure, expected));
}

static void test_room_service_checkpoint()
{
    EventLoop base_loop;
    assert(base_loop.valid());

    RoomService service(
        &base_loop,
        std::chrono::seconds(30));

    ServerCheckpoint input = make_server_checkpoint();

    assert(
        service.restore_checkpoint(
        input,
        SessionManager::Clock::now()));

    ServerCheckpoint expected = input;
    expected.generation = 8;
    canonicalize(expected);

    ServerCheckpoint output;
    assert(service.make_checkpoint(8, output));
    assert(same_server(output, expected));

    ServerCheckpoint invalid_output;
    assert(!service.make_checkpoint(0, invalid_output));

    ServerCheckpoint missing_session = input;
    missing_session.sessions.pop_back();

    assert(
        !service.restore_checkpoint(
        missing_session,
        SessionManager::Clock::now()));

    ServerCheckpoint after_failure;
    assert(service.make_checkpoint(9, after_failure));

    expected.generation = 9;
    assert(same_server(after_failure, expected));

    bool wrong_thread_make_result = true;
    std::thread make_thread(
        [&service, &wrong_thread_make_result]()
    {
        ServerCheckpoint checkpoint;
        wrong_thread_make_result =
            service.make_checkpoint(10, checkpoint);
    });
    make_thread.join();
    assert(!wrong_thread_make_result);

    bool wrong_thread_restore_result = true;
    std::thread restore_thread(
        [&service, &input, &wrong_thread_restore_result]()
    {
        wrong_thread_restore_result =
            service.restore_checkpoint(
                input,
                SessionManager::Clock::now());
    });
    restore_thread.join();
    assert(!wrong_thread_restore_result);
}

static void test_checkpoint_codec()
{
    const ServerCheckpoint input = make_server_checkpoint();

    std::string encoded;
    assert(
        CheckpointCodec::encode(input, encoded) ==
        CheckpointCodec::States::success);
    assert(encoded.size() > 16);

    ServerCheckpoint decoded;
    assert(
        CheckpointCodec::decode(encoded, decoded) ==
        CheckpointCodec::States::success);
    assert(same_server(input, decoded));

    ServerCheckpoint unchanged;
    unchanged.generation = 99;

    std::string malformed = encoded.substr(
        0,
        encoded.size() - 1);
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::malformed_data);
    assert(unchanged.generation == 99);

    malformed = encoded;
    malformed[0] = 'X';
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::malformed_data);
    assert(unchanged.generation == 99);

    malformed = encoded;
    malformed[4] = 0;
    malformed[5] = 2;
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::unsupported_version);
    assert(unchanged.generation == 99);

    malformed = encoded;
    malformed[16] = 9;
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::malformed_data);
    assert(unchanged.generation == 99);

    malformed = encoded;
    malformed.back() =
        static_cast<char>(malformed.back() ^ 1);
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::checksum_mismatch);
    assert(unchanged.generation == 99);

    malformed = encoded;
    malformed.push_back('\0');
    assert(
        CheckpointCodec::decode(malformed, unchanged) ==
        CheckpointCodec::States::malformed_data);
    assert(unchanged.generation == 99);

    const std::string too_large(
        64U * 1024U * 1024U + 1U,
        '\0');
    assert(
        CheckpointCodec::decode(too_large, unchanged) ==
        CheckpointCodec::States::too_large);
    assert(unchanged.generation == 99);

    ServerCheckpoint invalid = input;
    invalid.generation = 0;
    std::string invalid_output = "old output";
    assert(
        CheckpointCodec::encode(
        invalid,
        invalid_output) ==
        CheckpointCodec::States::invalid_checkpoint);
    assert(invalid_output == "old output");

    invalid = input;
    invalid.rooms.push_back(input.rooms.front());
    assert(
        CheckpointCodec::encode(
        invalid,
        invalid_output) ==
        CheckpointCodec::States::invalid_checkpoint);

    invalid = input;
    invalid.sessions.front().token[0] = 'A';
    assert(
        CheckpointCodec::encode(
        invalid,
        invalid_output) ==
        CheckpointCodec::States::invalid_checkpoint);
}

static void test_checkpoint_store()
{
    char directory_template[] =
        "/tmp/checkpoint-persistence-XXXXXX";
    const char *directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);

    const std::string path =
        std::string(directory) + "/server.checkpoint";
    const std::string temporary_path = path + ".tmp";
    ServerCheckpoint input = make_server_checkpoint();

    {
        CheckpointStore store(path);
        assert(
            store.load().state ==
            CheckpointStore::Loadstates::not_found);
        assert(store.start());
        assert(store.submit(input));
        assert(store.flush());

        const CheckpointStore::Status status =
            store.status();
        assert(status.last_submitted_generation == 7);
        assert(status.last_committed_generation == 7);
        assert(status.last_failed_generation == 0);
        assert(store.stop());
    }

    struct stat file_status;
    assert(::stat(path.c_str(), &file_status) == 0);
    assert((file_status.st_mode & 0777) == 0600);
    assert(::access(temporary_path.c_str(), F_OK) == -1);
    assert(errno == ENOENT);

    const int temporary_fd = ::open(
        temporary_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0600);
    assert(temporary_fd != -1);
    const std::string stale_data = "stale";
    assert(
        ::write(
        temporary_fd,
        stale_data.data(),
        stale_data.size()) ==
        static_cast<ssize_t>(stale_data.size()));
    assert(::close(temporary_fd) == 0);

    {
        CheckpointStore store(path);
        const CheckpointStore::LoadResult loaded =
            store.load();
        assert(
            loaded.state ==
            CheckpointStore::Loadstates::success);
        assert(same_server(input, loaded.checkpoint));

        const CheckpointStore::Status status =
            store.status();
        assert(status.last_submitted_generation == 7);
        assert(status.last_committed_generation == 7);

        assert(store.start());
        assert(!store.submit(input));

        input.generation = 8;
        assert(store.submit(input));
        assert(store.stop());
    }

    {
        CheckpointStore store(path);
        const CheckpointStore::LoadResult loaded =
            store.load();
        assert(
            loaded.state ==
            CheckpointStore::Loadstates::success);
        assert(same_server(input, loaded.checkpoint));
    }

    assert(::access(temporary_path.c_str(), F_OK) == -1);
    assert(errno == ENOENT);
    assert(::unlink(path.c_str()) == 0);
    assert(::rmdir(directory) == 0);
}

int main()
{
    test_gamestate_restore();
    test_room_checkpoint();
    test_room_manager_checkpoint();
    test_session_restore_constructor();
    test_session_manager_checkpoint();
    test_room_service_checkpoint();
    test_checkpoint_codec();
    test_checkpoint_store();

    std::cout
        << "persistence state tests passed"
        << std::endl;

    return 0;
}
