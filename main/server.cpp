#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include<sys/stat.h>
#include "logger.h"
#include "thread_pool.h"
#include <signal.h>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <mutex>
#include "http.h"
#include "router.h"
#include "connection.h"
#include <memory>

using namespace std;
#define endl '\n'

const int PORT = 8080;
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;
const int THREAD_COUNT = 4;
const int MAX_HTTP_HEADER_SIZE = 8192;
unordered_map<int, shared_ptr<Connection>> connections;
mutex connections_mutex;

void add_connection(int client_fd)
{
    lock_guard<mutex> lock(connections_mutex);
    connections[client_fd] = make_shared<Connection>(client_fd);
}

shared_ptr<Connection> get_connection(int client_fd)
{
    lock_guard<mutex> lock(connections_mutex);
    auto it = connections.find(client_fd);
    if (it == connections.end())
    {
        return nullptr;
    }
    return it->second;
}

shared_ptr<Connection> remove_connection(int client_fd)
{
    lock_guard<mutex> lock(connections_mutex);
    auto it = connections.find(client_fd);
    if (it == connections.end())
    {
        return nullptr;
    }
    shared_ptr<Connection> conn = it->second;
    connections.erase(it);
    return conn;
}

int set_non_blocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL, 0);
    if (old_flag == -1)
        return -1;
    int new_flag = old_flag | O_NONBLOCK;
    return fcntl(fd, F_SETFL, new_flag);
}

bool add_fd_to_epoll(int epoll_fd, int fd, bool one_shot)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        Logger::get_instance().write_log(
            "ERROR",
            "epoll_ctl失败，fd = "
            + to_string(fd)
            + ", 错误 = " + string(strerror(errno)));
        return false;
    }
    return true;
}

bool reset_oneshot(int epoll_fd, int fd, bool need_read, bool need_write)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLET | EPOLLONESHOT;
    if (need_read)
    {
        event.events |= EPOLLIN | EPOLLRDHUP;
    }
    if (need_write)
    {
        event.events |= EPOLLOUT;
    }
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1)
    {
        Logger::get_instance().write_log(
            "ERROR",
             "epoll_ctl重新设置EPOLLONESHOT失败，fd =  "
             + to_string(fd)
             + ",错误 = " + string(strerror(errno)));
        return false;
    }
    return true;
}

void close_client(int epoll_fd, int client_fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    shared_ptr<Connection> conn = remove_connection(client_fd);
    if (conn)
    {
        conn->close_connection();
    }
    Logger::get_instance().write_log(
    "INFO",
    "客户端连接关闭，fd = "
    + to_string(client_fd)
    );
}

void handle_write(int epoll_fd, int client_fd)
{
    shared_ptr<Connection> conn = get_connection(client_fd);
    if (!conn)
    {
        return;
    }
    if (!conn->write_to_socket())
    {
        Logger::get_instance().write_log("ERROR", "发送连接数据失败，fd = " + to_string(client_fd));
        close_client(epoll_fd, client_fd);
        return;
    }
    bool need_read = !conn->peer_eof();
    bool need_write = !conn->write_buffer().empty();
    if (!need_read && !need_write)//conn->peer_eof() && conn->write_buffer().empty()
    {
        close_client(epoll_fd, client_fd);
        return;
    }
    if (!reset_oneshot(epoll_fd, client_fd, need_read, need_write))
    {
        close_client(epoll_fd, client_fd);
        return;
    }
}

void handle_client(int epoll_fd, int client_fd)
{
    shared_ptr<Connection> conn = get_connection(client_fd);
    if (!conn)
    {
        return;
    }
    if (!conn->read_from_socket())
    {
        close_client(epoll_fd, client_fd);
        return;
    }
    Buffer &data = conn->read_buffer();
    if (!data.empty())
    {
        std::size_t len = data.readable_bytes();
        Logger::get_instance().write_log(
            "INFO",
            "收到客户端TCP数据，fd = " + to_string(client_fd) +
            "，字节数 = " + to_string(len));
        conn->append_write_buffer(data.peek(), len);
        data.retrieve(len);
    }
    handle_write(epoll_fd, client_fd);
}

