#include "persistence_service.h"
#include "logger.h"
#include <limits>
#include <utility>

// 构造：保存依赖并创建 Store 与周期定时器
PersistenceService::PersistenceService(EventLoop *base_loop, RoomService *room_service, std::string checkpoint_path, std::uint64_t checkpoint_interval_ms)
    :base_loop_(base_loop), room_service_(room_service), store_(std::move(checkpoint_path)),
    timer_(base_loop, checkpoint_interval_ms, [this](std::uint64_t expirations)
{
    handle_timer(expirations);
}), next_generation_(1), loaded_(false), started_(false)
{
}

// 回调：周期触发保存
void PersistenceService::handle_timer(std::uint64_t)
{
    if (!save_now())
    {
        Logger::get_instance().write_log("ERROR", "周期检查点提交失败");
    }
}

// 加载：读取检查点文件并恢复房间与会话（须在 start 前、base 线程）
PersistenceService::Loadstates PersistenceService::load(SessionManager::Clock::time_point now)
{
    if (!base_loop_ || !room_service_ || !base_loop_->is_in_loop_thread())
    {
        return Loadstates::invalid_state;
    }
    if (loaded_ || started_)
    {
        return Loadstates::invalid_state;
    }
    CheckpointStore::LoadResult result = store_.load();
    switch (result.state)
    {
        case CheckpointStore::Loadstates::not_found:
            {
                next_generation_ = 1;
                loaded_ = true;
                return Loadstates::not_found;
            }
        case CheckpointStore::Loadstates::invalid_state:
            {
                return Loadstates::invalid_state;
            }
        case CheckpointStore::Loadstates::io_error:
            {
                return Loadstates::io_error;
            }
        case CheckpointStore::Loadstates::decode_error:
            {
                return Loadstates::decode_error;
            }
        case CheckpointStore::Loadstates::success:
            {
                break;
            }
    }
    if (result.checkpoint.generation == std::numeric_limits<std::uint64_t>::max())
    {
        return Loadstates::invalid_state;
    }
    if (room_service_->restore_checkpoint(result.checkpoint, now))
    {
        next_generation_ = result.checkpoint.generation + 1;
        loaded_ = true;
        return Loadstates::restored;
    }
    else
    {
        return Loadstates::restore_error;
    }
}

// 启动：后台写线程 + 周期保存定时器
bool PersistenceService::start()
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread() || !loaded_ || !timer_.valid())
    {
        return false;
    }
    if (started_)
    {
        return false;
    }
    if (!store_.start())
    {
        return false;
    }
    if (!timer_.start())
    {
        store_.stop();
        return false;
    }
    started_ = true;
    return true;
}

// 立即：生成当前检查点并提交落盘（代号自增）
bool PersistenceService::save_now()
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread() || !room_service_ || !loaded_ || !started_)
    {
        return false;
    }
    if (next_generation_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    ServerCheckpoint temp{};
    if (!room_service_->make_checkpoint(next_generation_, temp))
    {
        return false;
    }
    if (!store_.submit(std::move(temp)))
    {
        return false;
    }
    ++next_generation_;
    return true;
}


// 停止：停定时器、补存一次并等全部落盘
bool PersistenceService::stop()
{
    if (!base_loop_ || !base_loop_->is_in_loop_thread())
    {
        return false;
    }
    bool success = true;
    if (started_ == false)
    {
        return false;
    }
    if (!timer_.stop())
    {
        success = false;
    }
    if (!save_now())
    {
        success = false;
    }
    if (!store_.flush())
    {
        success = false;
    }
    if (!store_.stop())
    {
        success = false;
    }
    started_ = false;
    return success;
}

// 查询：Store 已提交/已落盘/失败的最新代号
CheckpointStore::States PersistenceService::states() const
{
    return store_.states();
}

