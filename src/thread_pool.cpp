#include "../include/thread_pool.h"

// 构造：创建 worker 线程
ThreadPool::ThreadPool(int thread_count)
{
    stop = false;
    if (thread_count <= 0)
    {
        thread_count = 1;
    }
    for (int i = 0; i < thread_count; i++)
    {
        workers.emplace_back([this]()
        {
            this->worker_loop();
        });
    }
}

// 析构：停止并回收线程
ThreadPool::~ThreadPool()
{
    {
        unique_lock<mutex> lock(mtx);
        stop = true;
    }
    cv.notify_all();
    for (auto &worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

// 添加任务
void ThreadPool::add_task(function<void()> task)
{
    {
        unique_lock<mutex> lock(mtx);
        if (stop)
        {
            return;
        }
        tasks.emplace(task);
    }
    cv.notify_one();
}

// 工作线程循环
void ThreadPool::worker_loop()
{
    while (true)
    {
        function<void()> task;
        {
            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [this]()
            {
                return stop || !tasks.empty();
            });
            if (stop && tasks.empty())
            {
                return;
            }
            task = tasks.front();
            tasks.pop();
        }
        task();
    }
}
