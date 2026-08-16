#include "room_service.h"
#include "codec.h"
#include "connection.h"
#include "event_loop.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
    constexpr std::chrono::milliseconds RECONNECT_TIMEOUT(100);

    struct Frame
    {
        std::uint16_t type;
        std::string payload;
    };

    struct Peer
    {
        int client_fd = -1;
        Connection::ConnectionPtr connection;

        Peer() = default;

        Peer(int fd, Connection::ConnectionPtr conn)
            :client_fd(fd), connection(std::move(conn))
        {
        }

        Peer(const Peer &) = delete;
        Peer &operator=(const Peer &) = delete;

        Peer(Peer &&other) noexcept
            :client_fd(other.client_fd), connection(std::move(other.connection))
        {
            other.client_fd = -1;
        }

        Peer &operator=(Peer &&) = delete;

        ~Peer()
        {
            if (connection)
            {
                connection->connect_destroyed();
                connection.reset();
            }
            if (client_fd >= 0)
            {
                ::close(client_fd);
            }
        }

        bool valid() const
        {
            return client_fd >= 0 && connection != nullptr;
        }
    };

    bool require(bool condition, const char *function_name, int line)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << function_name
                << "，line = " << line << '\n';
            return false;
        }
        return true;
    }

#define REQUIRE(condition)                                      \
    do                                                          \
    {                                                           \
        if (!require((condition), __func__, __LINE__))           \
        {                                                       \
            return false;                                       \
        }                                                       \
    } while (false)

    std::uint16_t message_type(MessageType type)
    {
        return static_cast<std::uint16_t>(type);
    }

    Frame expected_frame(MessageType type, const std::string &payload)
    {
        return {message_type(type), payload};
    }

    void append_u16(std::string &data, std::uint16_t value)
    {
        const std::uint16_t network_value = htons(value);
        data.append(
            reinterpret_cast<const char *>(&network_value),
            sizeof(network_value));
    }

    void append_u32(std::string &data, std::uint32_t value)
    {
        const std::uint32_t network_value = htonl(value);
        data.append(
            reinterpret_cast<const char *>(&network_value),
            sizeof(network_value));
    }

    bool read_u16(const std::string &data, std::size_t &offset, std::uint16_t &value)
    {
        if (offset > data.size() || data.size() - offset < 2)
        {
            return false;
        }

        value =
            (static_cast<std::uint16_t>(
                static_cast<unsigned char>(data[offset])) << 8) |
            static_cast<std::uint16_t>(
                static_cast<unsigned char>(data[offset + 1]));

        offset += 2;
        return true;
    }

    bool read_u32(const std::string &data, std::size_t &offset, std::uint32_t &value)
    {
        if (offset > data.size() || data.size() - offset < 4)
        {
            return false;
        }

        value = 0;

        for (int i = 0; i < 4; ++i)
        {
            value =
                (value << 8) |
                static_cast<unsigned char>(
                    data[offset + static_cast<std::size_t>(i)]);
        }

        offset += 4;
        return true;
    }

    bool read_u64(const std::string &data, std::size_t &offset, std::uint64_t &value)
    {
        if (offset > data.size() || data.size() - offset < 8)
        {
            return false;
        }

        value = 0;

        for (int i = 0; i < 8; ++i)
        {
            value =
                (value << 8) |
                static_cast<unsigned char>(
                    data[offset + static_cast<std::size_t>(i)]);
        }

        offset += 8;
        return true;
    }

    bool parse_join_ok(
        const std::string &payload,
        std::uint32_t &room_id,
        std::uint64_t &player_id,
        std::string &token)
    {
        std::size_t offset = 0;
        std::uint16_t token_size = 0;

        if (!read_u32(payload, offset, room_id) ||
            !read_u64(payload, offset, player_id) ||
            !read_u16(payload, offset, token_size))
        {
            return false;
        }

        if (token_size == 0 ||
            token_size > Protocol::MAX_TOKEN_SIZE ||
            offset > payload.size() ||
            payload.size() - offset < token_size)
        {
            return false;
        }

        token = payload.substr(offset, token_size);
        return true;
    }

    std::string make_join_payload(
        std::uint32_t room_id,
        const std::string &player_name)
    {
        std::string payload;
        append_u32(payload, room_id);
        append_u16(
            payload,
            static_cast<std::uint16_t>(player_name.size()));
        payload.append(player_name);
        return payload;
    }

    std::string make_chat_payload(const std::string &message)
    {
        std::string payload;
        append_u16(
            payload,
            static_cast<std::uint16_t>(message.size()));
        payload.append(message);
        return payload;
    }

    std::string make_resume_payload(const std::string &token)
    {
        std::string payload;
        append_u16(
            payload,
            static_cast<std::uint16_t>(token.size()));
        payload.append(token);
        return payload;
    }

    Peer make_peer(EventLoop *loop)
    {
        int fds[2] = {-1, -1};

        if (::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            0,
            fds) != 0)
        {
            return {};
        }

        auto connection = std::make_shared<Connection>(loop, fds[0]);
        connection->connect_established();

        return Peer(fds[1], std::move(connection));
    }

    bool dispatch(
        RoomService &service,
        const Connection::ConnectionPtr &connection,
        MessageType type,
        const std::string &payload)
    {
        std::string frame;
        if (!Codec::encode(message_type(type), payload, frame))
        {
            return false;
        }

        Buffer input;
        input.append(frame);

        if (!service.handle_message(connection, input))
        {
            return false;
        }

        return input.empty();
    }

    bool dispatch_many(
        RoomService &service,
        const Connection::ConnectionPtr &connection,
        const std::vector<std::pair<MessageType, std::string>> &requests)
    {
        Buffer input;

        for (const auto &request : requests)
        {
            std::string frame;
            if (!Codec::encode(
                message_type(request.first),
                request.second,
                frame))
            {
                return false;
            }
            input.append(frame);
        }

        if (!service.handle_message(connection, input))
        {
            return false;
        }

        return input.empty();
    }

    bool receive_frames(Peer &peer, std::vector<Frame> &frames)
    {
        frames.clear();

        std::string bytes;
        char data[4096];

        while (true)
        {
            const ssize_t n = ::recv(
                peer.client_fd,
                data,
                sizeof(data),
                0);

            if (n > 0)
            {
                bytes.append(data, static_cast<std::size_t>(n));
                continue;
            }

            if (n == 0)
            {
                return false;
            }

            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

            return false;
        }

        if (bytes.empty())
        {
            return true;
        }

        Buffer input;
        input.append(bytes);

        const bool decoded = Codec::decode(
            input,
            [&frames](
            std::uint16_t type,
            const std::string &payload)
        {
            frames.push_back({type, payload});
            return true;
        });

        return decoded && input.empty();
    }

    bool receive_join_ok(
        Peer &peer,
        std::uint32_t expected_room_id,
        std::uint64_t expected_player_id,
        std::string &token)
    {
        std::vector<Frame> frames;

        if (!receive_frames(peer, frames))
        {
            return false;
        }

        if (frames.size() != 1 ||
            frames.front().type !=
            static_cast<std::uint16_t>(MessageType::join_ok))
        {
            return false;
        }

        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;

        return parse_join_ok(
            frames.front().payload,
            room_id,
            player_id,
            token) &&
            room_id == expected_room_id &&
            player_id == expected_player_id;
    }

    bool receive_resume_ok(
        Peer &peer,
        std::uint32_t expected_room_id,
        std::uint64_t expected_player_id)
    {
        std::vector<Frame> frames;

        if (!receive_frames(peer, frames))
        {
            return false;
        }

        if (frames.size() != 1 ||
            frames.front().type !=
            static_cast<std::uint16_t>(MessageType::resume_ok))
        {
            return false;
        }

        std::size_t offset = 0;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::uint16_t room_state = 0;
        std::uint64_t tick_id = 0;

        if (!read_u32(
            frames.front().payload,
            offset,
            room_id) ||
            !read_u64(
            frames.front().payload,
            offset,
            player_id) ||
            !read_u16(
            frames.front().payload,
            offset,
            room_state) ||
            !read_u64(
            frames.front().payload,
            offset,
            tick_id))
        {
            return false;
        }

        return
            room_id == expected_room_id &&
            player_id == expected_player_id &&
            room_state ==
            static_cast<std::uint16_t>(
                Roomstatemachine::States::running) &&
            tick_id == 0 &&
            offset < frames.front().payload.size();
    }

    bool expect_frames(
        Peer &peer,
        const std::vector<Frame> &expected)
    {
        std::vector<Frame> actual;
        if (!receive_frames(peer, actual))
        {
            return false;
        }

        if (actual.size() != expected.size())
        {
            std::cerr << "期望帧数 = " << expected.size()
                << "，实际帧数 = " << actual.size() << '\n';
            return false;
        }

        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            if (actual[i].type != expected[i].type ||
                actual[i].payload != expected[i].payload)
            {
                std::cerr << "第 " << i
                    << " 帧不符合预期\n";
                return false;
            }
        }

        return true;
    }

    bool test_two_player_lifecycle()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop, RECONNECT_TIMEOUT);
        REQUIRE(service.add_room(7, 2));

        Peer alice = make_peer(&loop);
        Peer bob = make_peer(&loop);
        REQUIRE(alice.valid());
        REQUIRE(bob.valid());

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::join,
            make_join_payload(7, "Alice")));

        std::string payload;

        std::string alice_token;

        REQUIRE(receive_join_ok(
            alice,
            7,
            1,
            alice_token));

        REQUIRE(alice_token.size() == SessionManager::TOKEN_SIZE);

        REQUIRE(expect_frames(bob, {}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        std::string bob_token;

        REQUIRE(receive_join_ok(
            bob,
            7,
            2,
            bob_token));

        REQUIRE(bob_token.size() == SessionManager::TOKEN_SIZE);

        REQUIRE(Protocol::encode_player_joined(
            7,
            2,
            "Bob",
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(
            MessageType::player_joined,
            payload)}));

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::chat,
            make_chat_payload("hello")));

        REQUIRE(Protocol::encode_chat_event(
            7,
            1,
            "hello",
            payload));

        const Frame chat_event =
            expected_frame(MessageType::chat_event, payload);

        REQUIRE(expect_frames(alice, {chat_event}));
        REQUIRE(expect_frames(bob, {chat_event}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::leave,
            ""));

        REQUIRE(Protocol::encode_leave_ok(7, payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::leave_ok, payload)}));

        REQUIRE(Protocol::encode_player_left(
            7,
            2,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(
            MessageType::player_left,
            payload)}));

        return true;
    }

    bool test_business_failures_do_not_change_membership()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop, RECONNECT_TIMEOUT);
        REQUIRE(service.add_room(7, 1));

        Peer alice = make_peer(&loop);
        Peer bob = make_peer(&loop);
        REQUIRE(alice.valid());
        REQUIRE(bob.valid());

        std::string payload;

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::chat,
            make_chat_payload("hello")));

        REQUIRE(Protocol::encode_error(
            MessageType::chat,
            ErrorCode::not_in_room,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(MessageType::error, payload)}));

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::join,
            make_join_payload(7, "Alice")));

        std::string alice_token;

        REQUIRE(receive_join_ok(
            alice,
            7,
            1,
            alice_token));

        REQUIRE(alice_token.size() == SessionManager::TOKEN_SIZE);

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::join,
            make_join_payload(7, "Alice")));

        REQUIRE(Protocol::encode_error(
            MessageType::join,
            ErrorCode::already_in_room,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(MessageType::error, payload)}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        REQUIRE(Protocol::encode_error(
            MessageType::join,
            ErrorCode::room_not_joinable,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::error, payload)}));

        REQUIRE(expect_frames(alice, {}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(99, "Bob")));

        REQUIRE(Protocol::encode_error(
            MessageType::join,
            ErrorCode::room_not_found,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::error, payload)}));

        REQUIRE(expect_frames(alice, {}));

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::leave,
            ""));

        REQUIRE(Protocol::encode_leave_ok(7, payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(MessageType::leave_ok, payload)}));

        REQUIRE(expect_frames(bob, {}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        REQUIRE(Protocol::encode_error(
            MessageType::join,
            ErrorCode::room_not_joinable,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::error, payload)}));

        REQUIRE(expect_frames(alice, {}));

        return true;
    }

    bool test_disconnect_resume_stale_close_and_timeout()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop, RECONNECT_TIMEOUT);
        REQUIRE(service.add_room(7, 2));

        Peer alice = make_peer(&loop);
        Peer bob = make_peer(&loop);
        Peer resumed_alice = make_peer(&loop);
        Peer duplicate_resume = make_peer(&loop);

        REQUIRE(alice.valid());
        REQUIRE(bob.valid());
        REQUIRE(resumed_alice.valid());
        REQUIRE(duplicate_resume.valid());

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::join,
            make_join_payload(7, "Alice")));

        std::string alice_token;

        REQUIRE(receive_join_ok(
            alice,
            7,
            1,
            alice_token));

        REQUIRE(alice_token.size() ==
            SessionManager::TOKEN_SIZE);

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        std::string bob_token;

        REQUIRE(receive_join_ok(
            bob,
            7,
            2,
            bob_token));

        REQUIRE(bob_token.size() ==
            SessionManager::TOKEN_SIZE);

        std::vector<Frame> ignored;
        REQUIRE(receive_frames(alice, ignored));

        // Connection A 断线，但玩家1仍留在房间中
        service.handle_connection_closed(
            alice.connection);

        REQUIRE(expect_frames(bob, {}));

        // 新连接恢复为原来的玩家1
        REQUIRE(dispatch(
            service,
            resumed_alice.connection,
            MessageType::resume,
            make_resume_payload(alice_token)));

        REQUIRE(receive_resume_ok(
            resumed_alice,
            7,
            1));

        // 在线 Session 不能被第二个连接同时恢复
        REQUIRE(dispatch(
            service,
            duplicate_resume.connection,
            MessageType::resume,
            make_resume_payload(alice_token)));

        std::string payload;

        REQUIRE(Protocol::encode_error(
            MessageType::resume,
            ErrorCode::session_online,
            payload));

        REQUIRE(expect_frames(
            duplicate_resume,
            {expected_frame(
            MessageType::error,
            payload)}));

        // Connection A 的重复旧关闭任务不能清除新连接
        service.handle_connection_closed(
            alice.connection);

        REQUIRE(expect_frames(
            resumed_alice,
            {}));

        REQUIRE(expect_frames(
            bob,
            {}));

        // 恢复后的连接继续使用原来的 player_id = 1
        REQUIRE(dispatch(
            service,
            resumed_alice.connection,
            MessageType::chat,
            make_chat_payload("after resume")));

        REQUIRE(Protocol::encode_chat_event(
            7,
            1,
            "after resume",
            payload));

        const Frame chat_event =
            expected_frame(
                MessageType::chat_event,
                payload);

        REQUIRE(expect_frames(
            resumed_alice,
            {chat_event}));

        REQUIRE(expect_frames(
            bob,
            {chat_event}));

        // 新连接再次断开，仍然先等待重连
        service.handle_connection_closed(
            resumed_alice.connection);

        REQUIRE(expect_frames(bob, {}));

        // 超时后才永久离开
        service.handle_session_timeouts(
            SessionManager::Clock::now() +
            RECONNECT_TIMEOUT +
            std::chrono::milliseconds(1));

        REQUIRE(Protocol::encode_player_left(
            7,
            1,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(
            MessageType::player_left,
            payload)}));

        // 永久清理以后，旧 token 已经失效
        REQUIRE(dispatch(
            service,
            duplicate_resume.connection,
            MessageType::resume,
            make_resume_payload(alice_token)));

        REQUIRE(Protocol::encode_error(
            MessageType::resume,
            ErrorCode::invalid_token,
            payload));

        REQUIRE(expect_frames(
            duplicate_resume,
            {expected_frame(
            MessageType::error,
            payload)}));

        return true;
    }

    bool test_coalesced_frames_keep_order()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop, RECONNECT_TIMEOUT);
        REQUIRE(service.add_room(7, 2));

        Peer alice = make_peer(&loop);
        REQUIRE(alice.valid());

        REQUIRE(dispatch_many(
            service,
            alice.connection,
            {
                {
                    MessageType::join,
                    make_join_payload(7, "Alice")
                },
                {
                    MessageType::chat,
                    make_chat_payload("hello")
                }
            }));

        std::vector<Frame> frames;
        REQUIRE(receive_frames(alice, frames));
        REQUIRE(frames.size() == 2);
        REQUIRE(frames[0].type == message_type(MessageType::join_ok));

        std::uint32_t join_ok_room_id = 0;
        std::uint64_t join_ok_player_id = 0;
        std::string alice_token;

        REQUIRE(parse_join_ok(
            frames[0].payload,
            join_ok_room_id,
            join_ok_player_id,
            alice_token));

        REQUIRE(join_ok_room_id == 7);
        REQUIRE(join_ok_player_id == 1);
        REQUIRE(alice_token.size() == SessionManager::TOKEN_SIZE);

        REQUIRE(frames[1].type == message_type(MessageType::chat_event));

        std::string chat_event_payload;
        REQUIRE(Protocol::encode_chat_event(
            7,
            1,
            "hello",
            chat_event_payload));
        REQUIRE(frames[1].payload == chat_event_payload);

        return true;
    }
}

int main()
{
    struct TestCase
    {
        const char *name;
        bool (*function)();
    };

    const std::vector<TestCase> tests =
    {
        {
            "two_player_lifecycle",
            test_two_player_lifecycle
        },
        {
            "business_failures_do_not_change_membership",
            test_business_failures_do_not_change_membership
        },
        {
            "disconnect_resume_stale_close_and_timeout",
            test_disconnect_resume_stale_close_and_timeout
        },
        {
            "coalesced_frames_keep_order",
            test_coalesced_frames_keep_order
        }
    };

    for (const auto &test : tests)
    {
        if (!test.function())
        {
            std::cerr << "[FAIL] " << test.name << '\n';
            return 1;
        }

        std::cout << "[PASS] " << test.name << '\n';
    }

    std::cout << "RoomService 业务验收通过\n";
    return 0;
}
