#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

using namespace std;

const char *SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8080;
const size_t DATA_SIZE = 32ULL * 1024 * 1024;
const int CLIENT_RECV_BUFFER_SIZE = 4096;

bool save_file(const string &path, const vector<char> &data)
{
    ofstream out(path, ios::binary);
    if (!out.is_open())
    {
        cerr << "打开文件失败：" << path << endl;
        return false;
    }
    out.write(data.data(), static_cast<streamsize>(data.size()));
    return out.good();
}

bool send_all(int fd, const vector<char> &data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t n = send(
            fd,
            data.data() + sent,
            data.size() - sent,
            MSG_NOSIGNAL
        );
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            cerr << "发送失败，已发送字节数 = " << sent
                << "，错误 = " << strerror(errno) << endl;
            return false;
        }
    }
    cout << "发送完成，字节数 = " << sent << endl;
    return true;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    vector<char> send_data(DATA_SIZE);
    for (size_t i = 0;i < send_data.size();i++)
    {
        send_data[i] = static_cast<char>('A' + i % 26);
    }
    if (!save_file("/tmp/echo_send.bin", send_data))
    {
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        cerr << "创建 socket 失败：" << strerror(errno) << endl;
        return 1;
    }

    int recv_buffer_size = CLIENT_RECV_BUFFER_SIZE;
    if (setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVBUF,
        &recv_buffer_size,
        sizeof(recv_buffer_size)
        ) == -1)
    {
        cerr << "设置接收缓冲区失败：" << strerror(errno) << endl;
        close(fd);
        return 1;
    }

    timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    if (setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
        ) == -1)
    {
        cerr << "设置接收超时失败：" << strerror(errno) << endl;
        close(fd);
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1)
    {
        cerr << "服务器 IP 地址无效" << endl;
        close(fd);
        return 1;
    }

    if (connect(
        fd,
        reinterpret_cast<sockaddr *>(&server_addr),
        sizeof(server_addr)
        ) == -1)
    {
        cerr << "连接服务器失败：" << strerror(errno) << endl;
        close(fd);
        return 1;
    }

    cout << "连接服务器成功" << endl;

    if (!send_all(fd, send_data))
    {
        close(fd);
        return 1;
    }

    if (shutdown(fd, SHUT_WR) == -1)
    {
        cerr << "shutdown(SHUT_WR) 失败：" << strerror(errno) << endl;
        close(fd);
        return 1;
    }

    cout << "已关闭客户端发送方向，暂停读取 3 秒" << endl;
    this_thread::sleep_for(chrono::seconds(3));

    vector<char> recv_data;
    recv_data.reserve(DATA_SIZE);
    char buffer[64 * 1024];
    bool recv_ok = true;

    while (true)
    {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0)
        {
            recv_data.insert(recv_data.end(), buffer, buffer + n);
        }
        else if (n == 0)
        {
            break;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            cerr << "接收失败或超时：" << strerror(errno) << endl;
            recv_ok = false;
            break;
        }
    }

    close(fd);

    if (!save_file("/tmp/echo_recv.bin", recv_data))
    {
        return 1;
    }

    cout << "接收完成，字节数 = " << recv_data.size() << endl;

    if (!recv_ok)
    {
        return 1;
    }

    if (send_data.size() != recv_data.size())
    {
        cerr << "数据长度不一致，发送 = " << send_data.size()
            << "，接收 = " << recv_data.size() << endl;
        return 1;
    }

    if (send_data != recv_data)
    {
        cerr << "数据内容不一致" << endl;
        return 1;
    }

    cout << "测试通过：发送数据与接收数据完全一致" << endl;
    return 0;
}
