#include"event_loop_thread.h"

// 构造：初始化线程状态
EventLoopThread::EventLoopThread()
    : loop_(nullptr), started_(false), start_failed_(false)
{
}

// 启动线程，返回其 EventLoop
EventLoop *EventLoopThread::start_loop()
{
    if (started_ == true)
        return nullptr;
    started_ = true;
    bool failed = false;
    thread_ = std::thread(&EventLoopThread::thread_func, this);
    EventLoop *result = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]()
        {
            return loop_ != nullptr || start_failed_;
        });
        result = loop_;
        failed = start_failed_;
    }
    if (failed && thread_.joinable())
    {
        thread_.join();
    }
    return result;
}

// 线程入口：创建 loop 并运行
void EventLoopThread::thread_func()
{
    EventLoop loop;
    if (!loop.valid())
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            start_failed_ = true;
        }
        condition_.notify_one();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    condition_.notify_one();
    loop.loop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
}

// 析构：退出并回收线程
EventLoopThread::~EventLoopThread()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_ != nullptr)
        {
            loop_->quit();
        }
    }
    if (thread_.joinable())
    {
        thread_.join();
    }
}
