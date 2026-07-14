#ifndef BUFFER_H
#define BUFFER_H
#include<string>
#include<vector>
#include<cstddef>

class Buffer
{
public:
    explicit Buffer(std::size_t initial_size = 4096);
    std::size_t readable_bytes() const;
    std::size_t writeable_bytes() const;
    std::size_t prependable_bytes() const;
    const char *peek() const;
    bool empty() const;
    void retrieve(std::size_t);
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
