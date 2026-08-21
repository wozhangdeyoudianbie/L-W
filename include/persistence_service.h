#ifndef PERSISTENCE_SERVICE_H
#define PERSISTENCE_SERVICE_H

#include "checkpoint_store.h"
#include "event_loop.h"
#include "room_service.h"
#include "tick_timer.h"
#include <cstdint>
#include <string>

class PersistenceService
{
public:
    enum class Loadstates
    {
        restored,
        not_found,
        invalid_state,
        io_error,
        decode_error,
        restore_error
    };
    PersistenceService(EventLoop *base_loop, RoomService *room_service, std::string checkpoint_path, std::uint64_t checkpoint_interval_ms);   // 构造：保存依赖并创建 Store 与周期定时器
    PersistenceService(const PersistenceService &) = delete;
    PersistenceService &operator=(const PersistenceService &) = delete;
    PersistenceService(PersistenceService &&) = delete;
    PersistenceService &operator=(PersistenceService &&) = delete;
    Loadstates load(SessionManager::Clock::time_point now);   // 加载：读取检查点文件并恢复房间与会话（须在 start 前）
    bool start();                                             // 启动：后台写线程 + 周期保存定时器
    bool save_now();                                          // 立即：生成当前检查点并提交落盘
    bool stop();                                              // 停止：停定时器、补存一次并等全部落盘
    CheckpointStore::States states() const;                   // 查询：Store 已提交/已落盘/失败的最新代号
private:
    void handle_timer(std::uint64_t expirations);             // 回调：周期触发保存
    EventLoop *base_loop_;
    RoomService *room_service_;
    CheckpointStore store_;
    TickTimer timer_;
    std::uint64_t next_generation_;
    bool loaded_;
    bool started_;
};

#endif
