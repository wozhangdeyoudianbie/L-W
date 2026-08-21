#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "checkpoint_codec.h"
#include "checkpoint_store.h"
#include<cassert>
#include<cerrno>
#include<chrono>
#include<cstdint>
#include<cstdlib>
#include<fcntl.h>
#include<iostream>
#include<string>
#include<sys/ioctl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<thread>
#include<unistd.h>
#include<utility>
#include<vector>

class TempDirectory
{
public:
    TempDirectory()
    {
        char pattern[] =
            "/tmp/lw-persistence-test-XXXXXX";

        char *path = ::mkdtemp(pattern);
        assert(path != nullptr);

        path_ = path;
    }

    ~TempDirectory()
    {
        const std::string checkpoint =
            checkpoint_path();

        const std::string temporary =
            checkpoint + ".tmp";

        ::unlink(temporary.c_str());
        ::rmdir(temporary.c_str());
        ::unlink(checkpoint.c_str());
        ::rmdir(path_.c_str());
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(
        const TempDirectory &) = delete;

    std::string checkpoint_path() const
    {
        return path_ + "/checkpoint.dat";
    }

private:
    std::string path_;
};

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
        same_players(
            lhs.game_states,
            rhs.game_states);
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
        same_sessions(
            lhs.sessions,
            rhs.sessions);
}

static std::string make_token(char suffix)
{
    std::string token(31, '0');
    token.push_back(suffix);
    return token;
}

static ServerCheckpoint make_checkpoint(
    std::uint64_t generation)
{
    ServerCheckpoint checkpoint;
    checkpoint.generation = generation;

    RoomCheckpoint running_room;
    running_room.room_id = 1;
    running_room.capacity = 4;
    running_room.next_player_id = 3;
    running_room.state =
        Roomstatemachine::States::running;
    running_room.tick_id = 25;
    running_room.members =
    {
        CheckpointMember
        {
            1,
            std::string("al\0pha", 6)
    },
        CheckpointMember
        {
            2,
            "beta"
    }
    };
    running_room.game_states =
    {
        PlayerGameState{1, 1, 2, 100},
        PlayerGameState{2, 5, 6, 0}
    };

    RoomCheckpoint waiting_room;
    waiting_room.room_id = 2;
    waiting_room.capacity = 2;
    waiting_room.next_player_id = 2;
    waiting_room.state =
        Roomstatemachine::States::waiting;
    waiting_room.tick_id = 0;
    waiting_room.members =
    {
        CheckpointMember{1, "gamma"}
    };

    checkpoint.rooms =
    {
        running_room,
        waiting_room
    };

    checkpoint.sessions =
    {
        SessionCheckpoint
        {
            make_token('1'),
            1,
            1
    },
        SessionCheckpoint
        {
            make_token('2'),
            1,
            2
    },
        SessionCheckpoint
        {
            make_token('3'),
            2,
            1
    }
    };

    return checkpoint;
}

static bool write_all_for_test(
    int fd,
    const std::string &data)
{
    std::size_t offset = 0;

    while (offset < data.size())
    {
        const ssize_t written = ::write(
            fd,
            data.data() + offset,
            data.size() - offset);

        if (written > 0)
        {
            offset +=
                static_cast<std::size_t>(written);
            continue;
        }

        if (written < 0 && errno == EINTR)
        {
            continue;
        }

        return false;
    }

    return true;
}

static void write_file(
    const std::string &path,
    const std::string &data)
{
    const int fd = ::open(
        path.c_str(),
        O_WRONLY |
            O_CREAT |
            O_TRUNC |
            O_CLOEXEC,
        S_IRUSR | S_IWUSR);

    assert(fd != -1);
    assert(write_all_for_test(fd, data));
    assert(::fsync(fd) == 0);
    assert(::close(fd) == 0);
}

static void test_codec_round_trip()
{
    const ServerCheckpoint input =
        make_checkpoint(7);

    std::string encoded;

    assert(
        CheckpointCodec::encode(
        input,
        encoded) ==
        CheckpointCodec::States::success);

    assert(encoded.size() >= 40);
    assert(encoded[0] == 'L');
    assert(encoded[1] == 'W');
    assert(encoded[2] == 'C');
    assert(encoded[3] == 'P');

    ServerCheckpoint output;

    assert(
        CheckpointCodec::decode(
        encoded,
        output) ==
        CheckpointCodec::States::success);

    assert(same_server(input, output));
    assert(
        output.rooms[0].
            members[0].
            player_name.size() == 6);

    assert(
        output.rooms[0].
            members[0].
            player_name[2] == '\0');
}

