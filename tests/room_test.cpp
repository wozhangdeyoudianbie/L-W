#include "room.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    Connection::ConnectionPtr make_connection()
    {
        return std::make_shared<Connection>(nullptr, -1);
    }

    bool expect(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << message << '\n';
            return false;
        }
        return true;
    }

    bool has_member(const std::vector<MemberInfo> &members, std::uint64_t player_id, const std::string &player_name)
    {
        for (const MemberInfo &member : members)
        {
            if (member.player_id == player_id && member.player_name == player_name)
            {
                return true;
            }
        }
        return false;
    }

    bool test_normal_join_and_snapshot()
    {
        Room room(100, 2);
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        if (!expect(room.id() == 100, "room id"))
        {
            return false;
        }
        if (!expect(room.capacity() == 2 && room.member_count() == 0, "initial state"))
        {
            return false;
        }

        Room::JoinResult result_a = room.join(connection_a, "alice");
        if (!expect(result_a.status == Room::JoinStatus::success, "A join status"))
        {
            return false;
        }
        if (!expect(result_a.player_id == 1 && result_a.members.empty(), "A join result"))
        {
            return false;
        }

        Room::JoinResult result_b = room.join(connection_b, "bob");
        if (!expect(result_b.status == Room::JoinStatus::success, "B join status"))
        {
            return false;
        }
        if (!expect(result_b.player_id == 2, "B player id"))
        {
            return false;
        }
        if (!expect(result_b.members.size() == 1, "B sees one existing member"))
        {
            return false;
        }
        if (!expect(has_member(result_b.members, 1, "alice"), "B sees A"))
        {
            return false;
        }
        if (!expect(room.member_count() == 2 && room.contains(1) && room.contains(2), "joined state"))
        {
            return false;
        }

        std::vector<Connection::ConnectionPtr> recipients = room.connections(2);
        if (!expect(recipients.size() == 1 && recipients[0] == connection_a, "exclude B"))
        {
            return false;
        }

        return true;
    }

    bool test_failures_leave_and_id_progress()
    {
        Room room(200, 2);
        auto connection_a = make_connection();
        auto connection_b = make_connection();
        auto connection_c = make_connection();

        Room::JoinResult result_a = room.join(connection_a, "alice");
        if (!expect(result_a.player_id == 1, "first id"))
        {
            return false;
        }

        Room::JoinResult null_result = room.join(nullptr, "null");
        if (!expect(null_result.status == Room::JoinStatus::invalid_connection, "null connection"))
        {
            return false;
        }

        Room::JoinResult empty_name = room.join(connection_b, "");
        if (!expect(empty_name.status == Room::JoinStatus::invalid_player_name, "empty name"))
        {
            return false;
        }

        std::string long_name(Protocol::MAX_PLAYER_NAME_SIZE + 1, 'x');
        Room::JoinResult oversized_name = room.join(connection_b, long_name);
        if (!expect(oversized_name.status == Room::JoinStatus::invalid_player_name, "oversized name"))
        {
            return false;
        }

        if (!expect(room.member_count() == 1, "failures do not add members"))
        {
            return false;
        }

        Room::JoinResult result_b = room.join(connection_b, "bob");
        if (!expect(result_b.player_id == 2, "failures do not consume id"))
        {
            return false;
        }

        Room::JoinResult full_result = room.join(connection_c, "carol");
        if (!expect(full_result.status == Room::JoinStatus::room_full, "room full"))
        {
            return false;
        }
        if (!expect(full_result.player_id == 0 && full_result.members.empty(), "full failure result"))
        {
            return false;
        }

        if (!expect(room.leave(1), "leave existing member"))
        {
            return false;
        }
        if (!expect(!room.leave(1), "repeated leave"))
        {
            return false;
        }

        Room::JoinResult result_c = room.join(connection_c, "carol");
        if (!expect(result_c.player_id == 3, "player id is not reused"))
        {
            return false;
        }

        return true;
    }

    bool test_binary_name_and_expired_connection()
    {
        Room room(300, 3);
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        const std::string binary_name("a\0b", 3);
        Room::JoinResult result_a = room.join(connection_a, binary_name);
        Room::JoinResult result_b = room.join(connection_b, "bob");

        if (!expect(result_a.status == Room::JoinStatus::success, "binary name"))
        {
            return false;
        }
        if (!expect(result_b.player_id == 2, "second id"))
        {
            return false;
        }

        connection_b.reset();

        std::vector<Connection::ConnectionPtr> recipients = room.connections();
        if (!expect(recipients.size() == 1 && recipients[0] == connection_a, "expired connection skipped"))
        {
            return false;
        }
        if (!expect(room.member_count() == 2 && room.contains(2), "snapshot does not erase membership"))
        {
            return false;
        }

        if (!expect(room.leave(2) && room.member_count() == 1, "explicit cleanup"))
        {
            return false;
        }

        return true;
    }

    bool test_state_machine_constraints()
    {
        Room room(400, 2);
        auto connection_a = make_connection();
        auto connection_b = make_connection();

        if (!expect(room.state() == Roomstatemachine::States::waiting, "room initially waiting"))
        {
            return false;
        }

        Room::JoinResult result_a = room.join(connection_a, "alice");
        if (!expect(result_a.status == Room::JoinStatus::success, "join before start"))
        {
            return false;
        }

        if (!expect(room.start(false) == Roomstatemachine::Transitionstates::condition_not_met, "start condition not met"))
        {
            return false;
        }
        if (!expect(room.state() == Roomstatemachine::States::waiting, "failed start keeps waiting"))
        {
            return false;
        }

        if (!expect(room.start(true) == Roomstatemachine::Transitionstates::success, "room start"))
        {
            return false;
        }
        if (!expect(room.state() == Roomstatemachine::States::running, "room enters running"))
        {
            return false;
        }

        Room::JoinResult blocked_running = room.join(connection_b, "bob");
        if (!expect(blocked_running.status == Room::JoinStatus::invalid_state, "running room rejects join"))
        {
            return false;
        }
        if (!expect(blocked_running.player_id == 0 && blocked_running.members.empty(), "running rejection result"))
        {
            return false;
        }
        if (!expect(room.member_count() == 1 && !room.contains(2), "running rejection preserves members"))
        {
            return false;
        }

        if (!expect(room.leave(result_a.player_id), "leave remains allowed while running"))
        {
            return false;
        }
        if (!expect(room.member_count() == 0, "running leave cleans membership"))
        {
            return false;
        }

        if (!expect(room.finish(false) == Roomstatemachine::Transitionstates::condition_not_met, "finish condition not met"))
        {
            return false;
        }
        if (!expect(room.state() == Roomstatemachine::States::running, "failed finish keeps running"))
        {
            return false;
        }

        if (!expect(room.finish(true) == Roomstatemachine::Transitionstates::success, "room finish"))
        {
            return false;
        }

        Room::JoinResult blocked_finished = room.join(connection_b, "bob");
        if (!expect(blocked_finished.status == Room::JoinStatus::invalid_state, "finished room rejects join"))
        {
            return false;
        }
        if (!expect(room.state() == Roomstatemachine::States::finished && room.member_count() == 0, "finished rejection preserves room"))
        {
            return false;
        }

        return true;
    }
}

int main()
{
    struct TestCase
    {
        const char *name;
        bool (*function)();
    };

    const TestCase tests[] = {
        {"normal_join_and_snapshot", test_normal_join_and_snapshot},
        {"failures_leave_and_id_progress", test_failures_leave_and_id_progress},
        {"binary_name_and_expired_connection", test_binary_name_and_expired_connection},
        {"state_machine_constraints", test_state_machine_constraints}
    };

    for (const TestCase &test : tests)
    {
        if (!test.function())
        {
            return 1;
        }
        std::cout << "[PASS] " << test.name << '\n';
    }

    std::cout << "Room 基础验收通过\n";
    return 0;
}
