#ifndef CONNECTION_H
#define CONNECTION_H
#include <string>
#include <ctime>
#include<buffer.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <cstdint>

class EventLoop;

class Connection : public std::enable_shared_from_this<Connection>
{
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    using CloseCallback = std::function<void(const ConnectionPtr &)>;
    using MessageCallback = std::function<void(const ConnectionPtr &, Buffer &)>;
    Connection(EventLoop *loop, int fd);
    void set_close_callback(CloseCallback callback);
    void connect_established();
    void connect_destroyed();
    void set_message_callback(MessageCallback callback);
    void send(std::string data);
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
    EventLoop *loop() const;
private:
    enum class State
    {
        Connecting,
        Connected,
        Disconnecting,
        Disconnected
    };
    EventLoop *loop_;
    int fd_;
    State state_;
    bool registered_;
    bool close_;
    Buffer read_buffer_;
    Buffer write_buffer_;
    time_t last_active_;
    bool peer_eof_;
    void handle_event(uint32_t events);
    void handle_close();
    void send_in_loop(std::string data);
    MessageCallback message_callback_;
    CloseCallback close_callback_;
};

#endif
