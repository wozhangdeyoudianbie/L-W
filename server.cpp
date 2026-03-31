#include<iostream>
#include<string>
#include<cstring>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>

using namespace std;
#define endl '\n'

int main()
{
    cin.tie(0)->sync_with_stdio(0);

    // server_fd = server file descriptor，服务器监听套接字文件描述符
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        cerr << "socket create failed" << endl;
        return 1;
    }

    // sockaddr_in = socket address internet，IPv4 地址结构体
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;                 // AF = Address Family，地址族，这里表示 IPv4
    server_addr.sin_port = htons(8080);               // htons = host to network short，主机字节序转网络字节序
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // htonl = host to network long，监听本机所有网卡地址

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        cerr << "bind failed" << endl;
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1)
    {
        cerr << "listen failed" << endl;
        close(server_fd);
        return 1;
    }

   cout << "server is listening on port 8080\n" << flush;

    while (true)
{
    // client_fd = client file descriptor，某个客户端连接对应的文件描述符
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd == -1)
    {
        cerr << "accept failed\n";
        continue;
    }

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n > 0)
    {
        cout << "received request:\n";
        cout << buffer << flush;
    }

    string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 18\r\n"
        "\r\n"
        "hello from server\n";

    write(client_fd, response.c_str(), response.size());
    close(client_fd);
}
close(server_fd);
return 0;
}