void accept_client(int epoll_fd, int server_fd)
{
    while (true)
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd == -1)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            Logger::get_instance().write_log(
                "ERROR",
                "accept连接失败 ");
            break;
        }
        if (set_non_blocking(client_fd) == -1)
        {
            Logger::get_instance().write_log(
                "ERROR",
                "设置client非阻塞失败,fd = "
                + to_string(client_fd));
            close(client_fd);
            continue;
        }
        add_connection(client_fd);
        if (!add_fd_to_epoll(epoll_fd, client_fd, true))
        {
            shared_ptr<Connection> conn = remove_connection(client_fd);
            if (conn)
            {
                conn->close_connection();
            }
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        const char *result = inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        if (result == nullptr)
        {
            Logger::get_instance().write_log(
                "ERROR",
                string("客户端 IP 地址转换失败，fd = ")
                + to_string(client_fd) + "，错误 = " + string(strerror(errno)));
        }
        else
        {
            Logger::get_instance().write_log("INFO",
                 string("新客户端连接，fd = ") + to_string(client_fd)
                 + "，ip = " + string(ip)
                 + "，port = "
                 + to_string(ntohs(client_addr.sin_port))
            );
        }
    }
}

int main()
{
    cin.tie(0)->sync_with_stdio(0);
    cout.tie(0);
    signal(SIGPIPE, SIG_IGN);
    if (!Logger::get_instance().init("logs/server.log"))
    {
        cout << "日志初始化失败" << endl;
        return 1;
    }
    Logger::get_instance().write_log("INFO", "服务器启动");
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        Logger::get_instance().write_log("ERROR", "server_fd创建失败，错误 = " + string(strerror(errno)));
        return 1;
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        Logger::get_instance().write_log("ERROR", "设置 SO_REUSEADDR 失败，错误 = " + string(strerror(errno)));
        close(server_fd);
        return 1;
    }
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1)
    {
        Logger::get_instance().write_log("ERROR", "bind 监听失败,错误 = " + string(strerror(errno)));
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, SOMAXCONN) == -1)
    {
        Logger::get_instance().write_log("ERROR", "listen 失败，错误 = " + string(strerror(errno)));
        close(server_fd);
        return 1;
    }
    if (set_non_blocking(server_fd) == -1)
    {
        Logger::get_instance().write_log("ERROR", "设置 server_fd 非阻塞失败");
        close(server_fd);
        return 1;
    }
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        Logger::get_instance().write_log("ERROR", "创建 epoll_fd 失败，错误 = " + string(strerror(errno)));
        close(server_fd);
        return 1;
    }
    if (!add_fd_to_epoll(epoll_fd, server_fd, false))
    {
        close(server_fd);
        close(epoll_fd);
        return 1;
    }
    ThreadPool pool(THREAD_COUNT);
    vector<epoll_event> events(MAX_EVENTS);
    Logger::get_instance().write_log("INFO", "服务器开始监听端口 " + to_string(PORT));
    while (true)
    {
        int event_count = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if (event_count == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            Logger::get_instance().write_log("ERROR", "epoll_wait 失败，错误=" + string(strerror(errno)));
            break;
        }
        for (int i = 0;i < event_count;i++)
        {
            int fd = events[i].data.fd;
            uint32_t event_type = events[i].events;
            if (fd == server_fd)
            {
                accept_client(epoll_fd, server_fd);
            }
            else if (event_type & EPOLLERR)
            {
                close_client(epoll_fd, fd);
            }
            else if (event_type & (EPOLLIN | EPOLLRDHUP))
            {
                pool.add_task([epoll_fd, fd]()
                {
                    handle_client(epoll_fd, fd);
                });
            }
            else if (event_type & EPOLLOUT)
            {
                pool.add_task([epoll_fd, fd]()
                {
                    handle_write(epoll_fd, fd);
                });
            }
            else if (event_type & EPOLLHUP)
            {
                close_client(epoll_fd, fd);
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
    Logger::get_instance().write_log("INFO", "服务器关闭");
    Logger::get_instance().flush();
    return 0;
}
