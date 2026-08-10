#include "room_service.h"
#include "codec.h"
#include "connection.h"
#include "event_loop.h"
#include "event_loop_thread.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::chrono::milliseconds TEST_TIMEOUT(5000);

    struct Frame
    {
        std::uint16_t type;
        std::string payload;
    };

    bool require(bool condition, const char *expression, const char *function_name, int line)
    {
        if (condition)
        {
            return true;
        }

        std::cerr << "[FAIL] " << function_name
            << "，line = " << line
            << "，expression = " << expression << '\n';

        return false;
    }

#define REQUIRE(condition)                                                   \
    do                                                                       \
    {                                                                        \
        if (!require((condition), #condition, __func__, __LINE__))           \
        {                                                                    \
            return false;                                                    \
        }                                                                    \
    } while (false)

    bool run_in_loop_and_wait(EventLoop *loop, EventLoop::Functor functor)
    {
        if (!loop || !functor)
        {
            return false;
        }

        auto done = std::make_shared<std::promise<void>>();
        std::future<void> future = done->get_future();

        loop->run_in_loop([done, functor = std::move(functor)]() mutable
        {
            functor();
            done->set_value();
        });

        return future.wait_for(TEST_TIMEOUT) == std::future_status::ready;
    }

    bool wait_until(const std::function<bool()> &predicate)
    {
        const Clock::time_point deadline = Clock::now() + TEST_TIMEOUT;

        while (Clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return predicate();
    }

    class LoopGate
    {
    public:
        LoopGate()
            :state_(std::make_shared<State>())
        {
        }

        ~LoopGate()
        {
            release();
        }

        LoopGate(const LoopGate &) = delete;
        LoopGate &operator=(const LoopGate &) = delete;

        bool block_after(EventLoop *loop, EventLoop::Functor functor)
        {
            if (!loop)
            {
                return false;
            }

            std::shared_ptr<State> state = state_;

            loop->queue_in_loop([state, functor = std::move(functor)]() mutable
            {
                if (functor)
                {
                    functor();
                }

                std::unique_lock<std::mutex> lock(state->mutex);
                state->entered = true;
                state->condition.notify_all();

                state->condition.wait(lock, [state]()
                {
                    return state->released;
                });
            });

            std::unique_lock<std::mutex> lock(state_->mutex);

            return state_->condition.wait_for(lock, TEST_TIMEOUT, [this]()
            {
                return state_->entered;
            });
        }

        void release()
        {
            std::lock_guard<std::mutex> lock(state_->mutex);

            if (state_->released)
            {
                return;
            }

            state_->released = true;
            state_->condition.notify_all();
        }

    private:
        struct State
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool entered = false;
            bool released = false;
        };

        std::shared_ptr<State> state_;
    };

    class ThreadPeer
    {
    public:
        ThreadPeer() = default;

        ~ThreadPeer()
        {
            stop();
        }

        ThreadPeer(const ThreadPeer &) = delete;
        ThreadPeer &operator=(const ThreadPeer &) = delete;

        bool start(int send_buffer_size = 0)
        {
            loop = thread.start_loop();

            if (!loop)
            {
                return false;
            }

            int fds[2] = {-1, -1};

            if (::socketpair(
                AF_UNIX,
                SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                0,
                fds) != 0)
            {
                return false;
            }

            if (send_buffer_size > 0 &&
                ::setsockopt(
                fds[0],
                SOL_SOCKET,
                SO_SNDBUF,
                &send_buffer_size,
                sizeof(send_buffer_size)) != 0)
            {
                ::close(fds[0]);
                ::close(fds[1]);
                return false;
            }

            client_fd = fds[1];
            connection = std::make_shared<Connection>(loop, fds[0]);

            Connection::ConnectionPtr current = connection;

            return run_in_loop_and_wait(loop, [current]()
            {
                current->connect_established();
            });
        }

        void stop()
        {
            if (connection && loop)
            {
                Connection::ConnectionPtr current = connection;

                run_in_loop_and_wait(loop, [current]()
                {
                    current->set_close_callback(Connection::CloseCallback());
                    current->set_message_callback(Connection::MessageCallback());
                    current->connect_destroyed();
                });

                connection.reset();
            }

            if (client_fd >= 0)
            {
                ::close(client_fd);
                client_fd = -1;
            }
        }

        EventLoopThread thread;
        EventLoop *loop = nullptr;
        int client_fd = -1;
        Connection::ConnectionPtr connection;
    };

    class RoomFixture
    {
    public:
        RoomFixture() = default;

        ~RoomFixture()
        {
            if (base_loop_)
            {
                run_in_loop_and_wait(base_loop_, []()
                {
                });
            }

            alice_.stop();
            bob_.stop();

            if (base_loop_)
            {
                run_in_loop_and_wait(base_loop_, []()
                {
                });
            }

            service_.reset();
        }

        RoomFixture(const RoomFixture &) = delete;
        RoomFixture &operator=(const RoomFixture &) = delete;

        bool start(std::uint32_t room_id, std::size_t capacity)
        {
            room_id_ = room_id;
            base_loop_ = base_thread_.start_loop();

            if (!base_loop_)
            {
                return false;
            }

            service_ = std::make_unique<RoomService>(base_loop_);

            bool room_added = false;

            if (!run_in_loop_and_wait(base_loop_, [this, &room_added, capacity]()
            {
                room_added = service_->add_room(room_id_, capacity);
            }) || !room_added)
            {
                return false;
            }

            if (!alice_.start() || !bob_.start())
            {
                return false;
            }

            return install_close_callback(
                alice_,
                alice_close_count_,
                alice_close_on_owner_) &&
                install_close_callback(
                bob_,
                bob_close_count_,
                bob_close_on_owner_);
        }

        bool sync_base()
        {
            return run_in_loop_and_wait(base_loop_, []()
            {
            });
        }

        bool install_close_callback(
            ThreadPeer &peer,
            std::atomic<int> &count,
            std::atomic<bool> &on_owner)
        {
            Connection::ConnectionPtr connection = peer.connection;

            return run_in_loop_and_wait(
                peer.loop,
                [this, connection, &count, &on_owner]()
            {
                connection->set_close_callback(
                    [this, &count, &on_owner](
                    const Connection::ConnectionPtr &current)
                {
                    const bool owner_thread =
                        current->loop() &&
                        current->loop()->is_in_loop_thread();

                    service_->handle_connection_closed(current);
                    current->connect_destroyed();

                    on_owner.store(owner_thread);
                    count.fetch_add(1);
                });
            });
        }

        std::uint32_t room_id_ = 0;
        EventLoopThread base_thread_;
        EventLoop *base_loop_ = nullptr;
        std::unique_ptr<RoomService> service_;
        std::atomic<int> alice_close_count_{0};
        std::atomic<int> bob_close_count_{0};
        std::atomic<bool> alice_close_on_owner_{false};
        std::atomic<bool> bob_close_on_owner_{false};
        ThreadPeer alice_;
        ThreadPeer bob_;
    };

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

    bool dispatch_now(
        RoomService &service,
        const Connection::ConnectionPtr &connection,
        MessageType type,
        const std::string &payload)
    {
        std::string frame;

        if (!Codec::encode(
            message_type(type),
            payload,
            frame))
        {
            return false;
        }

        Buffer input;
        input.append(frame);

        return service.handle_message(connection, input) &&
            input.empty();
    }

    bool dispatch_and_wait(
        RoomFixture &fixture,
        ThreadPeer &peer,
        MessageType type,
        const std::string &payload)
    {
        bool handled = false;

        if (!run_in_loop_and_wait(
            peer.loop,
            [&fixture, &peer, type, &payload, &handled]()
        {
            handled = dispatch_now(
                *fixture.service_,
                peer.connection,
                type,
                payload);
        }) || !handled)
        {
            return false;
        }

        return fixture.sync_base();
    }

    bool receive_frames(
        ThreadPeer &peer,
        std::vector<Frame> &frames)
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
                bytes.append(
                    data,
                    static_cast<std::size_t>(n));

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

            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
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
        ThreadPeer &peer,
        const std::vector<Frame> &expected)
    {
        std::vector<Frame> actual;

        if (!receive_frames(peer, actual))
        {
            return false;
        }

        if (actual.size() != expected.size())
        {
            std::cerr << "期望帧数 = "
                << expected.size()
                << "，实际帧数 = "
                << actual.size() << '\n';

            return false;
        }

        for (std::size_t i = 0;
            i < expected.size();
            ++i)
        {
            if (actual[i].type != expected[i].type ||
                actual[i].payload != expected[i].payload)
            {
                std::cerr << "第 "
                    << i
                    << " 帧不符合预期\n";

                return false;
            }
        }

        return true;
    }

    bool expect_types(
        ThreadPeer &peer,
        const std::vector<MessageType> &expected)
    {
        std::vector<Frame> actual;

        if (!receive_frames(peer, actual) ||
            actual.size() != expected.size())
        {
            return false;
        }

        for (std::size_t i = 0;
            i < expected.size();
            ++i)
        {
            if (actual[i].type != message_type(expected[i]))
            {
                return false;
            }
        }

        return true;
    }

    bool send_client_bytes(
        int fd,
        const std::string &data)
    {
        std::size_t sent = 0;

        while (sent < data.size())
        {
            const ssize_t n = ::send(
                fd,
                data.data() + sent,
                data.size() - sent,
                0);

            if (n > 0)
            {
                sent += static_cast<std::size_t>(n);
                continue;
            }

            if (n < 0 && errno == EINTR)
            {
                continue;
            }

            return false;
        }

        return true;
    }

    bool join_alice(RoomFixture &fixture)
    {
        if (!dispatch_and_wait(
            fixture,
            fixture.alice_,
            MessageType::join,
            make_join_payload(
            fixture.room_id_,
            "Alice")) ||
            !run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }))
        {
            return false;
        }

        std::string payload;
        std::vector<MemberInfo> members;

        return Protocol::encode_join_ok(
            fixture.room_id_,
            1,
            members,
            payload) &&
            expect_frames(
                fixture.alice_,
                {expected_frame(
            MessageType::join_ok,
            payload)});
    }

    bool join_bob(RoomFixture &fixture)
    {
        if (!dispatch_and_wait(
            fixture,
            fixture.bob_,
            MessageType::join,
            make_join_payload(
            fixture.room_id_,
            "Bob")) ||
            !run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }) ||
            !run_in_loop_and_wait(
                fixture.bob_.loop,
                []()
        {
        }))
        {
            return false;
        }

        std::string join_ok_payload;

        std::vector<MemberInfo> members =
        {
            {1, "Alice"}
        };

        if (!Protocol::encode_join_ok(
            fixture.room_id_,
            2,
            members,
            join_ok_payload) ||
            !expect_frames(
            fixture.bob_,
            {expected_frame(
            MessageType::join_ok,
            join_ok_payload)}))
        {
            return false;
        }

        std::string joined_payload;

        return Protocol::encode_player_joined(
            fixture.room_id_,
            2,
            "Bob",
            joined_payload) &&
            expect_frames(
                fixture.alice_,
                {expected_frame(
            MessageType::player_joined,
            joined_payload)});
    }

    bool test_reservation_boundaries_and_concurrent_producers()
    {
        EventLoopThread loop_thread;
        EventLoop *loop = loop_thread.start_loop();

        REQUIRE(loop != nullptr);

        Connection::ConnectionPtr connection =
            std::make_shared<Connection>(loop, -1);

        LoopGate first_gate;

        REQUIRE(first_gate.block_after(
            loop,
            EventLoop::Functor()));

        REQUIRE(!connection->send(""));
        REQUIRE(connection->pending_write_bytes() == 0);

        REQUIRE(connection->send(std::string(
            Connection::WRITE_HIGH_WATER_MARK - 1,
            'a')));

        REQUIRE(!connection->under_backpressure());

        REQUIRE(connection->send(std::string(1, 'b')));
        REQUIRE(connection->under_backpressure());

        REQUIRE(connection->send(std::string(
            Connection::WRITE_HARD_LIMIT -
            Connection::WRITE_HIGH_WATER_MARK,
            'c')));

        REQUIRE(
            connection->pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        const std::size_t full_debt =
            connection->pending_write_bytes();

        REQUIRE(!connection->send(std::string(1, 'd')));

        REQUIRE(!connection->send(std::string(
            Connection::WRITE_HARD_LIMIT + 1,
            'e')));

        REQUIRE(
            connection->pending_write_bytes() ==
            full_debt);

        first_gate.release();

        REQUIRE(run_in_loop_and_wait(loop, []()
        {
        }));

        REQUIRE(connection->pending_write_bytes() == 0);
        REQUIRE(!connection->under_backpressure());

        LoopGate second_gate;

        REQUIRE(second_gate.block_after(
            loop,
            EventLoop::Functor()));

        constexpr std::size_t producer_count = 8;
        constexpr std::size_t attempts_per_producer = 128;
        constexpr std::size_t chunk_size = 4096;

        std::atomic<std::size_t> accepted{0};
        std::vector<std::thread> producers;

        for (std::size_t i = 0;
            i < producer_count;
            ++i)
        {
            producers.emplace_back(
                [connection, &accepted]()
            {
                for (std::size_t j = 0;
                    j < attempts_per_producer;
                    ++j)
                {
                    if (connection->send(
                        std::string(
                        chunk_size,
                        'x')))
                    {
                        accepted.fetch_add(1);
                    }
                }
            });
        }

        for (std::thread &producer : producers)
        {
            producer.join();
        }

        REQUIRE(
            accepted.load() ==
            Connection::WRITE_HARD_LIMIT /
            chunk_size);

        REQUIRE(
            connection->pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        REQUIRE(!connection->send(
            std::string(chunk_size, 'y')));

        REQUIRE(
            connection->pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        second_gate.release();

        REQUIRE(run_in_loop_and_wait(loop, []()
        {
        }));

        REQUIRE(connection->pending_write_bytes() == 0);

        return true;
    }

    bool test_partial_write_keeps_reading_and_close_clears_debt()
    {
        ThreadPeer peer;

        REQUIRE(peer.start(4096));

        std::atomic<int> message_count{0};
        std::atomic<int> close_count{0};
        std::atomic<bool> message_ok{false};
        std::atomic<bool> close_on_owner{false};
        std::atomic<bool> zero_debt_on_close{false};

        Connection::ConnectionPtr connection =
            peer.connection;

        REQUIRE(run_in_loop_and_wait(
            peer.loop,
            [connection,
            &message_count,
            &message_ok,
            &close_count,
            &close_on_owner,
            &zero_debt_on_close]()
        {
            connection->set_message_callback(
                [&message_count, &message_ok](
                const Connection::ConnectionPtr &,
                Buffer &buffer)
            {
                message_ok.store(
                    buffer.retrieve_all_as_string() ==
                    "PING");

                message_count.fetch_add(1);

                return true;
            });

            connection->set_close_callback(
                [&close_count,
                &close_on_owner,
                &zero_debt_on_close](
                const Connection::ConnectionPtr &current)
            {
                close_on_owner.store(
                    current->loop() &&
                    current->loop()->is_in_loop_thread());

                zero_debt_on_close.store(
                    current->pending_write_bytes() == 0);

                current->connect_destroyed();
                close_count.fetch_add(1);
            });
        }));

        REQUIRE(connection->send(std::string(
            Connection::WRITE_HARD_LIMIT,
            's')));

        std::size_t buffered_bytes = 0;

        REQUIRE(run_in_loop_and_wait(
            peer.loop,
            [connection, &buffered_bytes]()
        {
            buffered_bytes =
                connection->write_buffer().readable_bytes();
        }));

        const std::size_t pending_bytes =
            connection->pending_write_bytes();

        REQUIRE(pending_bytes == buffered_bytes);
        REQUIRE(pending_bytes > 0);

        REQUIRE(
            pending_bytes <
            Connection::WRITE_HARD_LIMIT);

        REQUIRE(connection->under_backpressure());

        REQUIRE(send_client_bytes(
            peer.client_fd,
            "PING"));

        REQUIRE(wait_until([&message_count]()
        {
            return message_count.load() == 1;
        }));

        REQUIRE(message_ok.load());
        REQUIRE(close_count.load() == 0);

        connection->request_close();
        connection->request_close();

        REQUIRE(wait_until([&close_count]()
        {
            return close_count.load() == 1;
        }));

        REQUIRE(run_in_loop_and_wait(peer.loop, []()
        {
        }));

        REQUIRE(close_on_owner.load());
        REQUIRE(zero_debt_on_close.load());
        REQUIRE(connection->pending_write_bytes() == 0);
        REQUIRE(close_count.load() == 1);

        return true;
    }

    bool test_snapshot_drop_critical_close_and_connection_isolation()
    {
        RoomFixture fixture;

        REQUIRE(fixture.start(7, 2));
        REQUIRE(join_alice(fixture));
        REQUIRE(join_bob(fixture));

        LoopGate bob_gate;

        REQUIRE(bob_gate.block_after(
            fixture.bob_.loop,
            EventLoop::Functor()));

        REQUIRE(fixture.bob_.connection->send(
            std::string(
            Connection::WRITE_HIGH_WATER_MARK,
            's')));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HIGH_WATER_MARK);

        REQUIRE(
            fixture.bob_.connection->
            under_backpressure());

        REQUIRE(run_in_loop_and_wait(
            fixture.base_loop_,
            [&fixture]()
        {
            fixture.service_->handle_tick(1);
        }));

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        REQUIRE(expect_types(
            fixture.alice_,
            {MessageType::state_snapshot}));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HIGH_WATER_MARK);

        REQUIRE(
            fixture.bob_close_count_.load() == 0);

        const std::string first_message =
            "critical-at-high-water";

        std::string first_payload;
        std::string first_frame;

        REQUIRE(Protocol::encode_chat_event(
            fixture.room_id_,
            1,
            first_message,
            first_payload));

        REQUIRE(Codec::encode(
            message_type(MessageType::chat_event),
            first_payload,
            first_frame));

        REQUIRE(dispatch_and_wait(
            fixture,
            fixture.alice_,
            MessageType::chat,
            make_chat_payload(first_message)));

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::chat_event,
            first_payload)}));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HIGH_WATER_MARK +
            first_frame.size());

        REQUIRE(
            fixture.bob_close_count_.load() == 0);

        const std::size_t filler_size =
            Connection::WRITE_HARD_LIMIT -
            fixture.bob_.connection->
            pending_write_bytes();

        REQUIRE(filler_size > 0);

        REQUIRE(fixture.bob_.connection->send(
            std::string(filler_size, 'f')));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        const std::string second_message =
            "critical-at-hard-limit";

        std::string second_payload;

        REQUIRE(Protocol::encode_chat_event(
            fixture.room_id_,
            1,
            second_message,
            second_payload));

        REQUIRE(dispatch_and_wait(
            fixture,
            fixture.alice_,
            MessageType::chat,
            make_chat_payload(second_message)));

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::chat_event,
            second_payload)}));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        REQUIRE(
            fixture.bob_close_count_.load() == 0);

        bob_gate.release();

        REQUIRE(wait_until([&fixture]()
        {
            return fixture.bob_close_count_.load() == 1;
        }));

        REQUIRE(fixture.sync_base());

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        std::string left_payload;

        REQUIRE(Protocol::encode_player_left(
            fixture.room_id_,
            2,
            left_payload));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::player_left,
            left_payload)}));

        REQUIRE(
            fixture.bob_close_on_owner_.load());

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() == 0);

        REQUIRE(
            fixture.alice_close_count_.load() == 0);

        const std::string healthy_message =
            "alice-still-healthy";

        std::string healthy_payload;

        REQUIRE(Protocol::encode_chat_event(
            fixture.room_id_,
            1,
            healthy_message,
            healthy_payload));

        REQUIRE(dispatch_and_wait(
            fixture,
            fixture.alice_,
            MessageType::chat,
            make_chat_payload(healthy_message)));

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::chat_event,
            healthy_payload)}));

        return true;
    }

    bool test_join_ack_failure_still_broadcasts_join_and_leave()
    {
        RoomFixture fixture;

        REQUIRE(fixture.start(8, 3));
        REQUIRE(join_alice(fixture));

        LoopGate base_gate;

        REQUIRE(base_gate.block_after(
            fixture.base_loop_,
            EventLoop::Functor()));

        bool handled = false;

        const std::string join_payload =
            make_join_payload(
            fixture.room_id_,
            "Bob");

        LoopGate bob_gate;

        REQUIRE(bob_gate.block_after(
            fixture.bob_.loop,
            [&fixture, &join_payload, &handled]()
        {
            handled = dispatch_now(
                *fixture.service_,
                fixture.bob_.connection,
                MessageType::join,
                join_payload);
        }));

        REQUIRE(handled);

        REQUIRE(fixture.bob_.connection->send(
            std::string(
            Connection::WRITE_HARD_LIMIT,
            'j')));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        base_gate.release();

        REQUIRE(fixture.sync_base());

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        std::string joined_payload;

        REQUIRE(Protocol::encode_player_joined(
            fixture.room_id_,
            2,
            "Bob",
            joined_payload));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::player_joined,
            joined_payload)}));

        REQUIRE(
            fixture.bob_close_count_.load() == 0);

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        bob_gate.release();

        REQUIRE(wait_until([&fixture]()
        {
            return fixture.bob_close_count_.load() == 1;
        }));

        REQUIRE(fixture.sync_base());

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        std::string left_payload;

        REQUIRE(Protocol::encode_player_left(
            fixture.room_id_,
            2,
            left_payload));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::player_left,
            left_payload)}));

        REQUIRE(
            fixture.bob_close_on_owner_.load());

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() == 0);

        REQUIRE(
            fixture.alice_close_count_.load() == 0);

        return true;
    }

    bool test_leave_ack_failure_broadcasts_player_left_once()
    {
        RoomFixture fixture;

        REQUIRE(fixture.start(9, 3));
        REQUIRE(join_alice(fixture));
        REQUIRE(join_bob(fixture));

        LoopGate base_gate;

        REQUIRE(base_gate.block_after(
            fixture.base_loop_,
            EventLoop::Functor()));

        bool handled = false;

        LoopGate bob_gate;

        REQUIRE(bob_gate.block_after(
            fixture.bob_.loop,
            [&fixture, &handled]()
        {
            handled = dispatch_now(
                *fixture.service_,
                fixture.bob_.connection,
                MessageType::leave,
                "");
        }));

        REQUIRE(handled);

        REQUIRE(fixture.bob_.connection->send(
            std::string(
            Connection::WRITE_HARD_LIMIT,
            'l')));

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() ==
            Connection::WRITE_HARD_LIMIT);

        base_gate.release();

        REQUIRE(fixture.sync_base());

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        std::string left_payload;

        REQUIRE(Protocol::encode_player_left(
            fixture.room_id_,
            2,
            left_payload));

        REQUIRE(expect_frames(
            fixture.alice_,
            {expected_frame(
            MessageType::player_left,
            left_payload)}));

        REQUIRE(
            fixture.bob_close_count_.load() == 0);

        bob_gate.release();

        REQUIRE(wait_until([&fixture]()
        {
            return fixture.bob_close_count_.load() == 1;
        }));

        REQUIRE(fixture.sync_base());

        REQUIRE(run_in_loop_and_wait(
            fixture.alice_.loop,
            []()
        {
        }));

        REQUIRE(expect_frames(
            fixture.alice_,
            {}));

        REQUIRE(
            fixture.bob_close_on_owner_.load());

        REQUIRE(
            fixture.bob_.connection->
            pending_write_bytes() == 0);

        REQUIRE(
            fixture.alice_close_count_.load() == 0);

        return true;
    }
}

int main()
{
    const std::vector<
        std::pair<const char *, bool (*)()>>
        tests =
    {
        {
            "reservation boundaries and concurrent producers",
            test_reservation_boundaries_and_concurrent_producers
        },
        {
            "partial write, continued read and debt cleanup",
            test_partial_write_keeps_reading_and_close_clears_debt
        },
        {
            "snapshot drop, critical close and connection isolation",
            test_snapshot_drop_critical_close_and_connection_isolation
        },
        {
            "join acknowledgement failure consistency",
            test_join_ack_failure_still_broadcasts_join_and_leave
        },
        {
            "leave acknowledgement failure consistency",
            test_leave_ack_failure_broadcasts_player_left_once
        }
    };

    for (const auto &test : tests)
    {
        std::cout << "[RUN] "
            << test.first << '\n';

        if (!test.second())
        {
            return 1;
        }

        std::cout << "[PASS] "
            << test.first << '\n';
    }

    std::cout
        << "[PASS] backpressure stage acceptance\n";

    return 0;
}
