#include "connection.h"
#include "logger.h"
#include <unistd.h>
#include <cerrno>
#include <ctime>
#include <utility>
#include "event_loop.h"
#include <sys/epoll.h>

Connection::Connection(EventLoop *loop, int fd)
    :loop_(loop), fd_(fd), state_(State::Connecting), registered_(false),
    close_(false), last_active_(std::time(nullptr)), peer_eof_(false)
{
}

Connection::~Connection()
{
    close_connection();
}

EventLoop *Connection::loop() const
{
    return loop_;
}

void Connection::connect_established()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ != State::Connecting)
    {
        return;
    }
    if (registered_)
    {
        return;
    }
    std::weak_ptr<Connection> weak_connection = weak_from_this();
    if (weak_connection.expired())
    {
        return;
    }
    auto event_callback = [weak_connection](uint32_t events)
    {
        if (auto connection = weak_connection.lock())
        {
            connection->handle_event(events);
        }
    };
    uint32_t events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    bool added = loop_->add_fd(fd_, events, std::move(event_callback));
    if (added)
    {
        registered_ = true;
        state_ = State::Connected;
        return;
    }
    handle_close();
}

void Connection::connect_destroyed()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ == State::Disconnected)
    {
        return;
    }
    if (registered_)
    {
        loop_->remove_fd(fd_);
        registered_ = false;
    }
    close_connection();
    state_ = State::Disconnected;
}

void Connection::handle_close()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ == State::Disconnecting || state_ == State::Disconnected)
    {
        return;
    }
    state_ = State::Disconnecting;
    if (close_callback_)
    {
        close_callback_(shared_from_this());
    }
}

void Connection::handle_event(uint32_t events)
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ != State::Connected)
    {
        return;
    }
    bool connection_success = true;
    if ((events & (EPOLLIN | EPOLLRDHUP)) != 0)
    {
        connection_success = read_from_socket();
    }
    if (connection_success && (events & EPOLLOUT))
    {
        connection_success = write_to_socket();
    }
    if (!connection_success || (events & (EPOLLERR | EPOLLHUP)) != 0)
    {
        handle_close();
        return;
    }
    if (peer_eof_ && write_buffer_.empty())
    {
        handle_close();
        return;
    }
    uint32_t need_events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    if (!write_buffer_.empty())
    {
        need_events |= EPOLLOUT;
    }
    if (!loop_->update_fd(fd_, need_events))
    {
        handle_close();
        return;
    }
}

void Connection::send(std::string data)
{
    if (!loop_ || data.empty())
    {
        return;
    }
    auto connection = shared_from_this();
    loop_->run_in_loop([connection, data]()
    {
        connection->send_in_loop(data);
    });
}

void Connection::send_in_loop(std::string data)
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ != State::Connected || data.empty())
    {
        return;
    }
    write_buffer_.append(data);
    if (!write_to_socket())
    {
        handle_close();
        return;
    }
    uint32_t need_events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    if (!write_buffer_.empty())
    {
        need_events |= EPOLLOUT;
    }
    if (!loop_->update_fd(fd_, need_events))
    {
        handle_close();
        return;
    }
}

void Connection::set_close_callback(CloseCallback callback)
{
    close_callback_ = std::move(callback);
}

void Connection::set_message_callback(MessageCallback callback)
{
    message_callback_ = std::move(callback);
}

int Connection::fd() const
{
    return fd_;
}

bool Connection::close() const
{
    return close_;
}

time_t Connection::last_active() const
{
    return last_active_;
}

Buffer &Connection::read_buffer()
{
    return read_buffer_;
}

const Buffer &Connection::read_buffer() const
{
    return read_buffer_;
}

const Buffer &Connection::write_buffer() const
{
    return write_buffer_;
}

bool Connection::peer_eof() const
{
    return peer_eof_;
}

void Connection::append_write_buffer(const std::string &data)
{
    write_buffer_.append(data);
}

void Connection::append_write_buffer(const char *data, std::size_t len)
{
    write_buffer_.append(data, len);
}

bool Connection::read_from_socket()
{
    char buffer[4096];
    while (1)
    {
        ssize_t n = read(fd_, buffer, sizeof(buffer));
        if (n > 0)
        {
            read_buffer_.append(buffer, n);
            last_active_ = std::time(nullptr);
        }
        else if (n == 0)
        {
            peer_eof_ = true;
            return true;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return true;
            }
            return false;
        }
    }
}

bool Connection::write_to_socket()
{
    while (!write_buffer_.empty())
    {
        ssize_t n = write(fd_, write_buffer_.peek(), write_buffer_.readable_bytes());
        if (n > 0)
        {
            write_buffer_.retrieve(n);
            last_active_ = std::time(nullptr);
        }
        else if (n == 0)
        {
            return false;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                Logger::get_instance().write_log(
                "INFO",
                "写缓冲区暂时不可写，fd = " + std::to_string(fd_) +
                "，剩余字节 = " + std::to_string(write_buffer_.readable_bytes()));
                return true;
            }
            // close_connection();
            return false;
        }
    }
    return true;
}

void Connection::close_connection()
{
    if (!close_)
    {
        ::close(fd_);
        close_ = true;
    }
}
