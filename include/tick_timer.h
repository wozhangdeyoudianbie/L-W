#ifndef TICK_TIMER_H
#define TICK_TIMER_H

#include "event_loop.h"
#include<cstdint>
#include<functional>

class TickTimer
{
public:
    using TickCallback = std::function<void(std::uint64_t)>;
    TickTimer(EventLoop *loop, std::uint64_t interval_milliseconds, TickCallback tick_callback);
    ~TickTimer();
    TickTimer(const TickTimer &) = delete;
    TickTimer &operator=(const TickTimer &) = delete;
    bool start();
    bool stop();
    bool running() const;
private:
    void handle_read(std::uint32_t events);
    EventLoop *loop_;
    std::uint64_t interval_milliseconds_;
    TickCallback tick_callback_;
    int timer_fd_;
    bool running_;
};

#endif
