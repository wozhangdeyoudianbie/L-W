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
    EventLoop *start_loop();   // 启动线程并返回其中的 EventLoop（失败返回 nullptr）
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
