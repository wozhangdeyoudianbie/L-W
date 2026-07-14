#include "connection.h"
#include "logger.h"
#include <unistd.h>
#include <cerrno>
#include <ctime>

Connection::Connection(int fd)
    :fd_(fd), close_(false), last_active_(std::time(nullptr)), peer_eof_(false)
{
}

Connection::~Connection()
{
    close_connection();
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
