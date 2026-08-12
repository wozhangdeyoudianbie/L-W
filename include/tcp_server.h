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
    TcpServer(EventLoop *base_loop, std::uint16_t port, std::size_t thread_count);   // 构造：保存 base 循环与端口
    ~TcpServer();                                                                    // 析构：关闭监听与全部连接
    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;
    void set_message_callback(Connection::MessageCallback callback);             // 设置消息处理回调
    void set_connection_closed_callback(ConnectionClosedCallback callback);      // 设置连接关闭回调
    bool start();       // 监听端口并启动线程池
    bool started() const;                   // 查询：是否已启动
    void check_timeouts(std::chrono::milliseconds timeout);      // base 线程遍历连接并请求超时检查
private:
    bool create_listen_socket();            // 创建监听 socket（非阻塞）
    void close_listen_socket();             // 关闭监听 socket
    void handle_accept(uint32_t events);    // 接受新连接并分发到工作线程
    void remove_connection(const Connection::ConnectionPtr &connection);   // 移除连接（投递到 base 线程）
    void remove_connection_in_loop(const Connection::ConnectionPtr &connection);   // 移除连接（base 线程执行）
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
