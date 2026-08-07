#include "room_service.h"
#include "codec.h"
#include "connection.h"
#include "event_loop.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <cerrno>
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

        RoomService service(&loop);
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
        std::vector<MemberInfo> members;

        REQUIRE(Protocol::encode_join_ok(
            7,
            1,
            members,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(MessageType::join_ok, payload)}));

        REQUIRE(expect_frames(bob, {}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        members.push_back({1, "Alice"});

        REQUIRE(Protocol::encode_join_ok(
            7,
            2,
            members,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::join_ok, payload)}));

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

        RoomService service(&loop);
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

        std::vector<MemberInfo> members;

        REQUIRE(Protocol::encode_join_ok(
            7,
            1,
            members,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(MessageType::join_ok, payload)}));

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

    bool test_disconnect_cleans_membership_once()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop);
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

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::join,
            make_join_payload(7, "Bob")));

        std::vector<Frame> ignored;
        REQUIRE(receive_frames(alice, ignored));
        REQUIRE(receive_frames(bob, ignored));

        service.handle_connection_closed(bob.connection);

        std::string payload;
        REQUIRE(Protocol::encode_player_left(
            7,
            2,
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(
            MessageType::player_left,
            payload)}));

        REQUIRE(expect_frames(bob, {}));

        service.handle_connection_closed(bob.connection);

        REQUIRE(expect_frames(alice, {}));
        REQUIRE(expect_frames(bob, {}));

        REQUIRE(dispatch(
            service,
            alice.connection,
            MessageType::chat,
            make_chat_payload("still here")));

        REQUIRE(Protocol::encode_chat_event(
            7,
            1,
            "still here",
            payload));

        REQUIRE(expect_frames(
            alice,
            {expected_frame(
            MessageType::chat_event,
            payload)}));

        REQUIRE(expect_frames(bob, {}));

        REQUIRE(dispatch(
            service,
            bob.connection,
            MessageType::chat,
            make_chat_payload("am I here")));

        REQUIRE(Protocol::encode_error(
            MessageType::chat,
            ErrorCode::not_in_room,
            payload));

        REQUIRE(expect_frames(
            bob,
            {expected_frame(MessageType::error, payload)}));

        return true;
    }

    bool test_coalesced_frames_keep_order()
    {
        EventLoop loop;
        REQUIRE(loop.valid());

        RoomService service(&loop);
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

        std::string join_ok_payload;
        std::string chat_event_payload;
        std::vector<MemberInfo> members;

        REQUIRE(Protocol::encode_join_ok(
            7,
            1,
            members,
            join_ok_payload));

        REQUIRE(Protocol::encode_chat_event(
            7,
            1,
            "hello",
            chat_event_payload));

        REQUIRE(expect_frames(
            alice,
            {
                expected_frame(
                    MessageType::join_ok,
                    join_ok_payload),
                    expected_frame(
                        MessageType::chat_event,
                        chat_event_payload)
            }));

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
            "disconnect_cleans_membership_once",
            test_disconnect_cleans_membership_once
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
