#ifndef EVENT_LOOP_THREAD_H
#define EVENT_LOOP_THREAD_H

#include"event_loop.h"
#include<condition_variable>
#include <thread>
#include <mutex>

class EventLoopThread
{
public:
    EventLoopThread();
    ~EventLoopThread();
    EventLoopThread(const EventLoopThread &) = delete;
    EventLoopThread &operator=(const EventLoopThread &) = delete;
    EventLoop *start_loop();
private:
    void thread_func();
    std::thread thread_;
    EventLoop *loop_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool started_;
    bool start_failed_;
};

#endif
