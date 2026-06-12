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
#include "include/logger.h"
#include "include/thread_pool.h"
#include <signal.h>
#include <sstream>
#include <unordered_map>
#include <mutex>

using namespace std;
#define endl '\n'

const int PORT = 8080;
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;
const int THREAD_COUNT = 4;
const int MAX_HTTP_HEADER_SIZE = 8192;
unordered_map<int, string> client_buffers;
mutex client_buffers_mutex;

struct HttpRequest
{
    string method;
    string path;
    string version;
    bool valid = false;
};

HttpRequest parse_http_request(const string &request_text)
{
    HttpRequest request;
    size_t line_end = request_text.find("\r\n");
    if (line_end == string::npos)
    {
        return request;
    }
    string request_line = request_text.substr(0, line_end);
    stringstream ss(request_line);
    ss >> request.method >> request.path >> request.version;
    if (request.method.empty() || request.path.empty() || request.version.empty())
    {
        return request;
    }
    request.valid = true;
    return request;
}

void init_client_buffer(int client_fd)
{
    lock_guard<mutex> lock(client_buffers_mutex);
    client_buffers[client_fd] = "";
}

void erase_client_buffer(int client_fd)
{
    lock_guard<mutex> lock(client_buffers_mutex);
    client_buffers.erase(client_fd);
}

void append_client_buffer(int client_fd, const char *data, ssize_t len)
{
    lock_guard<mutex> lock(client_buffers_mutex);
    client_buffers[client_fd].append(data, len);
}

bool http_header_complate(const string &request_text)
{
    return request_text.find("\r\n\r\n") != string::npos;
}

string build_http_response(int status_code, const string &status_text, const string &body, const string &content_type, const string &extra_header = "")
{
    string response;
    response += "HTTP/1.1 " + to_string(status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + to_string(body.size()) + "\r\n";
    if (!extra_header.empty())
    {
        response += extra_header;
    }
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    return response;
}

string build_response_by_request(const string &request_text)
{
    HttpRequest request;
    request = parse_http_request(request_text);
    if (!request.valid)
    {
        string body = "400 Bad Request\n";
        return build_http_response(400, "Bad Request", body, "text/plain; charset=utf-8"
        );
    }
    Logger::get_instance().write_log(
        "INFO",
        "解析HTTP请求：method = " + request.method +
        "，path = " + request.path +
        "，version = " + request.version);
    if (request.method != "GET")
    {
        string body = "405 Method Not Allowed\n";
        return build_http_response(
            405,
            "Method Not Allowed",
            body,
            "text/plain; charset=utf-8"
        );
    }
    if (request.path == "/")
    {
        string body;
        body += "<!DOCTYPE html>\n";
        body += "<html>\n";
        body += "<head>\n";
        body += "    <meta charset=\"UTF-8\">\n";
        body += "    <title>My C++ WebServer</title>\n";
        body += "</head>\n";
        body += "<body>\n";
        body += "    <h1>你好，这是我的 C++ WebServer</h1>\n";
        body += "    <p>HTTP 第一阶段已经跑通。</p>\n";
        body += "</body>\n";
        body += "</html>\n";
        return build_http_response(
            200,
            "OK",
            body,
            "text/html; charset=utf-8"
        );
    }
    if (request.path == "/hello")
    {
        string body = "hello from C++ WebServer\n";
        return build_http_response(
            200,
            "OK",
            body,
            "text/plain; charset=utf-8"
        );
    }
    string body = "404 Not Found\n";
    return build_http_response(
        404,
        "Not Found",
        body,
        "text/plain; charset=utf-8"
    );
}

string get_client_buffer(int client_fd)
{
    lock_guard<mutex> lock(client_buffers_mutex);
    if (client_buffers.count(client_fd) == 0)
    {
        return "";
    }
    return client_buffers[client_fd];
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

bool reset_oneshot(int epoll_fd, int fd)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
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
    erase_client_buffer(client_fd);
    close(client_fd);
    Logger::get_instance().write_log(
        "INFO",
        "客户端连接关闭，fd = "
        + to_string(client_fd)
    );
}

bool write_all(int client_fd, const string &response)
{
    const char *data = response.c_str();
    ssize_t left = response.size();
    while (left > 0)
    {
        ssize_t write_len = write(client_fd, data, left);
        if (write_len > 0)
        {
            data += write_len;
            left -= write_len;
        }
        else if (write_len == -1 && errno == EINTR)
        {
            continue;
        }
        else if (write_len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            Logger::get_instance().write_log(
               "WARNING",
               "发送缓冲区暂时不可用，fd = " +
               to_string(client_fd)
            );
            return false;
        }
        else
        {
            Logger::get_instance().write_log(
                "ERROR",
                "向客户端写HTTP响应失败，fd = " + to_string(client_fd) +
                "，错误 = " + string(strerror(errno))
            );
            return false;
        }
    }
    return true;
}

void handle_client(int epoll_fd, int client_fd)
{
    char buffer[BUFFER_SIZE];
    bool  client_closed = false;
    bool need_response = false;
    while (true)
    {
        ssize_t read_len = read(client_fd, buffer, sizeof(buffer));
        if (read_len > 0)
        {
            append_client_buffer(client_fd, buffer, read_len);
            string message(buffer, read_len);
            Logger::get_instance().write_log(
                "INFO",
                "收到客户端HTTP数据，fd = "
                + to_string(client_fd)
                + "， 字节数 = " + to_string(read_len));
            string request_text = get_client_buffer(client_fd);
            if ((int)request_text.size() > MAX_HTTP_HEADER_SIZE)
            {
                string body = "431 Request Header Fields Too Large\n";
                string response = build_http_response(
                    431,
                    "Request Header Fields Too Large",
                    body,
                    "text/plain; charset=utf-8"
                );
                write_all(client_fd, response);
                close_client(epoll_fd, client_fd);
                return;
            }
            if (http_header_complate(request_text))
            {
                need_response = true;
                break;
            }
        }
        else if (read_len == 0)
        {
            client_closed = true;
            break;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            Logger::get_instance().write_log(
                "ERROR",
                 "读取客户端HTTP数据失败"
                + to_string(client_fd)
                + "，错误 = " + string(strerror(errno))
            );
            client_closed = true;
            break;
        }
    }
    if (need_response)
    {
        string request_text = get_client_buffer(client_fd);
        string response = build_response_by_request(request_text);
        write_all(client_fd, response);
        close_client(epoll_fd, client_fd);
        return;
    }
    if (client_closed)
    {
        close_client(epoll_fd, client_fd);
        return;
    }
    else
    {
        reset_oneshot(epoll_fd, client_fd);
    }
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
        if (!add_fd_to_epoll(epoll_fd, client_fd, true))
        {
            close(client_fd);
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
    if (!Logger::get_instance().init("server.log"))
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
            else if (event_type & (EPOLLERR | EPOLLHUP))
            {
                close_client(epoll_fd, fd);
            }
            else if (event_type & EPOLLIN)
            {
                pool.add_task([epoll_fd, fd]()
                {
                    handle_client(epoll_fd, fd);
                });
            }
            else if (event_type & EPOLLRDHUP)
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







