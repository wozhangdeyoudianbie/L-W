#include "protocol.h"
#include <arpa/inet.h>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
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

    void append_u16(std::string &payload, std::uint16_t value)
    {
        std::uint16_t network_value = htons(value);
        payload.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    void append_u32(std::string &payload, std::uint32_t value)
    {
        std::uint32_t network_value = htonl(value);
        payload.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    bool test_join_normal_and_binary_name()
    {
        std::string player_name("A\0B", 3);
        std::string payload;
        append_u32(payload, 7);
        append_u16(payload, static_cast<std::uint16_t>(player_name.size()));
        payload.append(player_name);

        JoinRequest request{99, "old"};
        CHECK(Protocol::decode_join_request(payload, request));
        CHECK(request.room_id == 7);
        CHECK(request.player_name == player_name);

        return true;
    }

    bool test_join_business_errors_are_structurally_valid()
    {
        std::string empty_name_payload;
        append_u32(empty_name_payload, 0);
        append_u16(empty_name_payload, 0);

        JoinRequest request{99, "old"};
        CHECK(Protocol::decode_join_request(empty_name_payload, request));
        CHECK(request.room_id == 0);
        CHECK(request.player_name.empty());

        std::string oversized_name(Protocol::MAX_PLAYER_NAME_SIZE + 1, 'n');
        std::string oversized_name_payload;
        append_u32(oversized_name_payload, 8);
        append_u16(oversized_name_payload, static_cast<std::uint16_t>(oversized_name.size()));
        oversized_name_payload.append(oversized_name);

        CHECK(Protocol::decode_join_request(oversized_name_payload, request));
        CHECK(request.room_id == 8);
        CHECK(request.player_name == oversized_name);

        return true;
    }

    bool test_join_invalid_layout_and_unchanged_output()
    {
        JoinRequest request{99, "old"};

        std::string missing_room_id(3, '\0');
        CHECK(!Protocol::decode_join_request(missing_room_id, request));
        CHECK(request.room_id == 99);
        CHECK(request.player_name == "old");

        std::string missing_name_size;
        append_u32(missing_name_size, 7);
        CHECK(!Protocol::decode_join_request(missing_name_size, request));
        CHECK(request.room_id == 99);
        CHECK(request.player_name == "old");

        std::string truncated_name;
        append_u32(truncated_name, 7);
        append_u16(truncated_name, 3);
        truncated_name.append("ab");
        CHECK(!Protocol::decode_join_request(truncated_name, request));
        CHECK(request.room_id == 99);
        CHECK(request.player_name == "old");

        std::string trailing_bytes;
        append_u32(trailing_bytes, 7);
        append_u16(trailing_bytes, 1);
        trailing_bytes.append("ab");
        CHECK(!Protocol::decode_join_request(trailing_bytes, request));
        CHECK(request.room_id == 99);
        CHECK(request.player_name == "old");

        return true;
    }

    bool test_leave_layout()
    {
        CHECK(Protocol::decode_leave_request(""));
        CHECK(!Protocol::decode_leave_request(std::string(1, '\0')));
        CHECK(!Protocol::decode_leave_request("leave"));

        return true;
    }

    bool test_chat_normal_and_binary_message()
    {
        std::string message("hello\0world", 11);
        std::string payload;
        append_u16(payload, static_cast<std::uint16_t>(message.size()));
        payload.append(message);

        ChatRequest request{"old"};
        CHECK(Protocol::decode_chat_request(payload, request));
        CHECK(request.message == message);

        return true;
    }

    bool test_chat_business_errors_are_structurally_valid()
    {
        std::string empty_message_payload;
        append_u16(empty_message_payload, 0);

        ChatRequest request{"old"};
        CHECK(Protocol::decode_chat_request(empty_message_payload, request));
        CHECK(request.message.empty());

        std::string oversized_message(Protocol::MAX_CHAT_MESSAGE_SIZE + 1, 'm');
        std::string oversized_message_payload;
        append_u16(oversized_message_payload, static_cast<std::uint16_t>(oversized_message.size()));
        oversized_message_payload.append(oversized_message);

        CHECK(Protocol::decode_chat_request(oversized_message_payload, request));
        CHECK(request.message == oversized_message);

        return true;
    }

    bool test_chat_invalid_layout_and_unchanged_output()
    {
        ChatRequest request{"old"};

        CHECK(!Protocol::decode_chat_request("", request));
        CHECK(request.message == "old");

        std::string half_size(1, '\0');
        CHECK(!Protocol::decode_chat_request(half_size, request));
        CHECK(request.message == "old");

        std::string truncated_message;
        append_u16(truncated_message, 3);
        truncated_message.append("ab");
        CHECK(!Protocol::decode_chat_request(truncated_message, request));
        CHECK(request.message == "old");

        std::string trailing_bytes;
        append_u16(trailing_bytes, 1);
        trailing_bytes.append("ab");
        CHECK(!Protocol::decode_chat_request(trailing_bytes, request));
        CHECK(request.message == "old");

        return true;
    }

    bool test_repeated_decode()
    {
        for (std::uint32_t i = 1;i <= 100;i++)
        {
            std::string player_name = "player-" + std::to_string(i);
            std::string join_payload;
            append_u32(join_payload, i);
            append_u16(join_payload, static_cast<std::uint16_t>(player_name.size()));
            join_payload.append(player_name);

            JoinRequest join_request{0, ""};
            CHECK(Protocol::decode_join_request(join_payload, join_request));
            CHECK(join_request.room_id == i);
            CHECK(join_request.player_name == player_name);

            std::string message = "message-" + std::to_string(i);
            std::string chat_payload;
            append_u16(chat_payload, static_cast<std::uint16_t>(message.size()));
            chat_payload.append(message);

            ChatRequest chat_request{""};
            CHECK(Protocol::decode_chat_request(chat_payload, chat_request));
            CHECK(chat_request.message == message);
        }

        return true;
    }

}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"join_normal_and_binary_name", test_join_normal_and_binary_name},
        {"join_business_errors_are_structurally_valid", test_join_business_errors_are_structurally_valid},
        {"join_invalid_layout_and_unchanged_output", test_join_invalid_layout_and_unchanged_output},
        {"leave_layout", test_leave_layout},
        {"chat_normal_and_binary_message", test_chat_normal_and_binary_message},
        {"chat_business_errors_are_structurally_valid", test_chat_business_errors_are_structurally_valid},
        {"chat_invalid_layout_and_unchanged_output", test_chat_invalid_layout_and_unchanged_output},
        {"repeated_decode", test_repeated_decode}
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

    std::cout << "Protocol 请求解码验收通过\n";
    return 0;
}
