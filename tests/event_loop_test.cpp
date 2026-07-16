#include "event_loop.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        if (!(condition))                                               \
        {                                                               \
            cerr << "CHECK failed: " << #condition                      \
                 << " at " << __FILE__ << ":" << __LINE__              \
                 << endl;                                               \
            abort();                                                    \
        }                                                               \
    } while (false)

void test_idle_cross_thread_quit()
{
    EventLoop loop;
    CHECK(loop.valid());

    auto begin = chrono::steady_clock::now();

    thread quitter([&loop]()
    {
        this_thread::sleep_for(chrono::milliseconds(100));
        loop.quit();
    });

    CHECK(loop.loop());

    quitter.join();

    auto elapsed = chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - begin);

    CHECK(elapsed < chrono::milliseconds(1000));

    cout << "[PASS] idle_cross_thread_quit" << endl;
}

void test_cross_thread_queue()
{
    EventLoop loop;
    CHECK(loop.valid());

    thread::id owner_thread_id = this_thread::get_id();
    thread::id producer_thread_id;
    thread::id execution_thread_id;

    int execution_count = 0;

    thread producer([&]()
    {
        producer_thread_id = this_thread::get_id();

        loop.queue_in_loop([&]()
        {
            execution_thread_id = this_thread::get_id();
            ++execution_count;
            loop.quit();
        });
    });

    CHECK(loop.loop());

    producer.join();

    CHECK(execution_count == 1);
    CHECK(execution_thread_id == owner_thread_id);
    CHECK(execution_thread_id != producer_thread_id);

    cout << "[PASS] cross_thread_queue" << endl;
}

void test_run_in_loop_and_empty_functor()
{
    EventLoop loop;
    CHECK(loop.valid());

    bool executed = false;

    loop.run_in_loop([&executed]()
    {
        executed = true;
    });

    CHECK(executed);

    EventLoop::Functor empty_functor;

    loop.run_in_loop(empty_functor);
    loop.queue_in_loop(empty_functor);

    cout << "[PASS] run_in_loop_and_empty_functor" << endl;
}

void test_nested_queue()
{
    EventLoop loop;
    CHECK(loop.valid());

    vector<int> order;

    thread producer([&]()
    {
        loop.queue_in_loop([&]()
        {
            order.push_back(1);

            loop.queue_in_loop([&]()
            {
                order.push_back(2);
                loop.quit();
            });
        });
    });

    CHECK(loop.loop());

    producer.join();

    vector<int> expected{1, 2};
    CHECK(order == expected);

    cout << "[PASS] nested_queue" << endl;
}

void test_multiple_producers()
{
    constexpr int producer_count = 8;
    constexpr int tasks_per_producer = 1000;
    constexpr int total_tasks =
        producer_count * tasks_per_producer;

    EventLoop loop;
    CHECK(loop.valid());

    vector<int> hits(total_tasks, 0);
    vector<thread> producers;

    int executed_tasks = 0;
    bool wrong_thread_execution = false;

    for (int producer = 0;
         producer < producer_count;
         ++producer)
    {
        producers.emplace_back([&, producer]()
        {
            for (int task = 0;
                 task < tasks_per_producer;
                 ++task)
            {
                int id =
                    producer * tasks_per_producer + task;

                loop.queue_in_loop([&, id]()
                {
                    if (!loop.is_in_loop_thread())
                    {
                        wrong_thread_execution = true;
                    }

                    ++hits[id];
                    ++executed_tasks;

                    if (executed_tasks == total_tasks)
                    {
                        loop.quit();
                    }
                });
            }
        });
    }

    CHECK(loop.loop());

    for (auto &producer : producers)
    {
        producer.join();
    }

    CHECK(!wrong_thread_execution);
    CHECK(executed_tasks == total_tasks);

    for (int hit : hits)
    {
        CHECK(hit == 1);
    }

    cout << "[PASS] multiple_producers" << endl;
}

void test_quit_drains_current_batch()
{
    EventLoop loop;
    CHECK(loop.valid());

    vector<int> order;

    loop.queue_in_loop([&order]()
    {
        order.push_back(1);
    });

    loop.queue_in_loop([&]()
    {
        order.push_back(2);
        loop.quit();
    });

    loop.queue_in_loop([&order]()
    {
        order.push_back(3);
    });

    CHECK(loop.loop());

    vector<int> expected{1, 2, 3};
    CHECK(order == expected);

    cout << "[PASS] quit_drains_current_batch" << endl;
}

void test_repeated_construct_destroy()
{
    for (int i = 0; i < 2048; ++i)
    {
        EventLoop loop(0);
        CHECK(loop.valid());
    }

    cout << "[PASS] repeated_construct_destroy" << endl;
}