static void test_codec_empty_checkpoint()
{
    ServerCheckpoint input;
    input.generation = 1;

    std::string encoded;

    assert(
        CheckpointCodec::encode(
        input,
        encoded) ==
        CheckpointCodec::States::success);

    assert(encoded.size() == 40);

    ServerCheckpoint output;

    assert(
        CheckpointCodec::decode(
        encoded,
        output) ==
        CheckpointCodec::States::success);

    assert(same_server(input, output));
}

static void test_codec_invalid_checkpoint()
{
    ServerCheckpoint invalid_generation =
        make_checkpoint(0);

    std::string output = "unchanged";

    assert(
        CheckpointCodec::encode(
        invalid_generation,
        output) ==
        CheckpointCodec::States::
            invalid_checkpoint);

    assert(output == "unchanged");

    ServerCheckpoint invalid_state =
        make_checkpoint(1);

    invalid_state.rooms[0].state =
        static_cast<Roomstatemachine::States>(99);

    assert(
        CheckpointCodec::encode(
        invalid_state,
        output) ==
        CheckpointCodec::States::
            invalid_checkpoint);

    assert(output == "unchanged");
}

static void test_codec_truncation()
{
    const ServerCheckpoint input =
        make_checkpoint(10);

    std::string encoded;

    assert(
        CheckpointCodec::encode(
        input,
        encoded) ==
        CheckpointCodec::States::success);

    for (std::size_t size = 0;
         size < encoded.size();
         ++size)
    {
        const std::string truncated =
            encoded.substr(0, size);

        ServerCheckpoint output =
            make_checkpoint(999);

        const ServerCheckpoint before = output;

        assert(
            CheckpointCodec::decode(
            truncated,
            output) !=
            CheckpointCodec::States::success);

        assert(same_server(output, before));
    }
}

static void test_codec_corruption()
{
    const ServerCheckpoint input =
        make_checkpoint(11);

    std::string encoded;

    assert(
        CheckpointCodec::encode(
        input,
        encoded) ==
        CheckpointCodec::States::success);

    assert(encoded.size() > 40);

    {
        std::string corrupted = encoded;
        corrupted[0] = 'X';

        ServerCheckpoint output;

        assert(
            CheckpointCodec::decode(
            corrupted,
            output) ==
            CheckpointCodec::States::
                malformed_data);
    }

    {
        std::string unsupported = encoded;

        unsupported[4] = 0;
        unsupported[5] = 0;
        unsupported[6] = 0;
        unsupported[7] = 2;

        ServerCheckpoint output;

        assert(
            CheckpointCodec::decode(
            unsupported,
            output) ==
            CheckpointCodec::States::
                unsupported_version);
    }

    {
        std::string corrupted = encoded;
        corrupted[40] ^= 1;

        ServerCheckpoint output =
            make_checkpoint(999);

        const ServerCheckpoint before = output;

        assert(
            CheckpointCodec::decode(
            corrupted,
            output) ==
            CheckpointCodec::States::
                checksum_mismatch);

        assert(same_server(output, before));
    }

    {
        std::string corrupted = encoded;
        corrupted[39] ^= 1;

        ServerCheckpoint output;

        assert(
            CheckpointCodec::decode(
            corrupted,
            output) ==
            CheckpointCodec::States::
                checksum_mismatch);
    }

    {
        std::string trailing = encoded;
        trailing.push_back('x');

        ServerCheckpoint output;

        assert(
            CheckpointCodec::decode(
            trailing,
            output) ==
            CheckpointCodec::States::
                malformed_data);
    }
}

static void test_store_not_found_and_stale_temp()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    write_file(
        path + ".tmp",
        "incomplete checkpoint");

    CheckpointStore store(path);

    const CheckpointStore::LoadResult result =
        store.load();

    assert(
        result.state ==
        CheckpointStore::Loadstates::not_found);

    assert(store.start());
    assert(store.stop());
}

static void test_invalid_store()
{
    CheckpointStore store("");

    assert(
        store.load().state ==
        CheckpointStore::Loadstates::
            invalid_state);

    assert(!store.start());
    assert(!store.flush());
    assert(!store.stop());
}

