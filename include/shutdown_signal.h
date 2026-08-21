#ifndef SHUTDOWN_SIGNAL_H
#define SHUTDOWN_SIGNAL_H

#include "event_loop.h"
#include <cstdint>
#include <signal.h>

class ShutdownSignal
{
public:
    enum class States
    {
        stopped,
        running
    };
    explicit ShutdownSignal(EventLoop *loop);   // 构造：屏蔽 SIGINT/SIGTERM 并创建 signalfd
    ~ShutdownSignal();                          // 析构：关闭 signalfd 并恢复信号屏蔽字
    ShutdownSignal(const ShutdownSignal &) = delete;
    ShutdownSignal &operator=(const ShutdownSignal &) = delete;
    ShutdownSignal(ShutdownSignal &&) = delete;
    ShutdownSignal &operator=(ShutdownSignal &&) = delete;
    bool valid() const;                          // 查询：signalfd 与屏蔽字是否就绪
    States state() const;                        // 查询：当前状态（停止/运行中）
    bool start();                                // 启动：注册 signalfd 到事件循环
    bool stop();                                 // 停止：从事件循环注销 signalfd
private:
    void handle_read(std::uint32_t events);      // 回调：signalfd 可读时处理，收到终止信号则退出循环
    bool read_signals(bool &shutdown_requested); // 读取：signalfd 中的信号，标记是否收到终止信号
    EventLoop *loop_;
    int signal_fd_;
    sigset_t signal_mask_;
    sigset_t previous_mask_;
    bool mask_blocked_;
    States state_;
};

#endif
