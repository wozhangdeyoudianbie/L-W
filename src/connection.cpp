#include "connection.h"
#include "logger.h"
#include <unistd.h>
#include <cerrno>
#include <ctime>
#include <utility>
#include "event_loop.h"
#include <sys/epoll.h>

// 构造：连接状态机初始化
Connection::Connection(EventLoop *loop, int fd)
    :loop_(loop), fd_(fd), state_(State::Connecting), registered_(false),
    close_(false), last_peer_activity_time_(Clock::now()), peer_eof_(false)
{
}

// 析构：关闭连接
Connection::~Connection()
{
    close_connection();
}

//重新刷新活性时间
void Connection::refresh_peer_activity()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ != State::Connected)
    {
        return;
    }
    last_peer_activity_time_ = Clock::now();
}

// 异步请求所属 loop 检查超时（任意线程可调用）
void Connection::check_timeout(std::chrono::milliseconds timeout)
{
    if (!loop_ || timeout.count() <= 0)
    {
        return;
    }
    std::shared_ptr<Connection> connection = weak_from_this().lock();
    if (!connection)
    {
        return;
    }
    loop_->queue_in_loop([connection, timeout]()
    {
        connection->check_timeout_in_loop(timeout);
    });
}

// 在 loop 线程检查超时：超过阈值未刷新活性则走关闭流程
void Connection::check_timeout_in_loop(std::chrono::milliseconds timeout)
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return;
    }
    if (state_ != State::Connected)
    {
        return;
    }
    if (Clock::now() - last_peer_activity_time_ >= timeout)
    {
        handle_close();
    }
}

// 查询：所属事件循环
EventLoop *Connection::loop() const
{
    return loop_;
}

// 注册 fd 到事件循环
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

// 注销 fd 并关闭
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

// 关闭流程：通知上层
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

// 事件入口：读/写分发
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
    if ((events & (EPOLLIN | EPOLLRDHUP)) != 0)
    {
        if (!read_from_socket())
        {
            handle_close();
            return;
        }
        if (!message_callback_ || !message_callback_(shared_from_this(), read_buffer_))
        {
            handle_close();
            return;
        }
        if (state_ != State::Connected)
        {
            return;
        }
    }
    bool connection_success = true;
    if ((events & EPOLLOUT) != 0)
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

// 线程安全发送
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

// 在 loop 线程执行发送
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

// 设置关闭回调
void Connection::set_close_callback(CloseCallback callback)
{
    close_callback_ = std::move(callback);
}

// 设置消息回调
void Connection::set_message_callback(MessageCallback callback)
{
    message_callback_ = std::move(callback);
}

// 查询：文件描述符
int Connection::fd() const
{
    return fd_;
}

// 查询：是否已关闭
bool Connection::close() const
{
    return close_;
}

// 读缓冲区
Buffer &Connection::read_buffer()
{
    return read_buffer_;
}

// 读缓冲区（只读）
const Buffer &Connection::read_buffer() const
{
    return read_buffer_;
}

// 写缓冲区（只读）
const Buffer &Connection::write_buffer() const
{
    return write_buffer_;
}

// 查询：对端关闭
bool Connection::peer_eof() const
{
    return peer_eof_;
}

// 追加待发送数据（字符串）
void Connection::append_write_buffer(const std::string &data)
{
    write_buffer_.append(data);
}

// 追加待发送数据（原始字节）
void Connection::append_write_buffer(const char *data, std::size_t len)
{
    write_buffer_.append(data, len);
}

// 读 socket 到缓冲区
bool Connection::read_from_socket()
{
    char buffer[4096];
    while (1)
    {
        ssize_t n = read(fd_, buffer, sizeof(buffer));
        if (n > 0)
        {
            read_buffer_.append(buffer, n);
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

// 写缓冲区到 socket
bool Connection::write_to_socket()
{
    while (!write_buffer_.empty())
    {
        ssize_t n = write(fd_, write_buffer_.peek(), write_buffer_.readable_bytes());
        if (n > 0)
        {
            write_buffer_.retrieve(n);
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

// 关闭底层 fd
void Connection::close_connection()
{
    if (!close_)
    {
        ::close(fd_);
        close_ = true;
    }
}
