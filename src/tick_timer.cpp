#include "tick_timer.h"
#include <cerrno>
#include <utility>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

// 构造：保存 loop 与回调（timerfd 在 start 时才创建）
TickTimer::TickTimer(EventLoop *loop, std::uint64_t interval_ms, TickCallback tick_callback)
    :loop_(loop), timer_fd_(-1), interval_ms_(interval_ms),
    tick_callback_(std::move(tick_callback)), state_(States::stopped)
{
    if (!loop_ || !loop_->valid() || interval_ms_ == 0 || !tick_callback_)
    {
        return;
    }
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}

// 查询：timerfd 是否已创建成功
bool TickTimer::valid() const
{
    if (!loop_ || !loop_->valid() || !tick_callback_)
    {
        return false;
    }
    if (interval_ms_ > 0 && timer_fd_ != -1)
    {
        return true;
    }
    return false;
}

// 查询：当前状态（停止/运行中）
TickTimer::States TickTimer::state() const
{
    return state_;
}

// 启动：创建 timerfd，设置周期并注册到 loop
bool TickTimer::start()
{
    if (state_ == States::running)
    {
        return false;
    }
    if (!loop_ || !tick_callback_ || !loop_->is_in_loop_thread())
    {
        return false;
    }
    struct itimerspec new_value {};
    new_value.it_interval.tv_sec = static_cast<time_t>(interval_ms_ / 1000);
    new_value.it_interval.tv_nsec = static_cast<long>((interval_ms_ % 1000) * 1000000);
    new_value.it_value = new_value.it_interval;
    if (timerfd_settime(timer_fd_, 0, &new_value, nullptr) == -1)
    {
        return false;
    }
    bool added = loop_->add_fd(timer_fd_, EPOLLIN | EPOLLET, [this](std::uint32_t events)
    {
        handle_read(events);
    });
    if (!added)
    {
        struct itimerspec stopped_value {};
        std::uint64_t ignored = 0;
        if (timerfd_settime(timer_fd_, 0, &stopped_value, nullptr) == -1 || !read_expirations(ignored))
        {
            close(timer_fd_);
            timer_fd_ = -1;
        }
        return false;
    }
    state_ = States::running;
    return true;
}

// 停止：从 loop 注销并关闭 timerfd
bool TickTimer::stop()
{
    if (!loop_ || !loop_->is_in_loop_thread())
    {
        return false;
    }
    if (state_ == States::stopped)
    {
        return true;
    }
    if (!loop_->remove_fd(timer_fd_))
    {
        return false;
    }
    struct itimerspec stopped_value {};
    if (timerfd_settime(timer_fd_, 0, &stopped_value, nullptr) == -1)
    {
        close(timer_fd_);
        timer_fd_ = -1;
        state_ = States::stopped;
        return false;
    }
    std::uint64_t ignored = 0;
    if (!read_expirations(ignored))
    {
        ::close(timer_fd_);
        timer_fd_ = -1;
        state_ = States::stopped;
        return false;
    }
    state_ = States::stopped;
    return true;
}

// 回调：timerfd 可读时触发，读取到期次数并回调
void TickTimer::handle_read(std::uint32_t events)
{
    if (state_ != States::running || !(events & EPOLLIN))
    {
        return;
    }
    std::uint64_t expirations = 0;
    if (!read_expirations(expirations))
    {
        return;
    }
    if (expirations > 0 && tick_callback_)
    {
        tick_callback_(expirations);
    }
}

// 读取：读出 timerfd 的累计到期次数
bool TickTimer::read_expirations(std::uint64_t &expirations)
{
    expirations = 0;
    while (1)
    {
        std::uint64_t value = 0;
        const ssize_t n = read(timer_fd_, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value)))
        {
            expirations += value;
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

// 析构：关闭 timerfd（如已创建）
TickTimer::~TickTimer()
{
    if (state_ == States::running)
    {
        stop();
    }
    if (timer_fd_ != -1)
    {
        ::close(timer_fd_);
        timer_fd_ = -1;
    }
}
