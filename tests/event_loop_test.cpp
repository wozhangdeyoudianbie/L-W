#include "event_loop.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std;

int main()
{
    int pipe_fds[2];
    assert(pipe(pipe_fds) == 0);

    int read_fd = pipe_fds[0];
    int write_fd = pipe_fds[1];

    int old_flag = fcntl(read_fd, F_GETFL, 0);
    assert(old_flag != -1);

    assert(fcntl(read_fd, F_SETFL, old_flag | O_NONBLOCK) != -1);

    EventLoop loop;

    assert(loop.valid());
    assert(loop.is_in_loop_thread());

    const uint32_t event_mask = EPOLLIN | EPOLLET | EPOLLONESHOT;

    EventLoop::EventCallback empty_callback;

    assert(!loop.add_fd(
        read_fd,
        event_mask,
        empty_callback));

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

        assert(callback_count <= 2);
        assert((event_type & EPOLLIN) != 0);

        bool nested_loop_result = loop.loop();

        assert(!nested_loop_result);

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

            assert(false);
        }

        if (callback_count == 1)
        {
            bool update_result =
                loop.update_fd(read_fd, event_mask);

            assert(update_result);

            ssize_t written =
                write(write_fd,
                      second_data.data(),
                      second_data.size());

            assert(written ==
                   static_cast<ssize_t>(
                   second_data.size()));

            return;
        }

        assert(callback_count == 2);

        bool remove_result =
            loop.remove_fd(read_fd);

        assert(remove_result);

        loop.quit();
    });

    assert(add_result);

    assert(!loop.add_fd(
        read_fd,
        event_mask,
        [](uint32_t) {}));

    assert(!loop.update_fd(write_fd, EPOLLOUT));
    assert(!loop.remove_fd(write_fd));

    bool other_thread_is_owner = true;
    bool wrong_thread_add_result = true;
    bool wrong_thread_update_result = true;
    bool wrong_thread_remove_result = true;
    bool wrong_thread_loop_result = true;

    thread other_thread(
        [&]()
    {
        other_thread_is_owner =
            loop.is_in_loop_thread();

        wrong_thread_add_result =
            loop.add_fd(
                write_fd,
                EPOLLOUT,
                [](uint32_t) {});

        wrong_thread_update_result =
            loop.update_fd(read_fd, event_mask);

        wrong_thread_remove_result =
            loop.remove_fd(read_fd);

        wrong_thread_loop_result =
            loop.loop();
    });

    other_thread.join();

    assert(!other_thread_is_owner);
    assert(!wrong_thread_add_result);
    assert(!wrong_thread_update_result);
    assert(!wrong_thread_remove_result);
    assert(!wrong_thread_loop_result);

    ssize_t written =
        write(write_fd,
              first_data.data(),
              first_data.size());

    assert(written ==
           static_cast<ssize_t>(first_data.size()));

    assert(loop.loop());

    assert(callback_count == 2);
    assert(nested_loop_rejection_count == 2);
    assert(received_data == first_data + second_data);

    close(read_fd);
    close(write_fd);

    cout << "EventLoop 完整基础验收通过" << endl;

    return 0;
}