static void test_store_commit_and_restart()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    const ServerCheckpoint generation_1 =
        make_checkpoint(1);

    {
        CheckpointStore store(path);

        assert(
            store.load().state ==
            CheckpointStore::Loadstates::
                not_found);

        assert(store.start());

        assert(
            store.load().state ==
            CheckpointStore::Loadstates::
                invalid_state);

        assert(store.submit(generation_1));
        assert(store.flush());

        const CheckpointStore::Status status =
            store.status();

        assert(
            status.last_submitted_generation == 1);

        assert(
            status.last_committed_generation == 1);

        assert(
            status.last_failed_generation == 0);

        assert(store.stop());
    }

    const ServerCheckpoint generation_2 =
        make_checkpoint(2);

    {
        CheckpointStore store(path);

        const CheckpointStore::LoadResult result =
            store.load();

        assert(
            result.state ==
            CheckpointStore::Loadstates::success);

        assert(
            same_server(
            result.checkpoint,
            generation_1));

        const CheckpointStore::Status status =
            store.status();

        assert(
            status.last_submitted_generation == 1);

        assert(
            status.last_committed_generation == 1);

        assert(store.start());

        assert(!store.submit(generation_1));
        assert(store.submit(generation_2));
        assert(store.flush());
        assert(store.stop());
    }

    {
        CheckpointStore store(path);

        const CheckpointStore::LoadResult result =
            store.load();

        assert(
            result.state ==
            CheckpointStore::Loadstates::success);

        assert(
            same_server(
            result.checkpoint,
            generation_2));
    }
}

static void test_store_generation_and_lifecycle()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    CheckpointStore store(path);

    assert(store.start());
    assert(!store.start());

    assert(store.submit(make_checkpoint(5)));
    assert(!store.submit(make_checkpoint(5)));
    assert(!store.submit(make_checkpoint(4)));

    assert(store.flush());
    assert(store.stop());
    assert(store.stop());

    assert(store.start());
    assert(store.submit(make_checkpoint(6)));
    assert(store.stop());

    const CheckpointStore::Status status =
        store.status();

    assert(
        status.last_submitted_generation == 6);

    assert(
        status.last_committed_generation == 6);
}

static void test_store_destructor_drains()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    const ServerCheckpoint checkpoint =
        make_checkpoint(1);

    {
        CheckpointStore store(path);

        assert(store.start());
        assert(store.submit(checkpoint));
    }

    CheckpointStore loader(path);

    const CheckpointStore::LoadResult result =
        loader.load();

    assert(
        result.state ==
        CheckpointStore::Loadstates::success);

    assert(
        same_server(
        result.checkpoint,
        checkpoint));
}

static void test_store_corrupted_file()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    ServerCheckpoint checkpoint =
        make_checkpoint(1);

    std::string encoded;

    assert(
        CheckpointCodec::encode(
        checkpoint,
        encoded) ==
        CheckpointCodec::States::success);

    assert(encoded.size() > 40);

    encoded[40] ^= 1;
    write_file(path, encoded);

    CheckpointStore store(path);

    assert(
        store.load().state ==
        CheckpointStore::Loadstates::
            decode_error);
}

static void test_write_failure_preserves_previous()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    const ServerCheckpoint generation_1 =
        make_checkpoint(1);

    {
        CheckpointStore store(path);

        assert(store.start());
        assert(store.submit(generation_1));
        assert(store.flush());
        assert(store.stop());
    }

    const std::string temporary_path =
        path + ".tmp";

    assert(
        ::mkdir(
        temporary_path.c_str(),
        S_IRWXU) == 0);

    {
        CheckpointStore store(path);

        const CheckpointStore::LoadResult loaded =
            store.load();

        assert(
            loaded.state ==
            CheckpointStore::Loadstates::success);

        assert(
            same_server(
            loaded.checkpoint,
            generation_1));

        assert(store.start());
        assert(store.submit(make_checkpoint(2)));

        assert(!store.flush());

        const CheckpointStore::Status status =
            store.status();

        assert(
            status.last_submitted_generation == 2);

        assert(
            status.last_committed_generation == 1);

        assert(
            status.last_failed_generation == 2);

        assert(!store.stop());
    }

    assert(
        ::rmdir(temporary_path.c_str()) == 0);

    {
        CheckpointStore loader(path);

        const CheckpointStore::LoadResult result =
            loader.load();

        assert(
            result.state ==
            CheckpointStore::Loadstates::success);

        assert(
            same_server(
            result.checkpoint,
            generation_1));
    }
}

