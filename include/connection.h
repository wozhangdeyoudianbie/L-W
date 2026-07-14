#ifndef CONNECTION_H
#define CONNECTION_H
#include <string>
#include <ctime>
#include<buffer.h>
#include <cstddef>

class Connection
{
public:
    explicit Connection(int fd);
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    int fd() const;
    bool close() const;
    time_t last_active() const;
    Buffer &read_buffer();
    const Buffer &read_buffer() const;
    const Buffer &write_buffer() const;
    void append_write_buffer(const char *data, std::size_t len);
    void append_write_buffer(const std::string &data);
    bool read_from_socket();
    bool write_to_socket();
    bool peer_eof() const;
    void close_connection();
private:
    int fd_;
    bool close_;
    Buffer read_buffer_;
    Buffer write_buffer_;
    time_t last_active_;
    bool peer_eof_;
};

#endif
