#include "buffer.h"
#include <cassert>
#include <algorithm>

// 构造：分配初始缓冲区
Buffer::Buffer(std::size_t initial_size)
    :buffer_(initial_size), read_pos_(0), write_pos_(0)
{
}

// 可读字节数
std::size_t Buffer::readable_bytes() const
{
    return write_pos_ - read_pos_;
}

// 可写字节数
std::size_t Buffer::writeable_bytes() const
{
    return buffer_.size() - write_pos_;
}

// 头部预留字节数
std::size_t Buffer::prependable_bytes() const
{
    return read_pos_;
}

// 是否无可读数据
bool Buffer::empty() const
{
    return read_pos_ == write_pos_;
}

// 可读区起始指针
const char *Buffer::peek() const
{
    return buffer_.data() + read_pos_;
}

// 消费 N 字节
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

// 消费全部
void Buffer::retrieve_all()
{
    read_pos_ = 0;
    write_pos_ = 0;
}

// 取走全部并返回字符串
std::string Buffer::retrieve_all_as_string()
{
    std::string result(peek(), readable_bytes());
    retrieve_all();
    return result;
}

// 腾出可写空间
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
// 确保可写空间
void Buffer::ensure_writable_bytes(std::size_t len)
{
    if (writeable_bytes() >= len)
    {
        return;
    }
    make_space(len);
}

// 追加数据（原始字节）
void Buffer::append(const char *data, std::size_t len)
{
    ensure_writable_bytes(len);
    std::copy(data, data + len, buffer_.data() + write_pos_);
    write_pos_ += len;
}

// 追加数据（字符串）
void Buffer::append(const std::string &data)
{
    append(data.data(), data.size());
}
