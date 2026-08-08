#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "connection.h"
#include "event_loop.h"
#include "event_loop_thread_pool.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

class TcpServer
{
public:
    using ConnectionClosedCallback = std::function<void(const Connection::ConnectionPtr &)>;
    TcpServer(EventLoop *base_loop, std::uint16_t port, std::size_t thread_count);
    ~TcpServer();
    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;
    void set_message_callback(Connection::MessageCallback callback);             // 设置消息处理回调
    void set_connection_closed_callback(ConnectionClosedCallback callback);      // 设置连接关闭回调
    bool start();       // 监听端口并启动线程池
    bool started() const;
    void check_timeouts(std::chrono::milliseconds timeout);      // base 线程遍历连接并请求超时检查
private:
    bool create_listen_socket();
    void close_listen_socket();
    void handle_accept(uint32_t events);
    void remove_connection(const Connection::ConnectionPtr &connection);
    void remove_connection_in_loop(const Connection::ConnectionPtr &connection);
    EventLoop *base_loop_;
    std::uint16_t port_;
    EventLoopThreadPool thread_pool_;
    int listen_fd_;
    bool listen_registered_;
    bool started_;
    Connection::MessageCallback message_callback_;
    ConnectionClosedCallback connection_closed_callback_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
};

#endif
