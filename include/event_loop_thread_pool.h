#ifndef EVENT_LOOP_THREAD_POOL_H
#define EVENT_LOOP_THREAD_POOL_H

#include "event_loop.h"
#include "event_loop_thread.h"
#include <cstddef>
#include <memory>
#include <vector>

class EventLoopThreadPool
{
public:
    EventLoopThreadPool(EventLoop *base_loop, std::size_t thread_count);
    ~EventLoopThreadPool() = default;
    EventLoopThreadPool(const EventLoopThreadPool &) = delete;
    EventLoopThreadPool &operator=(const EventLoopThreadPool &) = delete;
    bool start();
    EventLoop *get_next_loop();
    bool started() const;
    std::size_t size() const;
private:
    EventLoop *base_loop_;
    std::size_t thread_count_;
    bool started_;
    std::size_t next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop *> loops_;
};

#endif
