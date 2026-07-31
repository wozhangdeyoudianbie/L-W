#include <iostream>
#include <signal.h>
#include "event_loop.h"
#include "logger.h"
#include "room_service.h"
#include "tcp_server.h"

using namespace std;

#define endl '\n'

const int PORT = 8080;
const int THREAD_COUNT = 4;
const std::uint32_t DEFAULT_ROOM_ID = 1;
const std::size_t DEFAULT_ROOM_CAPACITY = 4;

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
        Logger::get_instance().write_log(
            "ERROR",
            "base EventLoop 创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    RoomService room_service(&base_loop);
    if (!room_service.add_room(
        DEFAULT_ROOM_ID,
        DEFAULT_ROOM_CAPACITY))
    {
        Logger::get_instance().write_log(
            "ERROR",
            "默认房间创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    TcpServer server(&base_loop, PORT, THREAD_COUNT);
    server.set_message_callback(
        [&room_service](
        const Connection::ConnectionPtr &connection,
        Buffer &buffer)
    {
        return room_service.handle_message(
            connection,
            buffer);
    });
    server.set_connection_closed_callback(
        [&room_service](
        const Connection::ConnectionPtr &connection)
    {
        room_service.handle_connection_closed(connection);
    });
    if (!server.start())
    {
        Logger::get_instance().write_log(
            "ERROR",
            "TcpServer 启动失败");
        Logger::get_instance().flush();
        return 1;
    }
    Logger::get_instance().write_log(
        "INFO",
        "服务器开始监听端口 " + to_string(PORT));
    base_loop.loop();
    Logger::get_instance().write_log(
        "INFO",
        "服务器关闭");
    Logger::get_instance().flush();
    return 0;
}
