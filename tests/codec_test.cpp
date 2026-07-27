#include"codec.h"
#include<arpa/inet.h>
#include<cstdint>
#include<cstring>
#include<exception>
#include<functional>
#include<iostream>
#include<string>
#include<utility>
#include<vector>

namespace
{

    struct ReceivedFrame
    {
        std::uint16_t type;
        std::string payload;
    };

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

    Codec::FrameCallback collect_frames(std::vector<ReceivedFrame> &frames)
    {
        return [&frames](std::uint16_t type, const std::string &payload)
        {
            frames.push_back({type, payload});
        };
    }

    void append_network_length(Buffer &buffer, std::uint32_t frame_length)
    {
        std::uint32_t network_length = htonl(frame_length);
        buffer.append(reinterpret_cast<const char *>(&network_length), sizeof(network_length));
    }

    bool test_encode_layout_and_normal_decode()
    {
        std::string frame;
        CHECK(Codec::encode(2, "hi", frame));

        const unsigned char expected[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x68, 0x69};
        CHECK(frame.size() == sizeof(expected));
        CHECK(std::memcmp(frame.data(), expected, sizeof(expected)) == 0);

        Buffer buffer;
        buffer.append(frame);

        std::vector<ReceivedFrame> frames;
        bool consumed_before_callback = false;

        Codec::FrameCallback callback = [&buffer, &frames, &consumed_before_callback](std::uint16_t type, const std::string &payload)
        {
            consumed_before_callback = buffer.empty();
            frames.push_back({type, payload});
        };

        CHECK(Codec::decode(buffer, callback));
        CHECK(consumed_before_callback);
        CHECK(buffer.empty());
        CHECK(frames.size() == 1);
        CHECK(frames[0].type == 2);
        CHECK(frames[0].payload == "hi");

        return true;
    }

    bool test_empty_and_binary_payload()
    {
        std::string empty_frame;
        std::string binary_frame;
        std::string binary_payload("a\0b\0c", 5);

        CHECK(Codec::encode(0, "", empty_frame));
        CHECK(Codec::encode(65535, binary_payload, binary_frame));
        CHECK(empty_frame.size() == 6);

        Buffer buffer;
        buffer.append(empty_frame);
        buffer.append(binary_frame);

        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        CHECK(Codec::decode(buffer, callback));
        CHECK(buffer.empty());
        CHECK(frames.size() == 2);
        CHECK(frames[0].type == 0);
        CHECK(frames[0].payload.empty());
        CHECK(frames[1].type == 65535);
        CHECK(frames[1].payload == binary_payload);

        return true;
    }

    bool test_half_header()
    {
        std::string frame;
        CHECK(Codec::encode(7, "half-header", frame));

        for (std::size_t split = 1;split < sizeof(std::uint32_t);split++)
        {
            Buffer buffer;
            std::vector<ReceivedFrame> frames;
            Codec::FrameCallback callback = collect_frames(frames);

            buffer.append(frame.data(), split);

            CHECK(Codec::decode(buffer, callback));
            CHECK(frames.empty());
            CHECK(buffer.readable_bytes() == split);

            buffer.append(frame.data() + split, frame.size() - split);

            CHECK(Codec::decode(buffer, callback));
            CHECK(buffer.empty());
            CHECK(frames.size() == 1);
            CHECK(frames[0].type == 7);
            CHECK(frames[0].payload == "half-header");
        }

        return true;
    }

    bool test_half_body()
    {
        std::string frame;
        CHECK(Codec::encode(8, "half-body", frame));

        for (std::size_t split = sizeof(std::uint32_t);split < frame.size();split++)
        {
            Buffer buffer;
            std::vector<ReceivedFrame> frames;
            Codec::FrameCallback callback = collect_frames(frames);

            buffer.append(frame.data(), split);

            CHECK(Codec::decode(buffer, callback));
            CHECK(frames.empty());
            CHECK(buffer.readable_bytes() == split);

            buffer.append(frame.data() + split, frame.size() - split);

            CHECK(Codec::decode(buffer, callback));
            CHECK(buffer.empty());
            CHECK(frames.size() == 1);
            CHECK(frames[0].type == 8);
            CHECK(frames[0].payload == "half-body");
        }

        return true;
    }

