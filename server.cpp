#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "logger.h"

using namespace std;
#define endl '\n'

const int PORT = 8080;
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;

int main()
{
    cin.tie(0)->sync_with_stdio(0);

    Logger logger;
    if (!logger.init("server.log"))
    {
        cerr << "logger init failed" << endl;
        return 1;
    }

    // server_fd（Server File Descriptor，服务器监听套接字文件描述符）
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        logger.write_log(Log_level::ERROR, "创建 socket 失败");
        return 1;
    }

    // opt（Option，选项值）
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        logger.write_log(Log_level::ERROR, "设置 socket 选项失败");
        close(server_fd);
        return 1;
    }

    // server_addr（Server Address，服务器地址结构）
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        logger.write_log(Log_level::ERROR, "绑定端口失败");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == -1)
    {
        logger.write_log(Log_level::ERROR, "开始监听失败");
        close(server_fd);
        return 1;
    }

    logger.write_log(Log_level::INFO, "服务器启动成功，正在监听端口 " + to_string(PORT));

    // epfd（Epoll File Descriptor，epoll 实例句柄）
    int epfd = epoll_create1(0);
    if (epfd == -1)
    {
        logger.write_log(Log_level::ERROR, "创建 epoll 实例失败");
        close(server_fd);
        return 1;
    }

    // ev（Event，事件对象）
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1)
    {
        logger.write_log(Log_level::ERROR, "将监听 socket 加入 epoll 失败");
        close(epfd);
        close(server_fd);
        return 1;
    }

    // events（Events，事件数组）
    vector<epoll_event> events(MAX_EVENTS);

    while (true)
    {
        // nfds（Number of File Descriptors，返回的就绪文件描述符数量）
        int nfds = epoll_wait(epfd, events.data(), MAX_EVENTS, -1);
        if (nfds == -1)
        {
            logger.write_log(Log_level::ERROR, "epoll_wait 等待事件失败");
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            // fd（File Descriptor，文件描述符）
            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                // client_addr（Client Address，客户端地址结构）
                sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                memset(&client_addr, 0, sizeof(client_addr));

                // client_fd（Client File Descriptor，客户端连接套接字文件描述符）
                int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);
                if (client_fd == -1)
                {
                    logger.write_log(Log_level::ERROR, "接受新连接失败");
                    continue;
                }

                logger.write_log(
                    Log_level::INFO,
                    "新客户端连接，fd=" + to_string(client_fd) +
                    "，ip=" + string(inet_ntoa(client_addr.sin_addr)) +
                    "，port=" + to_string(ntohs(client_addr.sin_port))
                );

                // client_ev（Client Event，客户端事件对象）
                epoll_event client_ev;
                memset(&client_ev, 0, sizeof(client_ev));
                client_ev.events = EPOLLIN;
                client_ev.data.fd = client_fd;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1)
                {
                    logger.write_log(Log_level::ERROR, "将客户端 fd 加入 epoll 失败，fd=" + to_string(client_fd));
                    close(client_fd);
                    continue;
                }
            }
            else
            {
                // buffer（Buffer，缓冲区）
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));

                // len（Length，读取到的字节数）
                int len = read(fd, buffer, sizeof(buffer) - 1);

                if (len < 0)
                {
                    logger.write_log(Log_level::ERROR, "读取数据失败，fd=" + to_string(fd));
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
                else if (len == 0)
                {
                    logger.write_log(Log_level::INFO, "客户端关闭连接，fd=" + to_string(fd));
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
                else
                {
                    string msg(buffer, len);
                    logger.write_log(
                        Log_level::INFO,
                        "收到数据，fd=" + to_string(fd) + "，内容=[" + msg + "]"
                    );

                    // 当前阶段故意不发送响应
                    // 当前阶段故意不主动关闭连接
                    // 目的：观察 LT 下多个连接、多次输入时的行为
                }
            }
        }
    }

    close(epfd);
    close(server_fd);
    return 0;
}
