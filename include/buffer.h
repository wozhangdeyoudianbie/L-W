#ifndef BUFFER_H
#define BUFFER_H
#include<string>
#include<vector>
#include<cstddef>

class Buffer
{
public:
    explicit Buffer(std::size_t initial_size = 4096);   // 构造：分配初始缓冲区
    std::size_t readable_bytes() const;     // 可读字节数
    std::size_t writeable_bytes() const;    // 可写字节数
    std::size_t prependable_bytes() const;  // 头部预留字节数
    const char *peek() const;               // 可读区起始指针（只读不消费）
    bool empty() const;                     // 是否无可读数据
    void retrieve(std::size_t);             // 消费 N 字节（挪动读指针）
    void retrieve_all();                    // 消费全部
    std::string retrieve_all_as_string();   // 取走全部并返回字符串
    void append(const char *data, std::size_t len);   // 追加数据（原始字节）
    void append(const std::string &data);   // 追加数据（字符串）
private:
    void ensure_writable_bytes(std::size_t len);   // 确保可写空间
    void make_space(std::size_t len);       // 腾出可写空间
    std::vector<char> buffer_;
    std::size_t read_pos_;
    std::size_t write_pos_;
};

#endif
