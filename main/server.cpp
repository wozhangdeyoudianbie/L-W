#include <iostream>
#include <signal.h>
#include "event_loop.h"
#include "logger.h"
#include "tcp_server.h"
#include <utility>
#include "codec.h"

using namespace std;
#define endl '\n'

const int PORT = 8080;
const int THREAD_COUNT = 4;

void handle_frame(const Connection::ConnectionPtr &connection, std::uint16_t type, const std::string &payload)
{
    std::string frame;
    if (!Codec::encode(type, payload, frame))
    {
        return;
    }
    connection->send(std::move(frame));
}

bool handle_message(const Connection::ConnectionPtr &connection, Buffer &buffer)
{
    Codec::FrameCallback frame_callback = [connection](std::uint16_t type, const std::string &payload)
    {
        handle_frame(connection, type, payload);
    };
    return Codec::decode(buffer, frame_callback);
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
    EventLoop base_loop;
    if (!base_loop.valid())
    {
        Logger::get_instance().write_log("ERROR", "base EventLoop 创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    TcpServer server(&base_loop, PORT, THREAD_COUNT);
    server.set_message_callback(handle_message);
    if (!server.start())
    {
        Logger::get_instance().write_log("ERROR", "TcpServer 启动失败");
        Logger::get_instance().flush();
        return 1;
    }
    Logger::get_instance().write_log("INFO", "服务器开始监听端口 " + to_string(PORT));
    base_loop.loop();
    Logger::get_instance().write_log("INFO", "服务器关闭");
    Logger::get_instance().flush();
    return 0;
}