    bool test_multiple_frames()
    {
        std::string first;
        std::string second;
        std::string third;

        CHECK(Codec::encode(1, "first", first));
        CHECK(Codec::encode(2, "second", second));
        CHECK(Codec::encode(3, "third", third));

        Buffer buffer;
        buffer.append(first);
        buffer.append(second);
        buffer.append(third);

        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        CHECK(Codec::decode(buffer, callback));
        CHECK(buffer.empty());
        CHECK(frames.size() == 3);
        CHECK(frames[0].type == 1);
        CHECK(frames[0].payload == "first");
        CHECK(frames[1].type == 2);
        CHECK(frames[1].payload == "second");
        CHECK(frames[2].type == 3);
        CHECK(frames[2].payload == "third");

        return true;
    }

    bool test_complete_frame_and_half_tail()
    {
        std::string first;
        std::string second;

        CHECK(Codec::encode(10, "complete", first));
        CHECK(Codec::encode(11, "tail", second));

        constexpr std::size_t second_split = 2;

        Buffer buffer;
        buffer.append(first);
        buffer.append(second.data(), second_split);

        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        CHECK(Codec::decode(buffer, callback));
        CHECK(frames.size() == 1);
        CHECK(frames[0].type == 10);
        CHECK(frames[0].payload == "complete");
        CHECK(buffer.readable_bytes() == second_split);

        buffer.append(second.data() + second_split, second.size() - second_split);

        CHECK(Codec::decode(buffer, callback));
        CHECK(buffer.empty());
        CHECK(frames.size() == 2);
        CHECK(frames[1].type == 11);
        CHECK(frames[1].payload == "tail");

        return true;
    }

    bool test_invalid_lengths_and_empty_callback()
    {
        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        Buffer too_small;
        append_network_length(too_small, 1);
        CHECK(!Codec::decode(too_small, callback));
        CHECK(frames.empty());

        Buffer too_large;
        append_network_length(too_large, 64 * 1024 + 1);
        CHECK(!Codec::decode(too_large, callback));
        CHECK(frames.empty());

        Buffer empty_buffer;
        Codec::FrameCallback empty_callback;
        CHECK(!Codec::decode(empty_buffer, empty_callback));

        return true;
    }

    bool test_encode_boundaries()
    {
        std::string maximum_payload(65534, 'x');
        std::string maximum_frame;

        CHECK(Codec::encode(42, maximum_payload, maximum_frame));
        CHECK(maximum_frame.size() == 65540);

        Buffer buffer;
        buffer.append(maximum_frame);

        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        CHECK(Codec::decode(buffer, callback));
        CHECK(buffer.empty());
        CHECK(frames.size() == 1);
        CHECK(frames[0].type == 42);
        CHECK(frames[0].payload == maximum_payload);

        std::string oversized_payload(65535, 'x');
        std::string failed_frame = "old-data";

        CHECK(!Codec::encode(42, oversized_payload, failed_frame));
        CHECK(failed_frame.empty());

        return true;
    }

    bool test_empty_buffer()
    {
        Buffer buffer;
        std::vector<ReceivedFrame> frames;
        Codec::FrameCallback callback = collect_frames(frames);

        CHECK(Codec::decode(buffer, callback));
        CHECK(buffer.empty());
        CHECK(frames.empty());

        return true;
    }

    bool test_repeated_encode_decode()
    {
        constexpr int repeat_count = 100;

        for (int i = 0;i < repeat_count;i++)
        {
            std::uint16_t type = static_cast<std::uint16_t>(i);
            std::string payload = "payload-" + std::to_string(i);
            std::string frame;

            CHECK(Codec::encode(type, payload, frame));

            Buffer buffer;
            buffer.append(frame);

            std::vector<ReceivedFrame> frames;
            Codec::FrameCallback callback = collect_frames(frames);

            CHECK(Codec::decode(buffer, callback));
            CHECK(buffer.empty());
            CHECK(frames.size() == 1);
            CHECK(frames[0].type == type);
            CHECK(frames[0].payload == payload);
        }

        return true;
    }

}

int main()
{
    const std::vector<std::pair<std::string, std::function<bool()>>> tests =
    {
        {"encode_layout_and_normal_decode", test_encode_layout_and_normal_decode},
        {"empty_and_binary_payload", test_empty_and_binary_payload},
        {"half_header", test_half_header},
        {"half_body", test_half_body},
        {"multiple_frames", test_multiple_frames},
        {"complete_frame_and_half_tail", test_complete_frame_and_half_tail},
        {"invalid_lengths_and_empty_callback", test_invalid_lengths_and_empty_callback},
        {"encode_boundaries", test_encode_boundaries},
        {"empty_buffer", test_empty_buffer},
        {"repeated_encode_decode", test_repeated_encode_decode}
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

    std::cout << "Codec 基础验收通过\n";
    return 0;
}
