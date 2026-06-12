#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

using namespace std;

class ThreadPool
{
private:
    vector<thread> workers;                 // worker threads，工作线程数组
    queue<function<void()>> tasks;          // task queue，任务队列
    mutex mtx;                              // mutex，互斥锁，保护任务队列和 stop
    condition_variable cv;                  // condition variable，条件变量，负责阻塞和唤醒线程
    bool stop;                              // 标记线程池是否准备停止
private:
    void worker_loop();                     // 每个工作线程都会执行这个循环函数
public:
    explicit ThreadPool(int thread_count);  // 构造函数：创建固定数量的工作线程
    ~ThreadPool();                          // 析构函数：关闭线程池并回收线程
    void add_task(function<void()> task);   // 向线程池添加任务
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
};
#endif
