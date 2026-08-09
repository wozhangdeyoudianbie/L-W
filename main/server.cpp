#include <chrono>
#include <iostream>
#include <signal.h>
#include "event_loop.h"
#include "logger.h"
#include "room_service.h"
#include "tcp_server.h"
#include "tick_timer.h"

using namespace std;

#define endl '\n'

const int PORT = 8080;
const int THREAD_COUNT = 4;
const std::uint32_t DEFAULT_ROOM_ID = 1;
const std::size_t DEFAULT_ROOM_CAPACITY = 4;
const std::uint64_t TICK_INTERVAL_MS = 50;                // 每 50ms 推进一次游戏状态
const std::uint64_t TIMEOUT_SCAN_INTERVAL_MS = 1000;      // 每 1 秒扫描一次连接
const std::chrono::milliseconds CONNECTION_TIMEOUT(10000); // 10 秒无合法对端活动则超时

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
    RoomService room_service(&base_loop);
    if (!room_service.add_room(
        DEFAULT_ROOM_ID,
        DEFAULT_ROOM_CAPACITY))
    {
        Logger::get_instance().write_log("ERROR", "默认房间创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    TickTimer tick_timer(&base_loop, TICK_INTERVAL_MS, [&room_service](std::uint64_t expirations)
    {
        room_service.handle_tick(expirations);
    });
    if (!tick_timer.valid())
    {
        Logger::get_instance().write_log("ERROR", "TickTimer 创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    TcpServer server(&base_loop, PORT, THREAD_COUNT);
    server.set_message_callback([&room_service](const Connection::ConnectionPtr &connection, Buffer &buffer)
    {
        return room_service.handle_message(connection, buffer);
    });
    server.set_connection_closed_callback([&room_service](const Connection::ConnectionPtr &connection)
    {
        room_service.handle_connection_closed(connection);
    });
    TickTimer timeout_timer(&base_loop, TIMEOUT_SCAN_INTERVAL_MS, [&server](std::uint64_t)
    {
        server.check_timeouts(CONNECTION_TIMEOUT);
    });
    if (!timeout_timer.valid())
    {
        Logger::get_instance().write_log("ERROR", "连接超时定时器创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    if (!server.start())
    {
        Logger::get_instance().write_log("ERROR", "TcpServer 启动失败");
        Logger::get_instance().flush();
        return 1;
    }
    Logger::get_instance().write_log("INFO", "服务器开始监听端口 " + to_string(PORT));
    if (!tick_timer.start())
    {
        Logger::get_instance().write_log("ERROR", "TickTimer 启动失败");
        Logger::get_instance().flush();
        return 1;
    }
    if (!timeout_timer.start())
    {
        tick_timer.stop();
        Logger::get_instance().write_log("ERROR", "连接超时定时器启动失败");
        Logger::get_instance().flush();
        return 1;
    }
    base_loop.loop();
    const bool timeout_timer_stopped = timeout_timer.stop();
    const bool tick_timer_stopped = tick_timer.stop();
    if (!timeout_timer_stopped || !tick_timer_stopped)
    {
        Logger::get_instance().write_log("ERROR", "定时器停止失败");
        Logger::get_instance().flush();
        return 1;
    }
    Logger::get_instance().write_log("INFO", "服务器关闭");
    Logger::get_instance().flush();
    return 0;
}
