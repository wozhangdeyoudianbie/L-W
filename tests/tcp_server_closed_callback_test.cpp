#include "event_loop.h"
#include "tcp_server.h"
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

    bool check(bool expression, const char *text, int line)
    {
        if (expression)
        {
            return true;
        }
        std::cerr << "[FAIL] " << text << " at line " << line << '\n';
        return false;
    }

#define CHECK(expression) do { if (!check((expression), #expression, __LINE__)) return false; } while (false)

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

    bool send_and_half_close(int fd, const std::string &data)
    {
        return send_all(fd, data) && ::shutdown(fd, SHUT_WR) == 0;
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

    bool wait_until(const std::function<bool()> &condition, int timeout_ms)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return condition();
    }

    class ServerRunner
    {
    public:
        ServerRunner(
            std::size_t thread_count,
            TcpServer::ConnectionClosedCallback initial_callback = {},
            TcpServer::ConnectionClosedCallback late_callback = {})
            : thread_count_(thread_count), port_(find_free_port()), loop_(nullptr),
            ready_(false), start_success_(false),
            initial_callback_(std::move(initial_callback)),
            late_callback_(std::move(late_callback)),
            close_count_(0), message_count_(0),
            close_on_base_thread_(true), close_connection_nonnull_(true)
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

        std::size_t close_count() const
        {
            return close_count_.load();
        }

        std::size_t message_count() const
        {
            return message_count_.load();
        }

        bool close_on_base_thread() const
        {
            return close_on_base_thread_.load();
        }

        bool close_connection_nonnull() const
        {
            return close_connection_nonnull_.load();
        }

    private:
        void thread_func()
        {
            EventLoop base_loop;
            TcpServer server(&base_loop, port_, thread_count_);

            server.set_message_callback([this](const Connection::ConnectionPtr &, Buffer &buffer)
            {
                buffer.retrieve_all();
                message_count_.fetch_add(1);
                return true;
            });

            if (initial_callback_)
            {
                server.set_connection_closed_callback([this, &base_loop](const Connection::ConnectionPtr &connection)
                {
                    if (!base_loop.is_in_loop_thread())
                    {
                        close_on_base_thread_.store(false);
                    }
                    if (!connection)
                    {
                        close_connection_nonnull_.store(false);
                    }

                    close_count_.fetch_add(1);
                    initial_callback_(connection);
                });
            }

            bool started = server.start();
            if (started && late_callback_)
            {
                server.set_connection_closed_callback(late_callback_);
            }

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
        TcpServer::ConnectionClosedCallback initial_callback_;
        TcpServer::ConnectionClosedCallback late_callback_;
        std::atomic<std::size_t> close_count_;
        std::atomic<std::size_t> message_count_;
        std::atomic<bool> close_on_base_thread_;
        std::atomic<bool> close_connection_nonnull_;
    };

    bool optional_callback_does_not_block_close()
    {
        ServerRunner server(2);
        CHECK(server.start());

        int client = connect_client(server.port());
        CHECK(client != -1);
        CHECK(send_and_half_close(client, "optional"));
        CHECK(wait_for_peer_close(client, 5000));

        ::close(client);
        server.stop();

        CHECK(server.close_count() == 0);
        return true;
    }

    bool callback_runs_once_on_base_thread()
    {
        ServerRunner server(2, [](const Connection::ConnectionPtr &)
        {
        });
        CHECK(server.start());

        int client = connect_client(server.port());
        CHECK(client != -1);
        CHECK(send_and_half_close(client, "normal-close"));
        CHECK(wait_for_peer_close(client, 5000));
        CHECK(wait_until([&server]()
        {
            return server.close_count() == 1;
        }, 5000));

        ::close(client);
        server.stop();

        CHECK(server.close_count() == 1);
        CHECK(server.close_on_base_thread());
        CHECK(server.close_connection_nonnull());
        return true;
    }

    bool every_connection_notifies_exactly_once()
    {
        constexpr std::size_t CLIENT_COUNT = 20;

        ServerRunner server(3, [](const Connection::ConnectionPtr &)
        {
        });
        CHECK(server.start());

        std::vector<int> clients;
        clients.reserve(CLIENT_COUNT);

        for (std::size_t i = 0;i < CLIENT_COUNT;i++)
        {
            int client = connect_client(server.port());
            CHECK(client != -1);
            clients.push_back(client);
        }

        for (int client : clients)
        {
            CHECK(send_and_half_close(client, "many-connections"));
        }

        for (int client : clients)
        {
            CHECK(wait_for_peer_close(client, 5000));
            ::close(client);
        }

        CHECK(wait_until([&server]()
        {
            return server.close_count() == CLIENT_COUNT;
        }, 5000));

        server.stop();

        CHECK(server.close_count() == CLIENT_COUNT);
        CHECK(server.close_on_base_thread());
        CHECK(server.close_connection_nonnull());
        return true;
    }

    bool callback_exception_does_not_block_cleanup()
    {
        ServerRunner server(2, [](const Connection::ConnectionPtr &)
        {
            throw std::runtime_error("expected test exception");
        });
        CHECK(server.start());

        for (int i = 0;i < 2;i++)
        {
            int client = connect_client(server.port());
            CHECK(client != -1);
            CHECK(send_and_half_close(client, "throwing-callback"));
            CHECK(wait_for_peer_close(client, 5000));
            ::close(client);
        }

        CHECK(wait_until([&server]()
        {
            return server.close_count() == 2;
        }, 5000));

        server.stop();

        CHECK(server.close_count() == 2);
        CHECK(server.close_on_base_thread());
        return true;
    }

    bool setter_after_start_does_not_replace_callback()
    {
        std::atomic<bool> late_callback_called(false);

        TcpServer::ConnectionClosedCallback initial_callback = [](const Connection::ConnectionPtr &)
        {
        };
        TcpServer::ConnectionClosedCallback late_callback = [&late_callback_called](const Connection::ConnectionPtr &)
        {
            late_callback_called.store(true);
        };

        ServerRunner server(2, std::move(initial_callback), std::move(late_callback));
        CHECK(server.start());

        int client = connect_client(server.port());
        CHECK(client != -1);
        CHECK(send_and_half_close(client, "late-setter"));
        CHECK(wait_for_peer_close(client, 5000));
        CHECK(wait_until([&server]()
        {
            return server.close_count() == 1;
        }, 5000));

        ::close(client);
        server.stop();

        CHECK(server.close_count() == 1);
        CHECK(!late_callback_called.load());
        return true;
    }

    bool server_shutdown_does_not_emit_runtime_callback()
    {
        ServerRunner server(2, [](const Connection::ConnectionPtr &)
        {
        });
        CHECK(server.start());

        int client = connect_client(server.port());
        CHECK(client != -1);
        CHECK(send_all(client, "active-connection"));
        CHECK(wait_until([&server]()
        {
            return server.message_count() >= 1;
        }, 5000));

        server.stop();

        CHECK(wait_for_peer_close(client, 5000));
        ::close(client);

        CHECK(server.close_count() == 0);
        return true;
    }

}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"optional_callback_does_not_block_close", optional_callback_does_not_block_close},
        {"callback_runs_once_on_base_thread", callback_runs_once_on_base_thread},
        {"every_connection_notifies_exactly_once", every_connection_notifies_exactly_once},
        {"callback_exception_does_not_block_cleanup", callback_exception_does_not_block_cleanup},
        {"setter_after_start_does_not_replace_callback", setter_after_start_does_not_replace_callback},
        {"server_shutdown_does_not_emit_runtime_callback", server_shutdown_does_not_emit_runtime_callback}
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

    std::cout << "TcpServer 业务断线通知验收通过\n";
    return 0;
}
