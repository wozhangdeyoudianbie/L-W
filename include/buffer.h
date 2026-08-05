#ifndef BUFFER_H
#define BUFFER_H
#include<string>
#include<vector>
#include<cstddef>

class Buffer
{
public:
    explicit Buffer(std::size_t initial_size = 4096);
    std::size_t readable_bytes() const;     // 可读字节数
    std::size_t writeable_bytes() const;    // 可写字节数
    std::size_t prependable_bytes() const;  // 头部预留字节数
    const char *peek() const;               // 可读区起始指针（只读不消费）
    bool empty() const;                     // 是否无可读数据
    void retrieve(std::size_t);             // 消费 N 字节（挪动读指针）
    void retrieve_all();
    std::string retrieve_all_as_string();
    void append(const char *data, std::size_t len);
    void append(const std::string &data);
private:
    void ensure_writable_bytes(std::size_t len);
    void make_space(std::size_t len);
    std::vector<char> buffer_;
    std::size_t read_pos_;
    std::size_t write_pos_;
};

#endif
