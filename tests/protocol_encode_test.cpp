#include "protocol.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
#define REQUIRE(condition)                                                                                              \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!(condition))                                                                                               \
        {                                                                                                               \
            std::cerr << "[FAIL] " << __func__ << ":" << __LINE__ << " " << #condition << '\n';                       \
            return false;                                                                                               \
        }                                                                                                               \
    } while (false)

    void append_expected_u16(std::string &data, std::uint16_t value)
    {
        data.push_back(static_cast<char>((value >> 8) & 0xff));
        data.push_back(static_cast<char>(value & 0xff));
    }

    void append_expected_u32(std::string &data, std::uint32_t value)
    {
        data.push_back(static_cast<char>((value >> 24) & 0xff));
        data.push_back(static_cast<char>((value >> 16) & 0xff));
        data.push_back(static_cast<char>((value >> 8) & 0xff));
        data.push_back(static_cast<char>(value & 0xff));
    }

    void append_expected_i32(std::string &data, std::int32_t value)
    {
        append_expected_u32(data, static_cast<std::uint32_t>(value));
    }

    void append_expected_u64(std::string &data, std::uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            data.push_back(static_cast<char>((value >> shift) & 0xff));
        }
    }

    bool test_join_ok_layout_and_binary_name()
    {
        const std::string token = "0123456789abcdef0123456789abcdef";
        const std::string binary_name("A\0B", 3);
        const std::vector<MemberInfo> members = {
            {0x1112131415161718ULL, binary_name},
            {0x2122232425262728ULL, "xy"}};

        std::string payload = "stale";
        REQUIRE(Protocol::encode_join_ok(0x01020304U, 0x0102030405060708ULL, token, members, payload));

        std::string expected;
        append_expected_u32(expected, 0x01020304U);
        append_expected_u64(expected, 0x0102030405060708ULL);
        append_expected_u16(expected, static_cast<std::uint16_t>(token.size()));
        expected.append(token);
        append_expected_u16(expected, 2);

        append_expected_u64(expected, 0x1112131415161718ULL);
        append_expected_u16(expected, 3);
        expected.append(binary_name);

        append_expected_u64(expected, 0x2122232425262728ULL);
        append_expected_u16(expected, 2);
        expected.append("xy");

        REQUIRE(payload == expected);

        std::cout << "[PASS] join_ok_layout_and_binary_name\n";
        return true;
    }

    bool test_join_ok_empty_members_and_failures()
    {
        const std::string token = "0123456789abcdef0123456789abcdef";

        std::string payload = "stale";
        REQUIRE(Protocol::encode_join_ok(7, 9, token, {}, payload));

        std::string expected;
        append_expected_u32(expected, 7);
        append_expected_u64(expected, 9);
        append_expected_u16(expected, static_cast<std::uint16_t>(token.size()));
        expected.append(token);
        append_expected_u16(expected, 0);
        REQUIRE(payload == expected);

        payload = "stale";
        REQUIRE(!Protocol::encode_join_ok(7, 9, "", {}, payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_join_ok(7, 9, std::string(Protocol::MAX_TOKEN_SIZE + 1, 't'), {}, payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_join_ok(7, 9, token, {{1, ""}}, payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_join_ok(7, 9, token, {{1, std::string(Protocol::MAX_PLAYER_NAME_SIZE + 1, 'x')}}, payload));
        REQUIRE(payload.empty());

        const std::size_t invalid_count = static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
        const std::vector<MemberInfo> too_many_members(invalid_count);

        payload = "stale";
        REQUIRE(!Protocol::encode_join_ok(7, 9, token, too_many_members, payload));
        REQUIRE(payload.empty());

        std::cout << "[PASS] join_ok_empty_members_and_failures\n";
        return true;
    }

    bool test_resume_ok()
    {
        const std::string binary_name("A\0B", 3);
        const std::vector<MemberInfo> members = {
            {0x1112131415161718ULL, binary_name}};
        const std::vector<PlayerGameState> states = {
            {0x1112131415161718ULL, -2, 3, 90}};

        std::string payload = "stale";
        REQUIRE(Protocol::encode_resume_ok(
            0x01020304U,
            0x0102030405060708ULL,
            Roomstatemachine::States::running,
            0x2122232425262728ULL,
            members,
            states,
            payload));

        std::string expected;
        append_expected_u32(expected, 0x01020304U);
        append_expected_u64(expected, 0x0102030405060708ULL);
        append_expected_u16(expected, static_cast<std::uint16_t>(Roomstatemachine::States::running));
        append_expected_u64(expected, 0x2122232425262728ULL);

        append_expected_u16(expected, 1);
        append_expected_u64(expected, 0x1112131415161718ULL);
        append_expected_u16(expected, 3);
        expected.append(binary_name);

        append_expected_u16(expected, 1);
        append_expected_u64(expected, 0x1112131415161718ULL);
        append_expected_i32(expected, -2);
        append_expected_i32(expected, 3);
        append_expected_i32(expected, 90);

        REQUIRE(payload == expected);

        payload = "stale";
        REQUIRE(!Protocol::encode_resume_ok(
            1,
            1,
            Roomstatemachine::States::waiting,
            0,
            {{1, ""}},
            {},
            payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_resume_ok(
            1,
            1,
            Roomstatemachine::States::waiting,
            0,
            {},
            {{0, 0, 0, 100}},
            payload));
        REQUIRE(payload.empty());

        std::cout << "[PASS] resume_ok\n";
        return true;
    }

    bool test_player_joined()
    {
        const std::string binary_name("X\0Y", 3);

        std::string payload = "stale";
        REQUIRE(Protocol::encode_player_joined(11, 13, binary_name, payload));

        std::string expected;
        append_expected_u32(expected, 11);
        append_expected_u64(expected, 13);
        append_expected_u16(expected, 3);
        expected.append(binary_name);
        REQUIRE(payload == expected);

        const std::string max_name(Protocol::MAX_PLAYER_NAME_SIZE, 'n');
        REQUIRE(Protocol::encode_player_joined(11, 13, max_name, payload));

        expected.clear();
        append_expected_u32(expected, 11);
        append_expected_u64(expected, 13);
        append_expected_u16(expected, static_cast<std::uint16_t>(max_name.size()));
        expected.append(max_name);
        REQUIRE(payload == expected);

        payload = "stale";
        REQUIRE(!Protocol::encode_player_joined(11, 13, "", payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_player_joined(11, 13, std::string(Protocol::MAX_PLAYER_NAME_SIZE + 1, 'n'), payload));
        REQUIRE(payload.empty());

        std::cout << "[PASS] player_joined\n";
        return true;
    }

    bool test_leave_responses()
    {
        std::string payload = "stale";
        REQUIRE(Protocol::encode_leave_ok(0x01020304U, payload));

        std::string expected;
        append_expected_u32(expected, 0x01020304U);
        REQUIRE(payload == expected);

        REQUIRE(Protocol::encode_player_left(0x11121314U, 0x2122232425262728ULL, payload));

        expected.clear();
        append_expected_u32(expected, 0x11121314U);
        append_expected_u64(expected, 0x2122232425262728ULL);
        REQUIRE(payload == expected);

        std::cout << "[PASS] leave_responses\n";
        return true;
    }

    bool test_chat_event()
    {
        const std::string binary_message("hello\0world", 11);

        std::string payload = "stale";
        REQUIRE(Protocol::encode_chat_event(17, 19, binary_message, payload));

        std::string expected;
        append_expected_u32(expected, 17);
        append_expected_u64(expected, 19);
        append_expected_u16(expected, static_cast<std::uint16_t>(binary_message.size()));
        expected.append(binary_message);
        REQUIRE(payload == expected);

        const std::string max_message(Protocol::MAX_CHAT_MESSAGE_SIZE, 'm');
        REQUIRE(Protocol::encode_chat_event(17, 19, max_message, payload));

        expected.clear();
        append_expected_u32(expected, 17);
        append_expected_u64(expected, 19);
        append_expected_u16(expected, static_cast<std::uint16_t>(max_message.size()));
        expected.append(max_message);
        REQUIRE(payload == expected);

        payload = "stale";
        REQUIRE(!Protocol::encode_chat_event(17, 19, "", payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_chat_event(17, 19, std::string(Protocol::MAX_CHAT_MESSAGE_SIZE + 1, 'm'), payload));
        REQUIRE(payload.empty());

        std::cout << "[PASS] chat_event\n";
        return true;
    }

    bool test_error()
    {
        const std::vector<MessageType> request_types = {
            MessageType::join,
            MessageType::leave,
            MessageType::chat,
            MessageType::move,
            MessageType::attack,
            MessageType::resume};

        const std::vector<ErrorCode> error_codes = {
            ErrorCode::room_not_found,
            ErrorCode::room_full,
            ErrorCode::already_in_room,
            ErrorCode::not_in_room,
            ErrorCode::invalid_player_name,
            ErrorCode::invalid_message,
            ErrorCode::player_id_exhausted,
            ErrorCode::room_not_joinable,
            ErrorCode::room_not_running,
            ErrorCode::already_submitted,
            ErrorCode::invalid_token,
            ErrorCode::session_online,
            ErrorCode::session_expired,
            ErrorCode::resume_failed};

        for (MessageType request_type : request_types)
        {
            for (ErrorCode error_code : error_codes)
            {
                std::string payload = "stale";
                REQUIRE(Protocol::encode_error(request_type, error_code, payload));

                std::string expected;
                append_expected_u16(expected, static_cast<std::uint16_t>(request_type));
                append_expected_u16(expected, static_cast<std::uint16_t>(error_code));
                REQUIRE(payload == expected);
            }
        }

        std::string payload = "stale";
        REQUIRE(!Protocol::encode_error(MessageType::join_ok, ErrorCode::room_not_found, payload));
        REQUIRE(payload.empty());

        payload = "stale";
        REQUIRE(!Protocol::encode_error(MessageType::join, static_cast<ErrorCode>(999), payload));
        REQUIRE(payload.empty());

        std::cout << "[PASS] error\n";
        return true;
    }
}

int main()
{
    if (!test_join_ok_layout_and_binary_name())
    {
        return 1;
    }
    if (!test_join_ok_empty_members_and_failures())
    {
        return 1;
    }
    if (!test_player_joined())
    {
        return 1;
    }
    if (!test_leave_responses())
    {
        return 1;
    }
    if (!test_chat_event())
    {
        return 1;
    }
    if (!test_resume_ok())
    {
        return 1;
    }
    if (!test_error())
    {
        return 1;
    }

    std::cout << "Protocol 响应编码验收通过\n";
    return 0;
}