void test_basic_epoll()
{
    int pipe_fds[2];

    int pipe_result = pipe(pipe_fds);
    CHECK(pipe_result == 0);

    int read_fd = pipe_fds[0];
    int write_fd = pipe_fds[1];

    int old_flag = fcntl(read_fd, F_GETFL, 0);
    CHECK(old_flag != -1);

    int nonblocking_result =
        fcntl(read_fd, F_SETFL, old_flag | O_NONBLOCK);

    CHECK(nonblocking_result != -1);

    EventLoop loop;

    CHECK(loop.valid());
    CHECK(loop.is_in_loop_thread());

    const uint32_t event_mask =
        EPOLLIN | EPOLLET | EPOLLONESHOT;

    EventLoop::EventCallback empty_callback;

    CHECK(!loop.add_fd(
        read_fd,
        event_mask,
        empty_callback));

    CHECK(!loop.add_fd(
        -1,
        EPOLLIN,
        [](uint32_t) {}));

    CHECK(!loop.update_fd(-1, EPOLLIN));
    CHECK(!loop.remove_fd(-1));

    int callback_count = 0;
    int nested_loop_rejection_count = 0;

    string received_data;

    const string first_data = "hello ";
    const string second_data = "event loop";

    bool add_result = loop.add_fd(
        read_fd,
        event_mask,
        [&](uint32_t event_type)
    {
        ++callback_count;

        CHECK(callback_count <= 2);
        CHECK((event_type & EPOLLIN) != 0);

        bool nested_loop_result = loop.loop();

        CHECK(!nested_loop_result);

        ++nested_loop_rejection_count;

        char buffer[4];

        while (true)
        {
            ssize_t n =
                read(read_fd, buffer, sizeof(buffer));

            if (n > 0)
            {
                received_data.append(
                    buffer,
                    static_cast<size_t>(n));

                continue;
            }

            if (n == 0)
            {
                break;
            }

            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                break;
            }

            CHECK(false);
        }

        if (callback_count == 1)
        {
            bool update_result =
                loop.update_fd(read_fd, event_mask);

            CHECK(update_result);

            ssize_t written =
                write(
                    write_fd,
                    second_data.data(),
                    second_data.size());

            CHECK(
                written ==
                static_cast<ssize_t>(
                second_data.size()));

            return;
        }

        CHECK(callback_count == 2);

        bool remove_result =
            loop.remove_fd(read_fd);

        CHECK(remove_result);

        loop.quit();
    });

    CHECK(add_result);

    CHECK(!loop.add_fd(
        read_fd,
        event_mask,
        [](uint32_t) {}));

    CHECK(!loop.update_fd(write_fd, EPOLLOUT));
    CHECK(!loop.remove_fd(write_fd));

    bool other_thread_is_owner = true;
    bool wrong_thread_add_result = true;
    bool wrong_thread_update_result = true;
    bool wrong_thread_remove_result = true;
    bool wrong_thread_loop_result = true;

    thread other_thread([&]()
    {
        other_thread_is_owner =
            loop.is_in_loop_thread();

        wrong_thread_add_result =
            loop.add_fd(
                write_fd,
                EPOLLOUT,
                [](uint32_t) {});

        wrong_thread_update_result =
            loop.update_fd(
                read_fd,
                event_mask);

        wrong_thread_remove_result =
            loop.remove_fd(read_fd);

        wrong_thread_loop_result =
            loop.loop();
    });

    other_thread.join();

    CHECK(!other_thread_is_owner);
    CHECK(!wrong_thread_add_result);
    CHECK(!wrong_thread_update_result);
    CHECK(!wrong_thread_remove_result);
    CHECK(!wrong_thread_loop_result);

    ssize_t written =
        write(
            write_fd,
            first_data.data(),
            first_data.size());

    CHECK(
        written ==
        static_cast<ssize_t>(
        first_data.size()));

    CHECK(loop.loop());

    CHECK(callback_count == 2);
    CHECK(nested_loop_rejection_count == 2);
    CHECK(received_data == first_data + second_data);

    close(read_fd);
    close(write_fd);

    cout << "[PASS] basic_epoll" << endl;
}

int main()
{
    test_basic_epoll();
    test_idle_cross_thread_quit();
    test_cross_thread_queue();
    test_run_in_loop_and_empty_functor();
    test_nested_queue();
    test_multiple_producers();
    test_quit_drains_current_batch();
    test_repeated_construct_destroy();

    cout << "EventLoop 全量验收通过" << endl;

    return 0;
}
