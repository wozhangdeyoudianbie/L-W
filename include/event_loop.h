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

class EventLoop
{
public:
    using EventCallback = std::function<void(uint32_t)>;
    explicit EventLoop(std::size_t max_events = 1024);
    ~EventLoop();
    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;
    bool valid() const;
    bool loop();
    void quit();
    bool add_fd(int fd, uint32_t events, EventCallback callback);
    bool update_fd(int fd, uint32_t events);
    bool remove_fd(int fd);
    bool is_in_loop_thread() const;
private:
    int epoll_fd_;
    std::atomic<bool> quit_;
    bool looping_;
    std::vector<epoll_event> events_;
    std::unordered_map<int, EventCallback> callbacks_;
    std::thread::id owner_thread_id_;
};

#endif
