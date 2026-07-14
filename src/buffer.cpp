#include "buffer.h"
#include <cassert>
#include <algorithm>

Buffer::Buffer(std::size_t initial_size)
    :buffer_(initial_size), read_pos_(0), write_pos_(0)
{
}

std::size_t Buffer::readable_bytes() const
{
    return write_pos_ - read_pos_;
}

std::size_t Buffer::writeable_bytes() const
{
    return buffer_.size() - write_pos_;
}

std::size_t Buffer::prependable_bytes() const
{
    return read_pos_;
}

bool Buffer::empty() const
{
    return read_pos_ == write_pos_;
}

const char *Buffer::peek() const
{
    return buffer_.data() + read_pos_;
}

void Buffer::retrieve(std::size_t len)
{
    assert(len <= readable_bytes());
    if (len < readable_bytes())
    {
        read_pos_ += len;
    }
    else
    {
        retrieve_all();
    }
}

void Buffer::retrieve_all()
{
    read_pos_ = 0;
    write_pos_ = 0;
}

std::string Buffer::retrieve_all_as_string()
{
    std::string result(peek(), readable_bytes());
    retrieve_all();
    return result;
}

void Buffer::make_space(std::size_t len)
{
    if (writeable_bytes() + prependable_bytes() < len)
    {
        buffer_.resize(write_pos_ + len);
    }
    else
    {
        std::size_t readable = readable_bytes();
        std::copy(buffer_.begin() + read_pos_, buffer_.begin() + write_pos_, buffer_.begin());
        read_pos_ = 0;
        write_pos_ = readable;
    }
}
void Buffer::ensure_writable_bytes(std::size_t len)
{
    if (writeable_bytes() >= len)
    {
        return;
    }
    make_space(len);
}

void Buffer::append(const char *data, std::size_t len)
{
    ensure_writable_bytes(len);
    std::copy(data, data + len, buffer_.data() + write_pos_);
    write_pos_ += len;
}

void Buffer::append(const std::string &data)
{
    append(data.data(), data.size());
}
