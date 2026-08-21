#include <chrono>
#include <iostream>
#include <signal.h>
#include <filesystem>
#include <system_error>
#include "event_loop.h"
#include "logger.h"
#include "room_service.h"
#include "tcp_server.h"
#include "tick_timer.h"
#include "persistence_service.h"
#include "shutdown_signal.h"

using namespace std;

#define endl '\n'

const int PORT = 8080;
const int THREAD_COUNT = 4;
const std::uint32_t DEFAULT_ROOM_ID = 1;
const std::size_t DEFAULT_ROOM_CAPACITY = 4;
const std::uint64_t TICK_INTERVAL_MS = 50;                // 每 50ms 推进一次游戏状态
const std::uint64_t TIMEOUT_SCAN_INTERVAL_MS = 1000;      // 每 1 秒扫描一次连接
const std::chrono::milliseconds CONNECTION_TIMEOUT(10000);  // 10 秒无合法对端活动则超时
const std::chrono::milliseconds RECONNECT_TIMEOUT(30000);   // 断线后 30 秒内允许重连
const std::uint64_t CHECKPOINT_INTERVAL_MS = 5000;
const char *CHECKPOINT_PATH = "data/server.checkpoint";

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
    ShutdownSignal shutdown_signal(&base_loop);
    if (!shutdown_signal.valid())
    {
        Logger::get_instance().write_log("ERROR", "关闭信号管理器创建失败");
        Logger::get_instance().flush();
        return 1;
    }
    const filesystem::path checkpoint_path(CHECKPOINT_PATH);
    error_code checkpoint_directory_error;
    filesystem::create_directories(checkpoint_path.parent_path(), checkpoint_directory_error);
    if (checkpoint_directory_error)
    {
        Logger::get_instance().write_log("ERROR", "检查点目录创建失败: " + checkpoint_directory_error.message());
        Logger::get_instance().flush();
        return 1;
    }
    RoomService room_service(&base_loop, RECONNECT_TIMEOUT);
    PersistenceService persistence_service(&base_loop, &room_service, CHECKPOINT_PATH, CHECKPOINT_INTERVAL_MS);
    const PersistenceService::Loadstates load_state = persistence_service.load(SessionManager::Clock::now());
    switch (load_state)
    {
        case PersistenceService::Loadstates::restored:
            {
                Logger::get_instance().write_log("INFO", "服务器检查点恢复成功");
                break;
            }
        case PersistenceService::Loadstates::not_found:
            {
                if (!room_service.add_room(DEFAULT_ROOM_ID, DEFAULT_ROOM_CAPACITY))
                {
                    Logger::get_instance().write_log("ERROR", "默认房间创建失败");
                    Logger::get_instance().flush();
                    return 1;
                }
                Logger::get_instance().write_log("INFO", "未找到服务器检查点，已创建默认房间");
                break;
            }
        case PersistenceService::Loadstates::invalid_state:
            {
                Logger::get_instance().write_log("ERROR", "检查点加载状态无效");
                Logger::get_instance().flush();
                return 1;
            }
        case PersistenceService::Loadstates::io_error:
            {
                Logger::get_instance().write_log("ERROR", "检查点读取失败");
                Logger::get_instance().flush();
                return 1;
            }
        case PersistenceService::Loadstates::decode_error:
            {
                Logger::get_instance().write_log("ERROR", "检查点解码失败");
                Logger::get_instance().flush();
                return 1;
            }
        case PersistenceService::Loadstates::restore_error:
            {
                Logger::get_instance().write_log("ERROR", "检查点业务状态恢复失败");
                Logger::get_instance().flush();
                return 1;
            }
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
    TickTimer timeout_timer(&base_loop, TIMEOUT_SCAN_INTERVAL_MS, [&server, &room_service](std::uint64_t)
    {
        server.check_timeouts(CONNECTION_TIMEOUT);
        room_service.handle_session_timeouts(SessionManager::Clock::now());
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
    if (!persistence_service.start())
    {
        const bool timeout_timer_stopped = timeout_timer.stop();
        const bool tick_timer_stopped = tick_timer.stop();
        Logger::get_instance().write_log("ERROR", "持久化服务启动失败");
        if (!timeout_timer_stopped || !tick_timer_stopped)
        {
            Logger::get_instance().write_log("ERROR", "持久化服务启动失败后的定时器回滚失败");
        }
        Logger::get_instance().flush();
        return 1;
    }
    if (!shutdown_signal.start())
    {
        const bool timeout_timer_stopped = timeout_timer.stop();
        const bool tick_timer_stopped = tick_timer.stop();
        const bool persistence_service_stopped = persistence_service.stop();
        Logger::get_instance().write_log("ERROR", "关闭信号管理器启动失败");
        if (!timeout_timer_stopped || !tick_timer_stopped || !persistence_service_stopped)
        {
            Logger::get_instance().write_log("ERROR", "关闭信号管理器启动失败后的服务回滚失败");
        }
        Logger::get_instance().flush();
        return 1;
    }
    const bool loop_succeeded = base_loop.loop();
    const bool timeout_timer_stopped = timeout_timer.stop();
    const bool tick_timer_stopped = tick_timer.stop();
    const bool persistence_service_stopped = persistence_service.stop();
    const bool shutdown_signal_stopped = shutdown_signal.stop();
    if (!loop_succeeded || !timeout_timer_stopped || !tick_timer_stopped || !persistence_service_stopped || !shutdown_signal_stopped)
    {
        Logger::get_instance().write_log("ERROR", "服务器事件循环或关闭清理失败");
        Logger::get_instance().flush();
        return 1;
    }
    Logger::get_instance().write_log("INFO", "服务器关闭");
    Logger::get_instance().flush();
    return 0;
}
