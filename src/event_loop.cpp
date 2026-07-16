#include "event_loop.h"
#include <cerrno>
#include <unistd.h>
#include <utility>
#include <sys/eventfd.h>

EventLoop::EventLoop(std::size_t max_events)
    : epoll_fd_(-1), wakeup_fd_(-1), quit_(false), looping_(false),
    events_(max_events == 0 ? 1 : max_events), owner_thread_id_(std::this_thread::get_id())
{
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1)
        return;
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ == -1)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
        return;
    }
    bool added = add_fd(wakeup_fd_, EPOLLIN | EPOLLET, [this](uint32_t events)
    {
        handle_wakeup(events);
    });
    if (!added)
    {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
        close(epoll_fd_);
        epoll_fd_ = -1;
        return;
    }
}

void EventLoop::wakeup()
{
    if (wakeup_fd_ == -1)
        return;
    uint64_t one = 1;
    while (true)
    {
        ssize_t result = write(wakeup_fd_, &one, sizeof(one));
        if (result == static_cast<ssize_t>(sizeof(one)))
            return;
        if (result == -1 && errno == EINTR)
            continue;
        if (result == -1 && errno == EAGAIN)
            return;
        return;
    }
}

void EventLoop::handle_wakeup(uint32_t events)
{
    if (!(events & EPOLLIN))
        return;
    uint64_t value = 0;
    while (1)
    {
        ssize_t n = read(wakeup_fd_, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value)))
        {
            continue;
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else if (n == -1 && errno == EAGAIN)
        {
            return;
        }
        return;
    }
}

EventLoop::~EventLoop()
{
    if (wakeup_fd_ != -1)
    {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

void EventLoop::queue_in_loop(Functor functor)
{
    if (!functor)
        return;
    {
        std::lock_guard<std::mutex> lock(pending_functors_mutex_);
        pending_functors_.push_back(std::move(functor));
    }
    // if (!is_in_loop_thread())
    // {
    //     wakeup();
    // }
    wakeup();
}

void EventLoop::do_pending_functors()
{
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(pending_functors_mutex_);
        functors.swap(pending_functors_);
    }
    for (auto &functor : functors)
        functor();
}

void EventLoop::run_in_loop(Functor functor)
{
    if (!functor)
    {
        return;
    }
    if (is_in_loop_thread())
    {
        functor();
    }
    else
    {
        queue_in_loop(std::move(functor));
    }
}

bool EventLoop::valid() const
{
    return epoll_fd_ != -1 && wakeup_fd_ != -1;
}

bool EventLoop::is_in_loop_thread() const
{
    return owner_thread_id_ == std::this_thread::get_id();
}

bool EventLoop::add_fd(int fd, uint32_t events, EventCallback callback)
{
    if (!valid() || fd < 0 || !callback)
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
    if (!valid() || fd < 0)
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
    if (!valid() || fd < 0)
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
        do_pending_functors();
    }
    looping_ = false;
    return true;
}

void EventLoop::quit()
{
    quit_.store(true);
    if (!is_in_loop_thread())
    {
        wakeup();
    }
}
