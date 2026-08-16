#include "room_manager.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
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

    bool contains_connection(const std::vector<Connection::ConnectionPtr> &connections, const Connection::ConnectionPtr &target)
    {
        return std::any_of(connections.begin(), connections.end(), [&target](const Connection::ConnectionPtr &connection)
        {
            return connection.get() == target.get();
        });
    }

    bool contains_member(const std::vector<MemberInfo> &members, std::uint64_t player_id, const std::string &player_name)
    {
        return std::any_of(members.begin(), members.end(), [player_id, &player_name](const MemberInfo &member)
        {
            return member.player_id == player_id && member.player_name == player_name;
        });
    }

    bool test_room_registration_join_and_start()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();

        CHECK(manager.room_count() == 0);
        CHECK(!manager.contains_room(1));

        CHECK(manager.add_room(1, 2));
        CHECK(manager.contains_room(1));
        CHECK(!manager.add_room(1, 8));
        CHECK(manager.add_room(2, 0));
        CHECK(manager.room_count() == 2);

        CHECK(manager.join(Connection::ConnectionPtr{}, 1, "null").state == RoomManager::States::invalid_connection);
        CHECK(manager.join(connection_a, 99, "Alice").state == RoomManager::States::room_not_found);
        CHECK(manager.join(connection_a, 1, "").state == RoomManager::States::invalid_player_name);
        CHECK(manager.join(connection_a, 2, "Alice").state == RoomManager::States::room_full);

        RoomManager::JoinResult join_a = manager.join(connection_a, 1, "Alice");
        CHECK(join_a.state == RoomManager::States::success);
        CHECK(join_a.room_id == 1);
        CHECK(join_a.player_id == 1);
        CHECK(join_a.members.empty());
        CHECK(join_a.notify_connections.empty());

        CHECK(manager.start_if_full(1) == RoomManager::States::success);

        RoomManager::JoinResult join_b = manager.join(connection_b, 1, "Bob");
        CHECK(join_b.state == RoomManager::States::success);
        CHECK(join_b.room_id == 1);
        CHECK(join_b.player_id == 2);
        CHECK(join_b.members.size() == 1);
        CHECK(contains_member(join_b.members, 1, "Alice"));
        CHECK(join_b.notify_connections.size() == 1);
        CHECK(contains_connection(join_b.notify_connections, connection_a));

        CHECK(manager.start_if_full(1) == RoomManager::States::success);

        RoomManager::JoinResult late_join = manager.join(connection_c, 1, "Carol");
        CHECK(late_join.state == RoomManager::States::room_not_joinable);

        return true;
    }

    bool test_detach_bind_and_stale_connection()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();
        auto connection_d = make_connection();

        CHECK(manager.add_room(7, 2));

        RoomManager::JoinResult join_a = manager.join(connection_a, 7, "Alice");
        RoomManager::JoinResult join_b = manager.join(connection_b, 7, "Bob");

        CHECK(join_a.state == RoomManager::States::success);
        CHECK(join_b.state == RoomManager::States::success);
        CHECK(manager.start_if_full(7) == RoomManager::States::success);

        CHECK(manager.detach_connection(7, join_a.player_id, connection_c).state == RoomManager::Bindingstates::connection_mismatch);
        CHECK(manager.detach_connection(7, join_a.player_id, connection_a).state == RoomManager::Bindingstates::success);
        CHECK(manager.detach_connection(7, join_a.player_id, connection_a).state == RoomManager::Bindingstates::not_bound);

        RoomManager::BindingResult bind_result = manager.bind_connection(7, join_a.player_id, connection_c);

        CHECK(bind_result.state == RoomManager::Bindingstates::success);
        CHECK(bind_result.room_id == 7);
        CHECK(bind_result.player_id == join_a.player_id);
        CHECK(bind_result.room_state == Roomstatemachine::States::running);
        CHECK(bind_result.tick_id == 0);
        CHECK(bind_result.members.size() == 2);
        CHECK(bind_result.snapshot.size() == 2);
        CHECK(contains_member(bind_result.members, join_a.player_id, "Alice"));
        CHECK(contains_member(bind_result.members, join_b.player_id, "Bob"));

        CHECK(manager.bind_connection(7, join_a.player_id, connection_d).state == RoomManager::Bindingstates::already_bound);

        CHECK(manager.detach_connection(7, join_a.player_id, connection_a).state == RoomManager::Bindingstates::connection_mismatch);

        RoomManager::ChatResult chat_result = manager.chat(7, join_a.player_id, "hello");

        CHECK(chat_result.state == RoomManager::States::success);
        CHECK(chat_result.notify_connections.size() == 2);
        CHECK(contains_connection(chat_result.notify_connections, connection_b));
        CHECK(contains_connection(chat_result.notify_connections, connection_c));

        CHECK(manager.detach_connection(7, join_a.player_id, connection_c).state == RoomManager::Bindingstates::success);
        CHECK(manager.bind_connection(7, join_a.player_id, connection_d).state == RoomManager::Bindingstates::success);

        RoomManager::LeaveResult leave_result = manager.leave(7, join_a.player_id);

        CHECK(leave_result.state == RoomManager::States::success);
        CHECK(leave_result.room_id == 7);
        CHECK(leave_result.player_id == join_a.player_id);
        CHECK(leave_result.notify_connections.size() == 1);
        CHECK(contains_connection(leave_result.notify_connections, connection_b));

        CHECK(manager.leave(7, join_a.player_id).state == RoomManager::States::not_in_room);
        CHECK(manager.bind_connection(7, join_a.player_id, connection_a).state == RoomManager::Bindingstates::player_not_found);

        return true;
    }

    bool test_stable_identity_commands()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        CHECK(manager.add_room(10, 2));

        RoomManager::JoinResult join_a = manager.join(connection_a, 10, "Alice");
        RoomManager::JoinResult join_b = manager.join(connection_b, 10, "Bob");

        CHECK(join_a.state == RoomManager::States::success);
        CHECK(join_b.state == RoomManager::States::success);

        CHECK(manager.move(10, join_a.player_id, 1, 0).state == RoomManager::States::room_not_running);
        CHECK(manager.start_if_full(10) == RoomManager::States::success);

        CHECK(manager.move(10, join_a.player_id, 1, 0).state == RoomManager::States::success);
        CHECK(manager.move(10, join_a.player_id, 1, 0).state == RoomManager::States::already_submitted);

        std::vector<RoomManager::TickResult> ticks = manager.tick_rooms();

        CHECK(ticks.size() == 1);
        CHECK(ticks.front().room_id == 10);
        CHECK(ticks.front().tick_id == 1);
        CHECK(ticks.front().processed_commands == 1);
        CHECK(ticks.front().successful_commands == 1);

        CHECK(manager.attack(10, join_a.player_id, join_b.player_id).state == RoomManager::States::success);

        ticks = manager.tick_rooms();

        CHECK(ticks.size() == 1);
        CHECK(ticks.front().tick_id == 2);
        CHECK(ticks.front().processed_commands == 1);
        CHECK(ticks.front().successful_commands == 1);

        CHECK(manager.chat(99, join_a.player_id, "hello").state == RoomManager::States::room_not_found);
        CHECK(manager.chat(10, 999, "hello").state == RoomManager::States::not_in_room);
        CHECK(manager.chat(10, join_a.player_id, "").state == RoomManager::States::invalid_message);

        return true;
    }

    bool test_repeated_lifecycle()
    {
        for (int i = 0; i < 100; ++i)
        {
            RoomManager manager;
            auto connection_a = make_connection();
            auto connection_b = make_connection();
            auto connection_c = make_connection();

            CHECK(manager.add_room(1, 2));

            RoomManager::JoinResult join_a = manager.join(connection_a, 1, "A-" + std::to_string(i));
            RoomManager::JoinResult join_b = manager.join(connection_b, 1, "B-" + std::to_string(i));

            CHECK(join_a.state == RoomManager::States::success);
            CHECK(join_b.state == RoomManager::States::success);
            CHECK(manager.start_if_full(1) == RoomManager::States::success);

            CHECK(manager.detach_connection(1, join_a.player_id, connection_a).state == RoomManager::Bindingstates::success);
            CHECK(manager.bind_connection(1, join_a.player_id, connection_c).state == RoomManager::Bindingstates::success);
            CHECK(manager.detach_connection(1, join_a.player_id, connection_a).state == RoomManager::Bindingstates::connection_mismatch);

            CHECK(manager.leave(1, join_a.player_id).state == RoomManager::States::success);
            CHECK(manager.leave(1, join_b.player_id).state == RoomManager::States::success);
        }

        return true;
    }
}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"room_registration_join_and_start", test_room_registration_join_and_start},
        {"detach_bind_and_stale_connection", test_detach_bind_and_stale_connection},
        {"stable_identity_commands", test_stable_identity_commands},
        {"repeated_lifecycle", test_repeated_lifecycle}
    };

    for (const auto &test : tests)
    {
        bool passed = false;

        try
        {
            passed = test.second();
        }
        catch (const std::exception &exception)
        {
            std::cerr << "[FAIL] " << test.first << ": " << exception.what() << '\n';
            return 1;
        }
        catch (...)
        {
            std::cerr << "[FAIL] " << test.first << ": unknown exception\n";
            return 1;
        }

        if (!passed)
        {
            std::cerr << "[FAIL] " << test.first << '\n';
            return 1;
        }

        std::cout << "[PASS] " << test.first << '\n';
    }

    std::cout << "RoomManager 稳定身份验收通过\n";
    return 0;
}
