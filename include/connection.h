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
    using MessageCallback = std::function<bool(const ConnectionPtr &, Buffer &)>;
    Connection(EventLoop *loop, int fd);
    void set_close_callback(CloseCallback callback);          // 设置连接关闭回调
    void connect_established();                               // 连接建立：注册 fd 到事件循环
    void connect_destroyed();                                 // 连接销毁：注销 fd 并关闭
    void set_message_callback(MessageCallback callback);      // 设置消息回调
    void send(std::string data);                              // 线程安全发送（内部投递到 loop 线程）
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
    bool read_from_socket();      // 从 socket 读数据到读缓冲区
    bool write_to_socket();       // 把写缓冲区数据写回 socket
    bool peer_eof() const;        // 对端是否已关闭
    void close_connection();      // 关闭底层 fd
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
