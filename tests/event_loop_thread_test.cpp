#include "event_loop_thread.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#define CHECK(expression)                                              \
    do                                                                 \
    {                                                                  \
        if (!(expression))                                             \
        {                                                              \
            std::cerr << "[FAIL] " << #expression                      \
                      << " at line " << __LINE__ << std::endl;         \
            return false;                                              \
        }                                                              \
    } while (false)

bool test_start_and_owner_thread()
{
    std::mutex done_mutex;
    std::condition_variable done_condition;

    bool done = false;
    bool owner_ok = false;
    int execution_count = 0;

    std::thread::id main_thread_id = std::this_thread::get_id();
    std::thread::id callback_thread_id;

    EventLoopThread loop_thread;

    EventLoop *loop = loop_thread.start_loop();
    CHECK(loop != nullptr);

    loop->queue_in_loop(
        [loop,
         &done_mutex,
         &done_condition,
         &done,
         &owner_ok,
         &execution_count,
         &callback_thread_id]()
    {
        {
            std::lock_guard<std::mutex> lock(done_mutex);

            owner_ok = loop->is_in_loop_thread();
            callback_thread_id = std::this_thread::get_id();
            ++execution_count;
            done = true;
        }

        done_condition.notify_one();
    });

    std::unique_lock<std::mutex> lock(done_mutex);

    bool finished = done_condition.wait_for(
        lock,
        std::chrono::seconds(2),
        [&done]()
    {
        return done;
    });

    CHECK(finished);
    CHECK(done);
    CHECK(owner_ok);
    CHECK(execution_count == 1);
    CHECK(callback_thread_id != main_thread_id);

    std::cout << "[PASS] start_and_owner_thread" << std::endl;
    return true;
}

bool test_repeated_start()
{
    EventLoopThread loop_thread;

    EventLoop *first_loop = loop_thread.start_loop();
    CHECK(first_loop != nullptr);

    EventLoop *second_loop = loop_thread.start_loop();
    CHECK(second_loop == nullptr);

    std::cout << "[PASS] repeated_start" << std::endl;
    return true;
}

bool test_destructor_stops_idle_loop()
{
    {
        EventLoopThread loop_thread;

        EventLoop *loop = loop_thread.start_loop();
        CHECK(loop != nullptr);
    }

    std::cout << "[PASS] destructor_stops_idle_loop" << std::endl;
    return true;
}

bool test_repeated_construct_destroy()
{
    constexpr int repeat_count = 100;

    for (int i = 0; i < repeat_count; ++i)
    {
        EventLoopThread loop_thread;

        EventLoop *loop = loop_thread.start_loop();
        CHECK(loop != nullptr);
    }

    std::cout << "[PASS] repeated_construct_destroy" << std::endl;
    return true;
}

int main()
{
    if (!test_start_and_owner_thread())
        return 1;

    if (!test_repeated_start())
        return 1;

    if (!test_destructor_stops_idle_loop())
        return 1;

    if (!test_repeated_construct_destroy())
        return 1;

    std::cout << "EventLoopThread 基础验收通过" << std::endl;
    return 0;
}
