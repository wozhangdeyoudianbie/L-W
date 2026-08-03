#include "codec.h"
#include "event_loop.h"
#include "tcp_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

    void handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload)
    {
        std::string frame;
        if (!Codec::encode(type, payload, frame))
        {
            return;
        }
        connection->send(std::move(frame));
    }

    bool handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer)
    {
        Codec::FrameCallback frame_callback = [connection](std::uint16_t type, const std::string &payload)
        {
            handle_frame(connection, type, payload);
            return true;
        };
        return Codec::decode(buffer, frame_callback);
    }

    std::uint16_t find_free_port()
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1)
        {
            return 0;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
        {
            ::close(fd);
            return 0;
        }

        socklen_t address_length = sizeof(address);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &address_length) == -1)
        {
            ::close(fd);
            return 0;
        }

        std::uint16_t port = ntohs(address.sin_port);
        ::close(fd);
        return port;
    }

    int connect_client(std::uint16_t port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1)
        {
            return -1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
        {
            ::close(fd);
            return -1;
        }

        return fd;
    }

    bool send_all(int fd, const std::string &data)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
            ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
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

    bool receive_exact(int fd, std::size_t expected_size, std::string &received, int timeout_ms)
    {
        received.clear();
        received.reserve(expected_size);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (received.size() < expected_size)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                return false;
            }

            int remaining_time = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            if (remaining_time <= 0)
            {
                remaining_time = 1;
            }

            pollfd event{};
            event.fd = fd;
            event.events = POLLIN | POLLHUP | POLLERR;

            int result;
            do
            {
                result = ::poll(&event, 1, remaining_time);
            } while (result == -1 && errno == EINTR);

            if (result <= 0)
            {
                return false;
            }

            char buffer[4096];
            std::size_t remaining_bytes = expected_size - received.size();
            std::size_t read_size = remaining_bytes < sizeof(buffer) ? remaining_bytes : sizeof(buffer);
            ssize_t n = ::recv(fd, buffer, read_size, 0);
            if (n > 0)
            {
                received.append(buffer, static_cast<std::size_t>(n));
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

    bool wait_for_no_data(int fd, int timeout_ms)
    {
        pollfd event{};
        event.fd = fd;
        event.events = POLLIN | POLLHUP | POLLERR;

        int result;
        do
        {
            result = ::poll(&event, 1, timeout_ms);
        } while (result == -1 && errno == EINTR);

        return result == 0;
    }

    bool wait_for_peer_close(int fd, int timeout_ms)
    {
        pollfd event{};
        event.fd = fd;
        event.events = POLLIN | POLLHUP | POLLERR;

        int result;
        do
        {
            result = ::poll(&event, 1, timeout_ms);
        } while (result == -1 && errno == EINTR);

        if (result <= 0)
        {
            return false;
        }

        char byte;
        ssize_t n = ::recv(fd, &byte, sizeof(byte), 0);
        if (n == 0)
        {
            return true;
        }
        if (n == -1 && (errno == ECONNRESET || errno == ENOTCONN))
        {
            return true;
        }
        return false;
    }

    class ServerRunner
    {
    public:
        explicit ServerRunner(std::size_t thread_count)
            : thread_count_(thread_count), port_(find_free_port()), loop_(nullptr),
            ready_(false), start_success_(false)
        {
        }

        ~ServerRunner()
        {
            stop();
        }

        ServerRunner(const ServerRunner &) = delete;
        ServerRunner &operator=(const ServerRunner &) = delete;

        bool start()
        {
            if (port_ == 0 || thread_.joinable())
            {
                return false;
            }

            thread_ = std::thread(&ServerRunner::thread_func, this);
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]()
            {
                return ready_;
            });
            return start_success_;
        }

        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (loop_)
                {
                    loop_->quit();
                }
            }

            if (thread_.joinable())
            {
                thread_.join();
            }
        }

        std::uint16_t port() const
        {
            return port_;
        }

    private:
        void thread_func()
        {
            EventLoop base_loop;
            TcpServer server(&base_loop, port_, thread_count_);
            server.set_message_callback(handle_message);
            bool started = server.start();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &base_loop;
                start_success_ = started;
                ready_ = true;
            }
            condition_.notify_one();

            if (started)
            {
                base_loop.loop();
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = nullptr;
            }
        }

        std::size_t thread_count_;
        std::uint16_t port_;
        std::thread thread_;
        std::mutex mutex_;
        std::condition_variable condition_;
        EventLoop *loop_;
        bool ready_;
        bool start_success_;
    };

    bool single_frame_echo()
    {
        ServerRunner server(2);
        if (!server.start())
        {
            return false;
        }

        int client = connect_client(server.port());
        if (client == -1)
        {
            server.stop();
            return false;
        }

        std::string frame;
        std::string received;
        std::string payload("hello\0world", 11);
        bool success = Codec::encode(7, payload, frame);
        success = success && send_all(client, frame);
        success = success && receive_exact(client, frame.size(), received, 5000);
        success = success && received == frame;

        ::close(client);
        server.stop();
        return success;
    }

    bool half_frame_echo()
    {
        ServerRunner server(2);
        if (!server.start())
        {
            return false;
        }

        int client = connect_client(server.port());
        if (client == -1)
        {
            server.stop();
            return false;
        }

        std::string frame;
        std::string received;
        bool success = Codec::encode(8, "half-frame", frame);
        success = success && send_all(client, frame.substr(0, 3));
        success = success && wait_for_no_data(client, 200);
        success = success && send_all(client, frame.substr(3));
        success = success && receive_exact(client, frame.size(), received, 5000);
        success = success && received == frame;

        ::close(client);
        server.stop();
        return success;
    }

    bool multiple_frames_echo()
    {
        ServerRunner server(2);
        if (!server.start())
        {
            return false;
        }

        int client = connect_client(server.port());
        if (client == -1)
        {
            server.stop();
            return false;
        }

        std::string frame_a;
        std::string frame_b;
        std::string frame_c;
        bool success = Codec::encode(1, "A", frame_a);
        success = success && Codec::encode(2, "message-B", frame_b);
        success = success && Codec::encode(3, "", frame_c);

        std::string packet = frame_a + frame_b + frame_c;
        std::string received;
        success = success && send_all(client, packet);
        success = success && receive_exact(client, packet.size(), received, 5000);
        success = success && received == packet;

        ::close(client);
        server.stop();
        return success;
    }

    bool invalid_frame_closes_connection()
    {
        ServerRunner server(2);
        if (!server.start())
        {
            return false;
        }

        int client = connect_client(server.port());
        if (client == -1)
        {
            server.stop();
            return false;
        }

        std::uint32_t invalid_length = htonl(1);
        std::string invalid_frame(sizeof(invalid_length), '\0');
        std::memcpy(invalid_frame.data(), &invalid_length, sizeof(invalid_length));

        bool success = send_all(client, invalid_frame);
        success = success && wait_for_peer_close(client, 5000);

        ::close(client);
        server.stop();
        return success;
    }

}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"single_frame_echo", single_frame_echo},
        {"half_frame_echo", half_frame_echo},
        {"multiple_frames_echo", multiple_frames_echo},
        {"invalid_frame_closes_connection", invalid_frame_closes_connection}
    };

    for (const auto &test : tests)
    {
        bool passed = false;
        try
        {
            passed = test.second();
        }
        catch (const std::exception &exception)
        {
            std::cerr << "[FAIL] " << test.first << ": " << exception.what() << '\n';
            return 1;
        }
        catch (...)
        {
            std::cerr << "[FAIL] " << test.first << ": unknown exception\n";
            return 1;
        }

        if (!passed)
        {
            std::cerr << "[FAIL] " << test.first << '\n';
            return 1;
        }
        std::cout << "[PASS] " << test.first << '\n';
    }

    std::cout << "MessageCallback 集成验收通过\n";
    return 0;
}
