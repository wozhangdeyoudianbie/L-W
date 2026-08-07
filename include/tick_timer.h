#ifndef TICK_TIMER_H
#define TICK_TIMER_H

#include "event_loop.h"
#include <cstdint>
#include <functional>

class TickTimer
{
public:
    using TickCallback = std::function<void(std::uint64_t expirations)>;
    enum class States
    {
        stopped,
        running
    };
    TickTimer(EventLoop *loop, std::uint64_t interval_ms, TickCallback tick_callback);  // 构造：保存 loop 与回调（timerfd 在 start 时才创建）
    ~TickTimer();                                // 析构：关闭 timerfd（如已创建）
    TickTimer(const TickTimer &) = delete;
    TickTimer &operator=(const TickTimer &) = delete;
    TickTimer(TickTimer &&) = delete;
    TickTimer &operator=(TickTimer &&) = delete;
    bool valid() const;                          // 查询：timerfd 是否已创建成功
    States state() const;                        // 查询：当前状态（停止/运行中）
    bool start();                                // 启动：创建 timerfd，设置周期并注册到 loop
    bool stop();                                 // 停止：从 loop 注销并关闭 timerfd
private:
    void handle_read(std::uint32_t events);      // 回调：timerfd 可读时触发，读取到期次数并回调
    bool read_expirations(std::uint64_t &expirations);  // 读取：读出 timerfd 的累计到期次数
    EventLoop *loop_;
    int timer_fd_;
    std::uint64_t interval_ms_;
    TickCallback tick_callback_;
    States state_;
};

#endif
