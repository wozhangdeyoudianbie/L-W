#include<iostream>
#include<string>
#include<cstring>
#include<thread>       // thread = 线程
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>

using namespace std;

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        cerr << "server_fd create file'\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//这一段我只能勉强猜到是在确认接受所有的ip 端口是8080？ 但是我还是没看懂格式

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));//这一段我不知道在干什么
    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        cerr << "bind failed'\n'";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1)//这里的10是什么意思
    {
        cerr << "listen failed'\n'";
        close(server_fd);
        return 1;
    }

    cerr << "begin gogogo port=8080 listing'\n'";
    while (1)
    {
        int client_fd = accept(server_fd, nullptr, nullptr);//为什么是nullptr
        if (client_fd == -1)
        {
            cerr << "client_fd falied'\n'";
            return 1;
        }
    }
    close(server_fd);
    return 0;
}
