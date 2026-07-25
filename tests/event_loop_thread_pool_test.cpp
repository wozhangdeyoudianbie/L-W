#include "event_loop_thread_pool.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

struct TaskState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    bool owner_ok = false;
    int execution_count = 0;
};

bool task_runs_in_owner_thread(EventLoop *loop)
{
    if (loop == nullptr)
    {
        return false;
    }

    auto state = std::make_shared<TaskState>();

    loop->queue_in_loop(
        [loop, state]()
    {
        {
            std::lock_guard<std::mutex> lock(state->mutex);

            state->owner_ok =
                loop->is_in_loop_thread();

            ++state->execution_count;
            state->done = true;
        }

        state->condition.notify_one();
    });

    std::unique_lock<std::mutex> lock(state->mutex);

    bool finished = state->condition.wait_for(
        lock,
        std::chrono::seconds(2),
        [state]()
    {
        return state->done;
    });

    if (!finished)
    {
        return false;
    }

    return state->owner_ok &&
        state->execution_count == 1;
}

bool test_zero_worker_fallback()
{
    EventLoop base_loop;
    CHECK(base_loop.valid());

    EventLoopThreadPool pool(&base_loop, 0);

    CHECK(!pool.started());
    CHECK(pool.size() == 0);
    CHECK(pool.get_next_loop() == nullptr);

    CHECK(pool.start());
    CHECK(pool.started());
    CHECK(pool.size() == 0);

    CHECK(pool.get_next_loop() == &base_loop);
    CHECK(pool.get_next_loop() == &base_loop);

    CHECK(!pool.start());

    std::cout
        << "[PASS] zero_worker_fallback"
        << std::endl;

    return true;
}

bool test_one_worker()
{
    EventLoop base_loop;
    CHECK(base_loop.valid());

    EventLoopThreadPool pool(&base_loop, 1);

    CHECK(pool.start());
    CHECK(pool.started());
    CHECK(pool.size() == 1);

    EventLoop *first = pool.get_next_loop();
    EventLoop *second = pool.get_next_loop();

    CHECK(first != nullptr);
    CHECK(first != &base_loop);
    CHECK(first == second);

    CHECK(!first->is_in_loop_thread());
    CHECK(task_runs_in_owner_thread(first));

    std::cout
        << "[PASS] one_worker"
        << std::endl;

    return true;
}

bool test_three_worker_round_robin()
{
    EventLoop base_loop;
    CHECK(base_loop.valid());

    EventLoopThreadPool pool(&base_loop, 3);

    CHECK(pool.start());
    CHECK(pool.started());
    CHECK(pool.size() == 3);

    std::vector<EventLoop *> selected;

    for (int i = 0; i < 6; ++i)
    {
        EventLoop *loop = pool.get_next_loop();

        CHECK(loop != nullptr);
        CHECK(loop != &base_loop);

        selected.push_back(loop);
    }

    CHECK(selected[0] != selected[1]);
    CHECK(selected[0] != selected[2]);
    CHECK(selected[1] != selected[2]);

    CHECK(selected[0] == selected[3]);
    CHECK(selected[1] == selected[4]);
    CHECK(selected[2] == selected[5]);

    CHECK(task_runs_in_owner_thread(selected[0]));
    CHECK(task_runs_in_owner_thread(selected[1]));
    CHECK(task_runs_in_owner_thread(selected[2]));

    std::cout
        << "[PASS] three_worker_round_robin"
        << std::endl;

    return true;
}

bool test_wrong_thread_rejected()
{
    EventLoop base_loop;
    CHECK(base_loop.valid());

    EventLoopThreadPool pool(&base_loop, 2);

    bool wrong_start_result = true;

    std::thread wrong_start_thread(
        [&pool, &wrong_start_result]()
    {
        wrong_start_result = pool.start();
    });

    wrong_start_thread.join();

    CHECK(!wrong_start_result);
    CHECK(!pool.started());
    CHECK(pool.size() == 0);

    CHECK(pool.start());
    CHECK(pool.started());
    CHECK(pool.size() == 2);

    EventLoop *wrong_loop = &base_loop;

    std::thread wrong_get_thread(
        [&pool, &wrong_loop]()
    {
        wrong_loop = pool.get_next_loop();
    });

    wrong_get_thread.join();

    CHECK(wrong_loop == nullptr);

    EventLoop *correct_loop =
        pool.get_next_loop();

    CHECK(correct_loop != nullptr);
    CHECK(correct_loop != &base_loop);

    std::cout
        << "[PASS] wrong_thread_rejected"
        << std::endl;

    return true;
}

bool test_repeated_construct_destroy()
{
    constexpr int repeat_count = 30;

    for (int i = 0; i < repeat_count; ++i)
    {
        EventLoop base_loop;
        CHECK(base_loop.valid());

        EventLoopThreadPool pool(
            &base_loop,
            3);

        CHECK(pool.start());
        CHECK(pool.started());
        CHECK(pool.size() == 3);
    }

    std::cout
        << "[PASS] repeated_construct_destroy"
        << std::endl;

    return true;
}

int main()
{
    if (!test_zero_worker_fallback())
        return 1;

    if (!test_one_worker())
        return 1;

    if (!test_three_worker_round_robin())
        return 1;

    if (!test_wrong_thread_rejected())
        return 1;

    if (!test_repeated_construct_destroy())
        return 1;

    std::cout
        << "EventLoopThreadPool 基础验收通过"
        << std::endl;

    return 0;
}
