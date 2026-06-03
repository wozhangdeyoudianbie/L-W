#include "../include/thread_pool.h"

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

ThreadPool::~ThreadPool()
{
    {
        unique_lock<mutex> lock(mtx);
        stop = true;
    }

    cv.notify_all();

    for (auto& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

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
