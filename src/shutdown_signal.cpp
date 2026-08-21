#include "shutdown_signal.h"
#include <cerrno>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

// 构造：屏蔽 SIGINT/SIGTERM 并创建 signalfd
ShutdownSignal::ShutdownSignal(EventLoop *loop)
    :loop_(loop), signal_fd_(-1), mask_blocked_(false), state_(States::stopped)
{
    if (!loop_ || !loop_->valid())
    {
        return;
    }
    if (sigemptyset(&signal_mask_) == -1)
    {
        return;
    }
    if (sigaddset(&signal_mask_, SIGINT) == -1)
    {
        return;
    }
    if (sigaddset(&signal_mask_, SIGTERM) == -1)
    {
        return;
    }
    if (pthread_sigmask(SIG_BLOCK, &signal_mask_, &previous_mask_) != 0)
    {
        return;
    }
    mask_blocked_ = true;
    signal_fd_ = signalfd(-1, &signal_mask_, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1)
    {
        if (pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr) == 0)
        {
            mask_blocked_ = false;
        }
    }
}

// 查询：signalfd 与屏蔽字是否就绪
bool ShutdownSignal::valid() const
{
    if (!loop_ || !loop_->valid())
    {
        return false;
    }
    return signal_fd_ != -1 && mask_blocked_;
}

// 查询：当前状态（停止/运行中）
ShutdownSignal::States ShutdownSignal::state() const
{
    return state_;
}

// 启动：注册 signalfd 到事件循环（须在 loop 线程）
bool ShutdownSignal::start()
{
    if (state_ == States::running)
    {
        return false;
    }
    if (!valid() || !loop_->is_in_loop_thread())
    {
        return false;
    }
    const bool add = loop_->add_fd(signal_fd_, EPOLLIN | EPOLLET, [this](std::uint32_t events)
    {
        handle_read(events);
    });
    if (!add)
    {
        return false;
    }
    state_ = States::running;
    return true;
}

// 停止：从事件循环注销 signalfd（须在 loop 线程）
bool ShutdownSignal::stop()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return false;
    }
    if (state_ == States::stopped)
    {
        return true;
    }
    if (!loop_->remove_fd(signal_fd_))
    {
        return false;
    }
    state_ = States::stopped;
    return true;
}

// 回调：signalfd 可读时处理，收到终止信号则退出循环
void ShutdownSignal::handle_read(std::uint32_t events)
{
    if (state_ != States::running)
    {
        return;
    }
    if (!(events & EPOLLIN))
    {
        loop_->quit();
        return;
    }
    bool shutdown_requested = false;
    if (!read_signals(shutdown_requested))
    {
        loop_->quit();
        return;
    }
    if (shutdown_requested)
    {
        loop_->quit();
    }
}

// 读取：signalfd 中的信号，标记是否收到终止信号
bool ShutdownSignal::read_signals(bool &shutdown_requested)
{
    shutdown_requested = false;
    while (1)
    {
        struct signalfd_siginfo information {};
        const ssize_t n = read(signal_fd_, &information, sizeof(information));
        if (n == static_cast<ssize_t>(sizeof(information)))
        {
            if (information.ssi_signo == SIGINT || information.ssi_signo == SIGTERM)
            {
                shutdown_requested = true;
            }
            continue;
        }
        if (n == -1 && errno == EINTR)
        {
            continue;
        }
        if (n == -1 && errno == EAGAIN)
        {
            return true;
        }
        return false;
    }
}

// 析构：关闭 signalfd 并恢复信号屏蔽字
ShutdownSignal::~ShutdownSignal()
{
    if (state_ == States::running)
    {
        stop();
    }
    if (signal_fd_ != -1)
    {
        ::close(signal_fd_);
        signal_fd_ = -1;
    }
    if (mask_blocked_)
    {
        if (pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr) == 0)
        {
            mask_blocked_ = false;
        }
    }
    state_ = States::stopped;
}
