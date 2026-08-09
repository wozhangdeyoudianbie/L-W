#include "buffer.h"
#include "codec.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        if (!(condition))                                               \
        {                                                               \
            std::cerr << "CHECK failed: " << #condition                 \
                      << " at " << __FILE__ << ":" << __LINE__          \
                      << std::endl;                                     \
            std::abort();                                               \
        }                                                               \
    } while (false)

namespace
{
    using Clock = std::chrono::steady_clock;

    const std::uint16_t SERVER_PORT = 8080;

    struct Frame
    {
        std::uint16_t type;
        std::string payload;
    };

    struct Client
    {
        int fd = -1;
        bool closed = false;
        Buffer read_buffer;

        ~Client()
        {
            if (fd != -1)
            {
                ::close(fd);
            }
        }
    };

    void append_u16(std::string &data, std::uint16_t value)
    {
        const std::uint16_t network_value = htons(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    void append_u32(std::string &data, std::uint32_t value)
    {
        const std::uint32_t network_value = htonl(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    void append_u64(std::string &data, std::uint64_t value)
    {
        append_u32(data, static_cast<std::uint32_t>(value >> 32));
        append_u32(data, static_cast<std::uint32_t>(value & 0xffffffffULL));
    }

    bool read_u64(const std::string &data, std::uint64_t &value)
    {
        if (data.size() != sizeof(std::uint64_t))
        {
            return false;
        }
        std::uint32_t network_high = 0;
        std::uint32_t network_low = 0;
        std::memcpy(&network_high, data.data(), sizeof(network_high));
        std::memcpy(&network_low, data.data() + 4, sizeof(network_low));
        const std::uint32_t high = ntohl(network_high);
        const std::uint32_t low = ntohl(network_low);
        value = (static_cast<std::uint64_t>(high) << 32) | low;
        return true;
    }

    std::string make_heartbeat_payload(std::uint64_t seq)
    {
        std::string payload;
        append_u64(payload, seq);
        return payload;
    }

    std::string make_join_payload(std::uint32_t room_id, const std::string &name)
    {
        std::string payload;
        append_u32(payload, room_id);
        append_u16(payload, static_cast<std::uint16_t>(name.size()));
        payload.append(name);
        return payload;
    }

    std::unique_ptr<Client> connect_client()
    {
        std::unique_ptr<Client> client = std::make_unique<Client>();
        client->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client->fd == -1)
        {
            return nullptr;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1)
        {
            return nullptr;
        }
        if (connect(client->fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
        {
            return nullptr;
        }
        return client;
    }

    bool send_all(int fd, const char *data, std::size_t size)
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            const ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
            if (n > 0)
            {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            if (n == -1 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        return true;
    }

    bool send_frame(Client &client, MessageType type, const std::string &payload)
    {
        std::string frame;
        if (!Codec::encode(static_cast<std::uint16_t>(type), payload, frame))
        {
            return false;
        }
        return send_all(client.fd, frame.data(), frame.size());
    }

    bool receive_once(Client &client, int timeout_ms, std::vector<Frame> &frames)
    {
        pollfd descriptor{};
        descriptor.fd = client.fd;
        descriptor.events = POLLIN | POLLHUP | POLLERR;
        int result = 0;
        do
        {
            result = poll(&descriptor, 1, timeout_ms);
        } while (result == -1 && errno == EINTR);
        if (result == 0)
        {
            return true;
        }
        if (result == -1)
        {
            return false;
        }
        char data[8192];
        ssize_t n = 0;
        do
        {
            n = recv(client.fd, data, sizeof(data), 0);
        } while (n == -1 && errno == EINTR);
        if (n == 0)
        {
            client.closed = true;
            return true;
        }
        if (n < 0)
        {
            return false;
        }
        client.read_buffer.append(data, static_cast<std::size_t>(n));
        return Codec::decode(client.read_buffer, [&frames](std::uint16_t type, const std::string &payload)
        {
            frames.push_back(Frame{type, payload});
            return true;
        });
    }

    bool wait_for_frame(Client &client, MessageType expected_type, std::string &payload, int timeout_ms)
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!client.closed && Clock::now() < deadline)
        {
            int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()).count());
            if (remaining < 1)
            {
                remaining = 1;
            }
            std::vector<Frame> frames;
            if (!receive_once(client, remaining, frames))
            {
                return false;
            }
            for (const Frame &frame : frames)
            {
                if (frame.type == static_cast<std::uint16_t>(expected_type))
                {
                    payload = frame.payload;
                    return true;
                }
            }
        }
        return false;
    }

    bool wait_for_close(Client &client, int timeout_ms)
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!client.closed && Clock::now() < deadline)
        {
            int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()).count());
            if (remaining < 1)
            {
                remaining = 1;
            }
            std::vector<Frame> frames;
            if (!receive_once(client, remaining, frames))
            {
                return false;
            }
        }
        return client.closed;
    }

    bool expect_heartbeat_ack(Client &client, std::uint64_t seq, int timeout_ms)
    {
        if (!send_frame(client, MessageType::heartbeat, make_heartbeat_payload(seq)))
        {
            return false;
        }
        std::string payload;
        if (!wait_for_frame(client, MessageType::heartbeat_ack, payload, timeout_ms))
        {
            return false;
        }
        std::uint64_t ack_seq = 0;
        return read_u64(payload, ack_seq) && ack_seq == seq;
    }

    void test_heartbeat_ack()
    {
        std::unique_ptr<Client> client = connect_client();
        CHECK(client);
        CHECK(expect_heartbeat_ack(*client, 0, 2000));
        CHECK(expect_heartbeat_ack(*client, std::numeric_limits<std::uint64_t>::max(), 2000));
        std::cout << "[PASS] heartbeat_ack_preserves_seq" << std::endl;
    }

    void test_malformed_heartbeat_closes()
    {
        std::unique_ptr<Client> client = connect_client();
        CHECK(client);
        CHECK(send_frame(*client, MessageType::heartbeat, std::string(1, '\x01')));
        CHECK(wait_for_close(*client, 3000));
        std::cout << "[PASS] malformed_heartbeat_closes" << std::endl;
    }

    void test_unknown_type_closes()
    {
        std::unique_ptr<Client> client = connect_client();
        CHECK(client);
        std::string frame;
        CHECK(Codec::encode(999, "", frame));
        CHECK(send_all(client->fd, frame.data(), frame.size()));
        CHECK(wait_for_close(*client, 3000));
        std::cout << "[PASS] unknown_type_closes" << std::endl;
    }

    void test_liveness_paths()
    {
        std::unique_ptr<Client> silent_client = connect_client();
        std::unique_ptr<Client> partial_client = connect_client();
        std::unique_ptr<Client> heartbeat_client = connect_client();
        std::unique_ptr<Client> business_client = connect_client();
        CHECK(silent_client);
        CHECK(partial_client);
        CHECK(heartbeat_client);
        CHECK(business_client);

        std::string heartbeat_frame;
        CHECK(Codec::encode(
            static_cast<std::uint16_t>(MessageType::heartbeat),
            make_heartbeat_payload(1),
            heartbeat_frame));
        CHECK(send_all(partial_client->fd, heartbeat_frame.data(), 1));

        std::vector<std::unique_ptr<Client>> tick_clients;
        for (int i = 0; i < 4; ++i)
        {
            std::unique_ptr<Client> client = connect_client();
            CHECK(client);
            CHECK(send_frame(
                *client,
                MessageType::join,
                make_join_payload(1, "tick_player_" + std::to_string(i))));
            tick_clients.push_back(std::move(client));
        }

        std::string snapshot_payload;
        CHECK(wait_for_frame(
            *tick_clients.front(),
            MessageType::state_snapshot,
            snapshot_payload,
            4000));

        Clock::time_point next_activity = Clock::now();
        for (std::uint64_t seq = 1; seq <= 7; ++seq)
        {
            CHECK(expect_heartbeat_ack(*heartbeat_client, seq, 1500));
            CHECK(send_frame(*business_client, MessageType::leave, ""));
            next_activity += std::chrono::seconds(2);
            std::this_thread::sleep_until(next_activity);
        }

        CHECK(wait_for_close(*silent_client, 3000));
        CHECK(wait_for_close(*partial_client, 3000));
        for (const auto &client : tick_clients)
        {
            CHECK(wait_for_close(*client, 3000));
        }

        CHECK(expect_heartbeat_ack(*heartbeat_client, 100, 2000));
        CHECK(expect_heartbeat_ack(*business_client, 200, 2000));

        std::cout << "[PASS] silent_connection_times_out" << std::endl;
        std::cout << "[PASS] incomplete_frame_does_not_refresh" << std::endl;
        std::cout << "[PASS] heartbeat_keeps_connection_alive" << std::endl;
        std::cout << "[PASS] recognized_business_frame_refreshes" << std::endl;
        std::cout << "[PASS] server_snapshots_do_not_refresh" << std::endl;
    }

    void test_repeated_connection_lifecycle()
    {
        for (int i = 0; i < 20; ++i)
        {
            std::unique_ptr<Client> client = connect_client();
            CHECK(client);
        }
        std::cout << "[PASS] repeated_connection_lifecycle" << std::endl;
    }
}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    test_heartbeat_ack();
    test_malformed_heartbeat_closes();
    test_unknown_type_closes();
    test_liveness_paths();
    test_repeated_connection_lifecycle();

    std::cout << "Heartbeat/timeout 真实入口功能验证通过" << std::endl;
    return 0;
}
