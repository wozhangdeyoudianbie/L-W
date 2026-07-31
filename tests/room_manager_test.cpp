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

    bool same_connections(const std::vector<Connection::ConnectionPtr> &actual, const std::vector<Connection::ConnectionPtr> &expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }
        for (const auto &connection : expected)
        {
            if (!contains_connection(actual, connection))
            {
                return false;
            }
        }
        return true;
    }

    bool contains_member(const std::vector<MemberInfo> &members, std::uint64_t player_id, const std::string &player_name)
    {
        return std::any_of(members.begin(), members.end(), [player_id, &player_name](const MemberInfo &member)
        {
            return member.player_id == player_id && member.player_name == player_name;
        });
    }

    bool test_room_registration_and_queries()
    {
        RoomManager manager;
        auto connection = make_connection();

        CHECK(manager.room_count() == 0);
        CHECK(!manager.contains_room(1));
        CHECK(!manager.contains_connection(connection));
        CHECK(!manager.contains_connection(Connection::ConnectionPtr{}));

        CHECK(manager.add_room(1, 2));
        CHECK(manager.contains_room(1));
        CHECK(manager.room_count() == 1);

        CHECK(!manager.add_room(1, 8));
        CHECK(manager.room_count() == 1);

        CHECK(manager.add_room(2, 0));
        CHECK(manager.room_count() == 2);

        RoomManager::JoinResult result = manager.join(connection, 2, "zero-capacity");
        CHECK(result.status == RoomManager::Status::room_full);
        CHECK(!manager.contains_connection(connection));

        return true;
    }

    bool test_join_members_and_notifications()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();
        std::string binary_name("C\0X", 3);

        CHECK(manager.add_room(10, 3));

        RoomManager::JoinResult result_a = manager.join(connection_a, 10, "Alice");
        CHECK(result_a.status == RoomManager::Status::success);
        CHECK(result_a.room_id == 10);
        CHECK(result_a.player_id == 1);
        CHECK(result_a.members.empty());
        CHECK(result_a.notify_connections.empty());
        CHECK(manager.contains_connection(connection_a));

        RoomManager::JoinResult result_b = manager.join(connection_b, 10, "Bob");
        CHECK(result_b.status == RoomManager::Status::success);
        CHECK(result_b.room_id == 10);
        CHECK(result_b.player_id == 2);
        CHECK(result_b.members.size() == 1);
        CHECK(contains_member(result_b.members, 1, "Alice"));
        CHECK(same_connections(result_b.notify_connections, {connection_a}));
        CHECK(manager.contains_connection(connection_b));

        RoomManager::JoinResult result_c = manager.join(connection_c, 10, binary_name);
        CHECK(result_c.status == RoomManager::Status::success);
        CHECK(result_c.room_id == 10);
        CHECK(result_c.player_id == 3);
        CHECK(result_c.members.size() == 2);
        CHECK(contains_member(result_c.members, 1, "Alice"));
        CHECK(contains_member(result_c.members, 2, "Bob"));
        CHECK(same_connections(result_c.notify_connections, {connection_a, connection_b}));
        CHECK(manager.contains_connection(connection_c));

        manager.disconnect(connection_a);
        manager.disconnect(connection_b);
        manager.disconnect(connection_c);
        return true;
    }

    bool test_join_failures_preserve_state()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        CHECK(manager.add_room(1, 1));
        CHECK(manager.add_room(2, 2));

        RoomManager::JoinResult null_result = manager.join(Connection::ConnectionPtr{}, 1, "null");
        CHECK(null_result.status == RoomManager::Status::invalid_connection);

        RoomManager::JoinResult missing_result = manager.join(connection_b, 999, "Bob");
        CHECK(missing_result.status == RoomManager::Status::room_not_found);
        CHECK(!manager.contains_connection(connection_b));

        RoomManager::JoinResult empty_name_result = manager.join(connection_b, 1, "");
        CHECK(empty_name_result.status == RoomManager::Status::invalid_player_name);
        CHECK(!manager.contains_connection(connection_b));

        std::string oversized_name(Protocol::MAX_PLAYER_NAME_SIZE + 1, 'n');
        RoomManager::JoinResult oversized_name_result = manager.join(connection_b, 1, oversized_name);
        CHECK(oversized_name_result.status == RoomManager::Status::invalid_player_name);
        CHECK(!manager.contains_connection(connection_b));

        RoomManager::JoinResult result_a = manager.join(connection_a, 1, "Alice");
        CHECK(result_a.status == RoomManager::Status::success);

        RoomManager::JoinResult room_full_result = manager.join(connection_b, 1, "Bob");
        CHECK(room_full_result.status == RoomManager::Status::room_full);
        CHECK(!manager.contains_connection(connection_b));

        std::string maximum_name(Protocol::MAX_PLAYER_NAME_SIZE, 'm');
        RoomManager::JoinResult result_b = manager.join(connection_b, 2, maximum_name);
        CHECK(result_b.status == RoomManager::Status::success);
        CHECK(manager.contains_connection(connection_b));

        RoomManager::JoinResult duplicate_result = manager.join(connection_a, 2, "Alice-again");
        CHECK(duplicate_result.status == RoomManager::Status::already_in_room);

        RoomManager::ChatResult chat_a = manager.chat(connection_a, "still-in-room-one");
        CHECK(chat_a.status == RoomManager::Status::success);
        CHECK(chat_a.room_id == 1);
        CHECK(chat_a.player_id == result_a.player_id);

        RoomManager::ChatResult chat_b = manager.chat(connection_b, "still-in-room-two");
        CHECK(chat_b.status == RoomManager::Status::success);
        CHECK(chat_b.room_id == 2);
        CHECK(chat_b.player_id == result_b.player_id);

        manager.disconnect(connection_a);
        manager.disconnect(connection_b);
        return true;
    }

    bool test_chat_validation_and_room_isolation()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();
        auto connection_d = make_connection();

        CHECK(manager.add_room(10, 2));
        CHECK(manager.add_room(20, 1));

        RoomManager::JoinResult result_a = manager.join(connection_a, 10, "Alice");
        RoomManager::JoinResult result_b = manager.join(connection_b, 10, "Bob");
        RoomManager::JoinResult result_c = manager.join(connection_c, 20, "Carol");

        CHECK(result_a.status == RoomManager::Status::success);
        CHECK(result_b.status == RoomManager::Status::success);
        CHECK(result_c.status == RoomManager::Status::success);

        RoomManager::ChatResult null_result = manager.chat(Connection::ConnectionPtr{}, "hello");
        CHECK(null_result.status == RoomManager::Status::invalid_connection);

        RoomManager::ChatResult empty_result = manager.chat(connection_a, "");
        CHECK(empty_result.status == RoomManager::Status::invalid_message);
        CHECK(manager.contains_connection(connection_a));

        std::string oversized_message(Protocol::MAX_CHAT_MESSAGE_SIZE + 1, 'x');
        RoomManager::ChatResult oversized_result = manager.chat(connection_a, oversized_message);
        CHECK(oversized_result.status == RoomManager::Status::invalid_message);
        CHECK(manager.contains_connection(connection_a));

        RoomManager::ChatResult not_in_room_result = manager.chat(connection_d, "hello");
        CHECK(not_in_room_result.status == RoomManager::Status::not_in_room);

        std::string binary_message("hello\0room", 10);
        RoomManager::ChatResult chat_a = manager.chat(connection_a, binary_message);
        CHECK(chat_a.status == RoomManager::Status::success);
        CHECK(chat_a.room_id == 10);
        CHECK(chat_a.player_id == result_a.player_id);
        CHECK(same_connections(chat_a.notify_connections, {connection_a, connection_b}));
        CHECK(!contains_connection(chat_a.notify_connections, connection_c));

        std::string maximum_message(Protocol::MAX_CHAT_MESSAGE_SIZE, 'm');
        RoomManager::ChatResult maximum_result = manager.chat(connection_b, maximum_message);
        CHECK(maximum_result.status == RoomManager::Status::success);
        CHECK(same_connections(maximum_result.notify_connections, {connection_a, connection_b}));

        RoomManager::ChatResult chat_c = manager.chat(connection_c, "room-twenty");
        CHECK(chat_c.status == RoomManager::Status::success);
        CHECK(chat_c.room_id == 20);
        CHECK(chat_c.player_id == result_c.player_id);
        CHECK(same_connections(chat_c.notify_connections, {connection_c}));

        manager.disconnect(connection_a);
        manager.disconnect(connection_b);
        manager.disconnect(connection_c);
        return true;
    }

    bool test_leave_and_rejoin()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();

        CHECK(manager.add_room(30, 2));

        RoomManager::JoinResult result_a = manager.join(connection_a, 30, "Alice");
        RoomManager::JoinResult result_b = manager.join(connection_b, 30, "Bob");
        CHECK(result_a.status == RoomManager::Status::success);
        CHECK(result_b.status == RoomManager::Status::success);

        RoomManager::LeaveResult null_result = manager.leave(Connection::ConnectionPtr{});
        CHECK(null_result.status == RoomManager::Status::invalid_connection);

        RoomManager::LeaveResult not_in_room_result = manager.leave(connection_c);
        CHECK(not_in_room_result.status == RoomManager::Status::not_in_room);

        RoomManager::LeaveResult leave_a = manager.leave(connection_a);
        CHECK(leave_a.status == RoomManager::Status::success);
        CHECK(leave_a.room_id == 30);
        CHECK(leave_a.player_id == result_a.player_id);
        CHECK(same_connections(leave_a.notify_connections, {connection_b}));
        CHECK(!manager.contains_connection(connection_a));
        CHECK(manager.contains_connection(connection_b));

        RoomManager::LeaveResult repeated_leave = manager.leave(connection_a);
        CHECK(repeated_leave.status == RoomManager::Status::not_in_room);

        RoomManager::ChatResult chat_b = manager.chat(connection_b, "still-here");
        CHECK(chat_b.status == RoomManager::Status::success);
        CHECK(same_connections(chat_b.notify_connections, {connection_b}));

        RoomManager::JoinResult rejoin_a = manager.join(connection_a, 30, "Alice-again");
        CHECK(rejoin_a.status == RoomManager::Status::success);
        CHECK(rejoin_a.player_id == 3);
        CHECK(rejoin_a.members.size() == 1);
        CHECK(contains_member(rejoin_a.members, result_b.player_id, "Bob"));
        CHECK(same_connections(rejoin_a.notify_connections, {connection_b}));

        manager.disconnect(connection_a);
        manager.disconnect(connection_b);
        return true;
    }

    bool test_disconnect_is_idempotent()
    {
        RoomManager manager;
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();

        CHECK(manager.add_room(40, 2));

        RoomManager::LeaveResult null_result = manager.disconnect(Connection::ConnectionPtr{});
        CHECK(null_result.status == RoomManager::Status::invalid_connection);

        RoomManager::LeaveResult unused_first = manager.disconnect(connection_c);
        CHECK(unused_first.status == RoomManager::Status::success);
        CHECK(unused_first.room_id == 0);
        CHECK(unused_first.player_id == 0);
        CHECK(unused_first.notify_connections.empty());

        RoomManager::LeaveResult unused_second = manager.disconnect(connection_c);
        CHECK(unused_second.status == RoomManager::Status::success);

        RoomManager::JoinResult result_a = manager.join(connection_a, 40, "Alice");
        RoomManager::JoinResult result_b = manager.join(connection_b, 40, "Bob");
        CHECK(result_a.status == RoomManager::Status::success);
        CHECK(result_b.status == RoomManager::Status::success);

        RoomManager::LeaveResult disconnect_a = manager.disconnect(connection_a);
        CHECK(disconnect_a.status == RoomManager::Status::success);
        CHECK(disconnect_a.room_id == 40);
        CHECK(disconnect_a.player_id == result_a.player_id);
        CHECK(same_connections(disconnect_a.notify_connections, {connection_b}));
        CHECK(!manager.contains_connection(connection_a));

        RoomManager::LeaveResult repeated_disconnect = manager.disconnect(connection_a);
        CHECK(repeated_disconnect.status == RoomManager::Status::success);
        CHECK(repeated_disconnect.room_id == 0);
        CHECK(repeated_disconnect.player_id == 0);
        CHECK(repeated_disconnect.notify_connections.empty());

        RoomManager::ChatResult chat_b = manager.chat(connection_b, "still-online");
        CHECK(chat_b.status == RoomManager::Status::success);
        CHECK(chat_b.player_id == result_b.player_id);
        CHECK(same_connections(chat_b.notify_connections, {connection_b}));

        manager.disconnect(connection_b);
        return true;
    }

    bool test_repeated_lifecycle()
    {
        for (int i = 0;i < 100;i++)
        {
            RoomManager manager;
            auto connection_a = make_connection();
            auto connection_b = make_connection();
            auto connection_c = make_connection();

            CHECK(manager.add_room(1, 3));
            CHECK(manager.add_room(2, 1));

            RoomManager::JoinResult result_a = manager.join(connection_a, 1, "A-" + std::to_string(i));
            RoomManager::JoinResult result_b = manager.join(connection_b, 1, "B-" + std::to_string(i));
            RoomManager::JoinResult result_c = manager.join(connection_c, 2, "C-" + std::to_string(i));

            CHECK(result_a.status == RoomManager::Status::success);
            CHECK(result_b.status == RoomManager::Status::success);
            CHECK(result_c.status == RoomManager::Status::success);

            CHECK(manager.chat(connection_a, "message").status == RoomManager::Status::success);
            CHECK(manager.disconnect(connection_a).status == RoomManager::Status::success);
            CHECK(manager.disconnect(connection_a).status == RoomManager::Status::success);
            CHECK(manager.leave(connection_b).status == RoomManager::Status::success);
            CHECK(manager.disconnect(connection_c).status == RoomManager::Status::success);

            CHECK(!manager.contains_connection(connection_a));
            CHECK(!manager.contains_connection(connection_b));
            CHECK(!manager.contains_connection(connection_c));
        }

        return true;
    }

}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"room_registration_and_queries", test_room_registration_and_queries},
        {"join_members_and_notifications", test_join_members_and_notifications},
        {"join_failures_preserve_state", test_join_failures_preserve_state},
        {"chat_validation_and_room_isolation", test_chat_validation_and_room_isolation},
        {"leave_and_rejoin", test_leave_and_rejoin},
        {"disconnect_is_idempotent", test_disconnect_is_idempotent},
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

    std::cout << "RoomManager 全量验收通过\n";
    return 0;
}
