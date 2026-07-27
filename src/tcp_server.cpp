#include "tcp_server.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <cerrno>

TcpServer::TcpServer(EventLoop *base_loop, std::uint16_t port, std::size_t thread_count)
    :base_loop_(base_loop), port_(port), thread_pool_(base_loop, thread_count),
    listen_fd_(-1), listen_registered_(false), started_(false)
{
}

bool TcpServer::create_listen_socket()
{
    if (listen_fd_ != -1 || listen_registered_)
    {
        return false;
    }
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1)
    {
        return false;
    }
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        close_listen_socket();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
    {
        close_listen_socket();
        return false;
    }
    if (listen(listen_fd_, SOMAXCONN) == -1)
    {
        close_listen_socket();
        return false;
    }
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (flags == -1)
    {
        close_listen_socket();
        return false;
    }
    if (fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close_listen_socket();
        return false;
    }
    return true;
}

void TcpServer::close_listen_socket()
{
    if (listen_registered_)
    {
        base_loop_->remove_fd(listen_fd_);
        listen_registered_ = false;
    }
    if (listen_fd_ != -1)
    {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

bool TcpServer::start()
{
    if (!base_loop_ || !base_loop_->valid() || !base_loop_->is_in_loop_thread() || started_ || listen_fd_ != -1 || listen_registered_ || !message_callback_)
    {
        return false;
    }
    if (!create_listen_socket())
    {
        return false;
    }
    auto callback = [this](uint32_t events)
    {
        handle_accept(events);
    };
    if (!base_loop_->add_fd(listen_fd_, EPOLLIN | EPOLLET, std::move(callback)))
    {
        close_listen_socket();
        return false;
    }
    listen_registered_ = true;
    if (!thread_pool_.start())
    {
        close_listen_socket();
        return false;
    }
    started_ = true;
    return true;
}

bool TcpServer::started() const
{
    return started_;
}

void TcpServer::handle_accept(uint32_t events)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return;
    }
    if (!started_ || !listen_registered_ || listen_fd_ == -1)
    {
        return;
    }
    if ((events & EPOLLIN) == 0)
    {
        return;
    }
    while (true)
    {
        sockaddr_in client_address{};
        socklen_t client_address_length = sizeof(client_address);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_address), &client_address_length);
        if (client_fd == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            break;
        }
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags == -1)
        {
            ::close(client_fd);
            continue;
        }
        if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            close(client_fd);
            continue;
        }
        EventLoop *io_loop = thread_pool_.get_next_loop();
        if (!io_loop)
        {
            close(client_fd);
            continue;
        }
        if (connections_.find(client_fd) != connections_.end())
        {
            close(client_fd);
            continue;
        }
        auto connection = std::make_shared<Connection>(io_loop, client_fd);
        connection->set_close_callback([this](const Connection::ConnectionPtr &connection)
        {
            remove_connection(connection);
        });
        connection->set_message_callback(message_callback_);
        connections_[client_fd] = connection;
        io_loop->run_in_loop([connection]()
        {
            connection->connect_established();
        });
    }
}

void TcpServer::remove_connection(const Connection::ConnectionPtr &connection)
{
    if (!base_loop_ || !connection)
    {
        return;
    }
    base_loop_->run_in_loop([this, connection]()
    {
        remove_connection_in_loop(connection);
    });
}

void TcpServer::remove_connection_in_loop(const Connection::ConnectionPtr &connection)
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return;
    }
    if (!connection)
    {
        return;
    }
    auto it = connections_.find(connection->fd());
    if (it == connections_.end())
    {
        return;
    }
    if (it->second != connection)
    {
        return;
    }
    EventLoop *io_loop = connection->loop();
    if (!io_loop)
    {
        return;
    }
    connections_.erase(it);
    io_loop->queue_in_loop([connection]()
    {
        connection->connect_destroyed();
    });
}

void TcpServer::set_message_callback(Connection::MessageCallback callback)
{
    if (started_)
    {
        return;
    }
    message_callback_ = std::move(callback);
}

TcpServer::~TcpServer()
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return;
    }
    close_listen_socket();
    started_ = false;
    for (auto &it : connections_)
    {
        Connection::ConnectionPtr connection = it.second;
        EventLoop *io_loop = connection->loop();
        if (!io_loop)
        {
            continue;
        }
        io_loop->run_in_loop([connection]()
        {
            connection->connect_destroyed();
        });
    }

    connections_.clear();
}

