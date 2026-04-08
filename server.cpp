#include<iostream>
#include<string>
#include<thread>
#include<sstream>
#include<cstring>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include"include/logger.h"
#include"include/server.h"

using namespace std;
#define endl '\n'
using ll = long long;
Logger logger;

string get_client_ip(const sockaddr_in &client_addr)//client_addr（Client Address，客户端地址）
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    return string(ip_str);
}

void handle_client(int client_fd, sockaddr_in client_addr)//client_fd（Client File Descriptor，客户端文件描述符）
{
    logger.write_log(Log_level::INFO,
                 "worker start, fd = " + to_string(client_fd) +
                 ", ip = " + get_client_ip(client_addr) +
                 ", port = " + to_string(ntohs(client_addr.sin_port)));
    cout << "新的线程开始处理客户端，ip = "
        << get_client_ip(client_addr)
        << "，port = "
        << ntohs(client_addr.sin_port)
        << endl;

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    ssize_t recv_len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);//recv_len（Receive Length，接收长度）
    if (recv_len == 0)
    {
        cout << "客户端正常关闭连接，fd = " << client_fd << endl;
        logger.write_log(Log_level::WARNING, "client closed before request, fd = " + to_string(client_fd));
        close(client_fd);
        return;
    }
    else if (recv_len < 0)
    {
        cout << "客户端读取失败，fd = " << client_fd << endl;
        logger.write_log(Log_level::ERROR, "recv failed, fd = " + to_string(client_fd));
        close(client_fd);
        return;
    }
    cout << "收到客户端请求：" << endl;
    cout << buffer << endl;

    string body = "Hello, this is a multi-thread server!\n";

    stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain; charset=utf-8\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    string response_str = response.str();//response_str（Response String，响应字符串）
    ssize_t send_len = send(client_fd, response_str.c_str(), response_str.size(), 0);//send_len（Send Length，发送长度）
    if (send_len < 0)
    {
        cout << "响应发送失败，fd = " << client_fd << endl;
        logger.write_log(Log_level::ERROR, "send failed, fd = " + to_string(client_fd));
    }
    else
    {
        cout << "响应发送完成，fd = " << client_fd << endl;
        logger.write_log(Log_level::INFO, "response sent, fd = " + to_string(client_fd));
    }
    cout << "响应发送完成，关闭客户端连接，fd = " << client_fd << endl;
    logger.write_log(Log_level::INFO, "close client, fd = " + to_string(client_fd));
    close(client_fd);
}

int run_server(int port)
{
    if (!logger.init("server.log"))
    {
        cout << "日志文件打开失败" << endl;
        return -1;
    }
    logger.write_log(Log_level::INFO, "server start");
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);//server_fd（Server File Descriptor，服务端文件描述符）
    if (server_fd < 0)
    {
        cout << "socket 创建失败" << endl;
        logger.write_log(Log_level::ERROR, "socket create failed");
        return -1;
    }

    int opt = 1;//opt（Option，选项值）
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;//server_addr（Server Address，服务端地址）
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        cout << "bind 失败" << endl;
        logger.write_log(Log_level::ERROR, "bind failed, port = " + to_string(port));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 128) < 0)
    {
        cout << "listen 失败" << endl;
        logger.write_log(Log_level::ERROR, "listen failed, port = " + to_string(port));
        close(server_fd);
        return -1;
    }

    cout << "多线程服务器启动成功，正在监听端口 " << port << endl;
    logger.write_log(Log_level::INFO, "server listen on port " + to_string(port));

    while (true)
    {
        sockaddr_in client_addr;//client_addr（Client Address，客户端地址）
        socklen_t client_addr_len = sizeof(client_addr);//client_addr_len（Client Address Length，客户端地址长度）

        int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            cout << "accept 失败" << endl;
            logger.write_log(Log_level::ERROR, "accept failed");
            continue;
        }
        cout << "主线程 accept 成功，新的客户端到来，fd = " << client_fd << endl;
        logger.write_log(Log_level::INFO, "accept new client, fd = " + to_string(client_fd));

        thread worker(handle_client, client_fd, client_addr);//worker（Worker Thread，工作线程）
        worker.detach();
    }

    close(server_fd);
    return 0;
}
