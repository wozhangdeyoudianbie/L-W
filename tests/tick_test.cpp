#include "room_manager.h"
#include "tick_timer.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
    bool check(bool expression, const char *text, int line)
    {
        if (expression)
        {
            return true;
        }
        std::cerr << "[FAIL] " << text << " at line " << line << '\n';
        return false;
    }

#define CHECK(expression) do { if (!check((expression), #expression, __LINE__)) return false; } while (false)

    Connection::ConnectionPtr make_connection()
    {
        return std::make_shared<Connection>(nullptr, -1);
    }

    const PlayerGameState *find_player(const std::vector<PlayerGameState> &states, std::uint64_t player_id)
    {
        auto it = std::find_if(states.begin(), states.end(), [player_id](const PlayerGameState &state)
        {
            return state.player_id == player_id;
        });
        if (it == states.end())
        {
            return nullptr;
        }
        return &*it;
    }

    bool test_room_tick_batch()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        CHECK(manager.add_room(1, 2));

        RoomManager::JoinResult join_a = manager.join(connection_a, 1, "Alice");
        CHECK(join_a.state == RoomManager::States::success);

        CHECK(manager.move(join_a.room_id, join_a.player_id, 1, 0).state == RoomManager::States::room_not_running);

        RoomManager::JoinResult join_b = manager.join(connection_b, 1, "Bob");
        CHECK(join_b.state == RoomManager::States::success);

        CHECK(manager.start_if_full(1) == RoomManager::States::success);

        CHECK(manager.move(join_a.room_id, join_a.player_id, 1, 0).state == RoomManager::States::success);
        CHECK(manager.move(join_a.room_id, join_a.player_id, 1, 0).state == RoomManager::States::already_submitted);

        std::vector<RoomManager::TickResult> results = manager.tick_rooms();
        CHECK(results.size() == 1);

        const RoomManager::TickResult &first_tick = results.front();
        CHECK(first_tick.room_id == 1);
        CHECK(first_tick.tick_id == 1);
        CHECK(first_tick.processed_commands == 1);
        CHECK(first_tick.successful_commands == 1);
        CHECK(first_tick.snapshot.size() == 2);

        const PlayerGameState *alice_state = find_player(first_tick.snapshot, join_a.player_id);
        const PlayerGameState *bob_state = find_player(first_tick.snapshot, join_b.player_id);
        CHECK(alice_state != nullptr);
        CHECK(bob_state != nullptr);
        CHECK(alice_state->x == 1);
        CHECK(alice_state->y == 0);
        CHECK(alice_state->hp == 100);
        CHECK(bob_state->x == 1);
        CHECK(bob_state->y == 0);
        CHECK(bob_state->hp == 100);

        CHECK(manager.attack(join_a.room_id, join_a.player_id, join_b.player_id).state == RoomManager::States::success);
        CHECK(manager.attack(join_a.room_id, join_a.player_id, join_b.player_id).state == RoomManager::States::already_submitted);

        results = manager.tick_rooms();
        CHECK(results.size() == 1);

        const RoomManager::TickResult &second_tick = results.front();
        CHECK(second_tick.tick_id == 2);
        CHECK(second_tick.processed_commands == 1);
        CHECK(second_tick.successful_commands == 1);

        bob_state = find_player(second_tick.snapshot, join_b.player_id);
        CHECK(bob_state != nullptr);
        CHECK(bob_state->hp == 90);

        results = manager.tick_rooms();
        CHECK(results.size() == 1);
        CHECK(results.front().tick_id == 3);
        CHECK(results.front().processed_commands == 0);
        CHECK(results.front().successful_commands == 0);

        return true;
    }

    bool test_tick_timer_lifecycle()
    {
        EventLoop loop;
        CHECK(loop.valid());

        TickTimer invalid_timer(&loop, 0, [](std::uint64_t)
        {
        });
        CHECK(!invalid_timer.valid());

        TickTimer *timer_ptr = nullptr;
        int phase = 0;
        bool callback_ok = true;
        std::uint64_t total_expirations = 0;

        TickTimer timer(&loop, 10, [&](std::uint64_t expirations)
        {
            total_expirations += expirations;
            if (expirations == 0)
            {
                callback_ok = false;
                timer_ptr->stop();
                loop.quit();
                return;
            }

            if (phase == 0)
            {
                const bool stopped = timer_ptr->stop();
                const bool restarted = stopped &&
                    timer_ptr->state() == TickTimer::States::stopped &&
                    timer_ptr->valid() &&
                    timer_ptr->start();

                if (!restarted)
                {
                    callback_ok = false;
                    loop.quit();
                    return;
                }

                phase = 1;
                return;
            }

            if (phase == 1)
            {
                if (!timer_ptr->stop())
                {
                    callback_ok = false;
                }
                phase = 2;
                loop.quit();
            }
        });
        timer_ptr = &timer;

        CHECK(timer.valid());
        CHECK(timer.state() == TickTimer::States::stopped);
        CHECK(timer.start());
        CHECK(timer.state() == TickTimer::States::running);
        CHECK(loop.loop());

        CHECK(callback_ok);
        CHECK(phase == 2);
        CHECK(total_expirations >= 2);
        CHECK(timer.valid());
        CHECK(timer.state() == TickTimer::States::stopped);
        CHECK(timer.stop());

        for (int i = 0; i < 20; ++i)
        {
            TickTimer lifecycle_timer(&loop, 10, [](std::uint64_t)
            {
            });
            CHECK(lifecycle_timer.valid());
            CHECK(lifecycle_timer.start());
        }

        return true;
    }
}

int main()
{
    if (!test_room_tick_batch())
    {
        return 1;
    }
    std::cout << "[PASS] Room Tick 命令批次与权威快照\n";

    if (!test_tick_timer_lifecycle())
    {
        return 1;
    }
    std::cout << "[PASS] TickTimer 停止、重启与生命周期\n";

    std::cout << "Tick 阶段专项测试通过\n";
    return 0;
}
