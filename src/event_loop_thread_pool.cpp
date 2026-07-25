#include"event_loop_thread_pool.h"
#include <utility>

EventLoopThreadPool::EventLoopThreadPool(EventLoop *base_loop, std::size_t thread_count)
    :base_loop_(base_loop), thread_count_(thread_count), started_(false), next_(0)
{
}

bool EventLoopThreadPool::start()
{
    if (base_loop_ == nullptr)
    {
        return false;
    }
    if (!base_loop_->valid())
    {
        return false;
    }
    if (!base_loop_->is_in_loop_thread())
    {
        return false;
    }
    if (started_)
    {
        return false;
    }
    std::vector<std::unique_ptr<EventLoopThread>> temporary_threads;
    std::vector<EventLoop *> temporary_loops;
    temporary_threads.reserve(thread_count_);
    temporary_loops.reserve(thread_count_);
    for (std::size_t i = 0;i < thread_count_;i++)
    {
        auto thread = std::make_unique<EventLoopThread>();
        EventLoop *loop = thread->start_loop();
        if (loop == nullptr)
        {
            return false;
        }
        temporary_loops.push_back(loop);
        temporary_threads.push_back(std::move(thread));
    }
    threads_ = std::move(temporary_threads);
    loops_ = std::move(temporary_loops);
    started_ = true;
    next_ = 0;
    return true;
}

EventLoop *EventLoopThreadPool::get_next_loop()
{
    if (!started_)
    {
        return nullptr;
    }
    if (!base_loop_->is_in_loop_thread())
    {
        return nullptr;
    }
    if (loops_.empty())
    {
        return base_loop_;
    }
    EventLoop *result = loops_[next_];
    next_++;
    if (next_ == loops_.size())
    {
        next_ = 0;
    }
    return result;
}

bool EventLoopThreadPool::started() const
{
    return started_;
}

std::size_t EventLoopThreadPool::size() const
{
    return loops_.size();
}