static bool wait_until_pipe_full(
    int fd,
    int pipe_capacity)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(5);

    while (
        std::chrono::steady_clock::now() <
        deadline)
    {
        int available = 0;

        if (::ioctl(
            fd,
            FIONREAD,
            &available) == 0 &&
            available >= pipe_capacity)
        {
            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    return false;
}

static void drain_fifo(int fd)
{
    char buffer[8192];

    while (true)
    {
        const ssize_t count =
            ::read(fd, buffer, sizeof(buffer));

        if (count > 0)
        {
            continue;
        }

        if (count == 0)
        {
            break;
        }

        if (errno == EINTR)
        {
            continue;
        }

        assert(false);
    }

    assert(::close(fd) == 0);
}

static void test_latest_pending_replacement()
{
    TempDirectory directory;

    const std::string path =
        directory.checkpoint_path();

    const std::string fifo_path =
        path + ".tmp";

    assert(
        ::mkfifo(
        fifo_path.c_str(),
        S_IRUSR | S_IWUSR) == 0);

    const int reader_fd = ::open(
        fifo_path.c_str(),
        O_RDONLY |
            O_NONBLOCK |
            O_CLOEXEC);

    assert(reader_fd != -1);

    int pipe_capacity = ::fcntl(
        reader_fd,
        F_SETPIPE_SZ,
        4096);

    if (pipe_capacity == -1)
    {
        pipe_capacity = ::fcntl(
            reader_fd,
            F_GETPIPE_SZ);
    }

    assert(pipe_capacity > 0);

    ServerCheckpoint generation_1 =
        make_checkpoint(1);

    generation_1.rooms[0].
        members[0].
        player_name.assign(
            static_cast<std::size_t>(
        pipe_capacity) +
                1024U * 1024U,
            'x');

    const ServerCheckpoint generation_2 =
        make_checkpoint(2);

    const ServerCheckpoint generation_3 =
        make_checkpoint(3);

    CheckpointStore store(path);

    assert(store.start());
    assert(store.submit(generation_1));

    /*
     * G1 正在向容量有限的 FIFO 写入大检查点。
     * 没有读取方消费时，它会停在 write() 中，
     * 因而可以稳定地让 G2、G3 留在 pending。
     */
    assert(
        wait_until_pipe_full(
        reader_fd,
        pipe_capacity));

    assert(store.submit(generation_2));
    assert(store.submit(generation_3));

    const int flags = ::fcntl(
        reader_fd,
        F_GETFL);

    assert(flags != -1);

    assert(
        ::fcntl(
        reader_fd,
        F_SETFL,
        flags & ~O_NONBLOCK) != -1);

    std::thread drain_thread(
        drain_fifo,
        reader_fd);

    /*
     * G1 写入 FIFO 后会在 fsync(FIFO) 失败；
     * G2 已经被 G3 替换；
     * 随后 G3 写入真正的普通文件。
     */
    assert(store.flush());

    drain_thread.join();

    const CheckpointStore::Status status =
        store.status();

    assert(
        status.last_submitted_generation == 3);

    assert(
        status.last_committed_generation == 3);

    assert(
        status.last_failed_generation == 1);

    assert(store.stop());

    CheckpointStore loader(path);

    const CheckpointStore::LoadResult result =
        loader.load();

    assert(
        result.state ==
        CheckpointStore::Loadstates::success);

    assert(
        same_server(
        result.checkpoint,
        generation_3));
}

int main()
{
    test_codec_round_trip();
    test_codec_empty_checkpoint();
    test_codec_invalid_checkpoint();
    test_codec_truncation();
    test_codec_corruption();

    test_store_not_found_and_stale_temp();
    test_invalid_store();
    test_store_commit_and_restart();
    test_store_generation_and_lifecycle();
    test_store_destructor_drains();
    test_store_corrupted_file();
    test_write_failure_preserves_previous();
    test_latest_pending_replacement();

    std::cout
        << "persistence store tests passed"
        << std::endl;

    return 0;
}
