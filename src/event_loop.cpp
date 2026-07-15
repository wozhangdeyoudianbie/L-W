#include "event_loop.h"
#include <cerrno>
#include <unistd.h>
#include <utility>

EventLoop::EventLoop(std::size_t max_events)
    :epoll_fd_(epoll_create1(EPOLL_CLOEXEC)), quit_(false), looping_(false),
    events_(max_events == 0 ? 1 : max_events), owner_thread_id_(std::this_thread::get_id())
{
}

EventLoop::~EventLoop()
{
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EventLoop::valid() const
{
    return epoll_fd_ != -1;
}

bool EventLoop::is_in_loop_thread() const
{
    return owner_thread_id_ == std::this_thread::get_id();
}

bool EventLoop::add_fd(int fd, uint32_t events, EventCallback callback)
{
    if (!valid() || !callback)
    {
        return false;
    }
    if (!is_in_loop_thread())
    {
        return false;
    }
    if (callbacks_.find(fd) != callbacks_.end())
    {
        return false;
    }
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        return false;
    }
    callbacks_.emplace(fd, std::move(callback));
    return true;
}

bool EventLoop::update_fd(int fd, uint32_t events)
{
    if (!valid())
    {
        return false;
    }
    if (!is_in_loop_thread())
    {
        return false;
    }
    if (callbacks_.find(fd) == callbacks_.end())
    {
        return false;
    }
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1)
    {
        return false;
    }
    return true;
}

bool EventLoop::remove_fd(int fd)
{
    if (!valid())
    {
        return false;
    }
    if (!is_in_loop_thread())
    {
        return false;
    }
    auto it = callbacks_.find(fd);
    if (it == callbacks_.end())
    {
        return false;
    }
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1)
    {
        return false;
    }
    callbacks_.erase(it);
    return true;
}

bool EventLoop::loop()
{
    if (!valid())
    {
        return false;
    }
    if (!is_in_loop_thread())
    {
        return false;
    }
    if (looping_)
    {
        return false;
    }
    looping_ = true;
    while (!quit_.load())
    {
        int event_count = epoll_wait(epoll_fd_, events_.data(),
        static_cast<int>(events_.size()), -1);
        if (event_count == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            looping_ = false;
            return false;
        }
        for (int i = 0; i < event_count; i++)
        {
            int fd = events_[i].data.fd;
            uint32_t event_type = events_[i].events;
            auto it = callbacks_.find(fd);
            if (it == callbacks_.end())
            {
                continue;
            }
            EventCallback callback = it->second;
            callback(event_type);
            if (quit_.load())
            {
                break;
            }
        }
    }
    looping_ = false;
    return true;
}

void EventLoop::quit()
{
    quit_.store(true);
}
