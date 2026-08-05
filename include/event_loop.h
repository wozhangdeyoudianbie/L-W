#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/epoll.h>
#include <mutex>

class EventLoop
{
public:
    using EventCallback = std::function<void(uint32_t)>;
    using Functor = std::function<void()>;
    explicit EventLoop(std::size_t max_events = 1024);
    ~EventLoop();
    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;
    bool valid() const;                       // epoll/eventfd 是否创建成功
    bool loop();                              // 进入事件循环（阻塞，须在 loop 线程调用）
    void quit();                              // 退出事件循环
    void run_in_loop(Functor functor);        // 在 loop 线程执行；已在则直接执行
    void queue_in_loop(Functor functor);      // 投递到 loop 线程队列（总是异步）
    bool add_fd(int fd, uint32_t events, EventCallback callback);   // 注册 fd 到 epoll（须在 loop 线程）
    bool update_fd(int fd, uint32_t events);  // 修改 fd 监听的事件
    bool remove_fd(int fd);                   // 从 epoll 注销 fd
    bool is_in_loop_thread() const;           // 当前线程是否就是 loop 线程
private:
    int epoll_fd_;
    int wakeup_fd_;
    std::atomic<bool> quit_;
    bool looping_;
    std::vector<epoll_event> events_;
    std::unordered_map<int, EventCallback> callbacks_;
    std::thread::id owner_thread_id_;
    std::mutex pending_functors_mutex_;
    std::vector<Functor> pending_functors_;
    void wakeup();
    void do_pending_functors();
    void handle_wakeup(uint32_t events);
};

#endif
