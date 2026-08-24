#include "load_gen.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    bool parse_positive_u64(const char *text, std::uint64_t &value)
    {
        if (!text || *text == '\0')
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0' || parsed == 0)
        {
            return false;
        }
        if (parsed > std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 7)
    {
        std::cerr
            << "usage: " << argv[0]
            << " <report-directory> [address] [port] [client-count] [room-capacity] [connections-per-second]\n";
        return 1;
    }

    LoadGen::Config config;
    if (argc >= 3)
    {
        config.address = argv[2];
    }

    std::uint64_t value = 0;
    if (argc >= 4)
    {
        if (!parse_positive_u64(argv[3], value) ||
            value > std::numeric_limits<std::uint16_t>::max())
        {
            std::cerr << "port must be a positive uint16 integer\n";
            return 1;
        }
        config.port = static_cast<std::uint16_t>(value);
    }
    if (argc >= 5)
    {
        if (!parse_positive_u64(argv[4], value) ||
            value > std::numeric_limits<std::size_t>::max())
        {
            std::cerr << "client-count must be a positive integer\n";
            return 1;
        }
        config.client_count = static_cast<std::size_t>(value);
    }
    if (argc >= 6)
    {
        if (!parse_positive_u64(argv[5], value) ||
            value > std::numeric_limits<std::size_t>::max())
        {
            std::cerr << "room-capacity must be a positive integer\n";
            return 1;
        }
        config.room_capacity = static_cast<std::size_t>(value);
    }
    if (argc >= 7)
    {
        if (!parse_positive_u64(argv[6], value) ||
            value > std::numeric_limits<std::size_t>::max())
        {
            std::cerr << "connections-per-second must be a positive integer\n";
            return 1;
        }
        config.connections_per_second = static_cast<std::size_t>(value);
    }

    LoadGen load_gen(config);
    if (!load_gen.valid())
    {
        std::cerr << "invalid load generator configuration\n";
        return 1;
    }
    if (!load_gen.run())
    {
        std::cerr << "load generator failed\n";
        return 1;
    }
    if (!load_gen.write_report(argv[1]))
    {
        std::cerr << "cannot write load generator report\n";
        return 1;
    }

    const LoadGen::Result &result = load_gen.result();
    std::cout << std::boolalpha;
    std::cout << "workload_completed = " << result.workload_completed << '\n';
    std::cout << "correctness_passed = " << result.correctness_passed << '\n';
    return result.correctness_passed ? 0 : 1;
}
