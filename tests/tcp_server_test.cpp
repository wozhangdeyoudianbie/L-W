#include "event_loop.h"
#include "tcp_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

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

    bool send_and_half_close(int fd, const std::string &data)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
            ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
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
        return ::shutdown(fd, SHUT_WR) == 0;
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
            ready_(false), start_success_(false), repeated_start_rejected_(false)
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

        bool repeated_start_rejected() const
        {
            return repeated_start_rejected_;
        }

    private:
        void thread_func()
        {
            EventLoop base_loop;
            TcpServer server(&base_loop, port_, thread_count_);
            bool first_start = server.start();
            bool second_start = first_start ? server.start() : false;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &base_loop;
                start_success_ = first_start;
                repeated_start_rejected_ = first_start && !second_start && server.started();
                ready_ = true;
            }
            condition_.notify_one();

            if (first_start)
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
        bool repeated_start_rejected_;
    };

    bool null_base_rejected()
    {
        TcpServer server(nullptr, find_free_port(), 1);
        return !server.start() && !server.started();
    }

    bool wrong_thread_rejected()
    {
        EventLoop base_loop;
        TcpServer server(&base_loop, find_free_port(), 1);
        bool result = true;
        std::thread thread([&server, &result]()
        {
            result = server.start();
        });
        thread.join();
        return !result && !server.started();
    }

    bool accept_and_close(std::size_t thread_count, std::size_t client_count)
    {
        ServerRunner server(thread_count);
        if (!server.start() || !server.repeated_start_rejected())
        {
            return false;
        }

        std::vector<int> clients;
        clients.reserve(client_count);
        for (std::size_t i = 0;i < client_count;i++)
        {
            int fd = connect_client(server.port());
            if (fd == -1)
            {
                for (int client : clients)
                {
                    ::close(client);
                }
                server.stop();
                return false;
            }
            clients.push_back(fd);
        }

        bool success = true;
        for (int client : clients)
        {
            if (!send_and_half_close(client, "tcp-server-test"))
            {
                success = false;
            }
        }

        for (int client : clients)
        {
            if (!wait_for_peer_close(client, 5000))
            {
                success = false;
            }
            ::close(client);
        }

        server.stop();
        return success;
    }

    bool zero_worker_accept_and_close()
    {
        return accept_and_close(0, 1);
    }

    bool one_worker_accept_and_close()
    {
        return accept_and_close(1, 4);
    }

    bool three_worker_multiple_connections()
    {
        return accept_and_close(3, 24);
    }

    bool destructor_closes_active_connection()
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

        server.stop();
        bool closed = wait_for_peer_close(client, 5000);
        ::close(client);
        return closed;
    }

    bool repeated_construct_destroy()
    {
        for (int i = 0;i < 10;i++)
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

            server.stop();
            bool closed = wait_for_peer_close(client, 5000);
            ::close(client);
            if (!closed)
            {
                return false;
            }
        }
        return true;
    }

}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"null_base_rejected", null_base_rejected},
        {"wrong_thread_rejected", wrong_thread_rejected},
        {"zero_worker_accept_and_close", zero_worker_accept_and_close},
        {"one_worker_accept_and_close", one_worker_accept_and_close},
        {"three_worker_multiple_connections", three_worker_multiple_connections},
        {"destructor_closes_active_connection", destructor_closes_active_connection},
        {"repeated_construct_destroy", repeated_construct_destroy}
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

    std::cout << "TcpServer 基础验收通过\n";
    return 0;
}
