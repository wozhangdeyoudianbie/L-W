#ifndef CONNECTION_H
#define CONNECTION_H
#include <atomic>
#include <string>
#include <chrono>
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
    static constexpr std::size_t WRITE_HIGH_WATER_MARK = 512 * 1024;
    static constexpr std::size_t WRITE_HARD_LIMIT = 1024 * 1024;
    Connection(EventLoop *loop, int fd);
    void set_close_callback(CloseCallback callback);          // 设置连接关闭回调
    void connect_established();                               // 连接建立：注册 fd 到事件循环
    void connect_destroyed();                                 // 连接销毁：注销 fd 并关闭
    void set_message_callback(MessageCallback callback);      // 设置消息回调
    bool send(std::string data);                              // 线程安全接纳完整发送数据
    void request_close();                                     // 线程安全请求关闭连接
    void refresh_peer_activity();                             // 刷新对端活性时间（须在所属 loop 线程）
    void check_timeout(std::chrono::milliseconds timeout);    // 异步请求所属 loop 检查超时
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    int fd() const;
    bool close() const;
    Buffer &read_buffer();
    const Buffer &read_buffer() const;
    const Buffer &write_buffer() const;
    std::size_t pending_write_bytes() const;
    bool under_backpressure() const;
    bool peer_eof() const;        // 对端是否已关闭
    EventLoop *loop() const;
private:
    using Clock = std::chrono::steady_clock;
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
    std::atomic<std::size_t> pending_write_bytes_;
    Clock::time_point last_peer_activity_time_;
    bool peer_eof_;
    void handle_event(uint32_t events);
    void handle_close();
    bool reserve_write_bytes(std::size_t len);
    void release_write_bytes(std::size_t len);
    void clear_write_buffer_in_loop();
    void send_in_loop(std::string data);
    bool read_from_socket();      // 从 socket 读数据到读缓冲区
    bool write_to_socket();       // 把写缓冲区数据写回 socket
    void close_connection();      // 关闭底层 fd
    void check_timeout_in_loop(std::chrono::milliseconds timeout);
    MessageCallback message_callback_;
    CloseCallback close_callback_;
};

#endif
