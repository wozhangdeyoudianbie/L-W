#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "connection.h"
#include "event_loop.h"
#include "event_loop_thread_pool.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

class TcpServer
{
public:
    TcpServer(EventLoop *base_loop, std::uint16_t port, std::size_t thread_count);
    ~TcpServer();
    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;
    bool start();
    bool started() const;
private:
    bool create_listen_socket();
    void close_listen_socket();
    void handle_accept(uint32_t events);
    EventLoop *base_loop_;
    std::uint16_t port_;
    EventLoopThreadPool thread_pool_;
    int listen_fd_;
    bool listen_registered_;
    bool started_;
    std::unordered_map<int, std::shared_ptr<Connection>>connections_;
    void remove_connection(const Connection::ConnectionPtr &connection);
    void remove_connection_in_loop(const Connection::ConnectionPtr &connection);
};

#endif
