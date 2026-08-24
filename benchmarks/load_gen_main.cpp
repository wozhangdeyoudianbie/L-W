#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include "load_gen.h"

using namespace std;

#define endl '\n'

namespace
{
    bool parse_positive_u64(const char *text, uint64_t &value)
    {
        if (!text || *text == '\0')
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const unsigned long long parsed = strtoull(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0' || parsed == 0)
        {
            return false;
        }
        if (parsed > numeric_limits<uint64_t>::max())
        {
            return false;
        }
        value = static_cast<uint64_t>(parsed);
        return true;
    }
}

int main(int argc, char *argv[])
{
    cin.tie(0)->sync_with_stdio(0);
    cout.tie(0);
    if (argc < 2 || argc > 7)
    {
        cerr << "用法: " << argv[0] << " <报告目录> [服务器地址] [端口] [客户端数量] [房间容量] [每秒连接数]" << endl;
        return 1;
    }
    LoadGen::Config config;
    if (argc >= 3)
    {
        config.address = argv[2];
    }
    uint64_t value = 0;
    if (argc >= 4)
    {
        if (!parse_positive_u64(argv[3], value) || value > numeric_limits<uint16_t>::max())
        {
            cerr << "端口必须是 uint16 范围内的正整数" << endl;
            return 1;
        }
        config.port = static_cast<uint16_t>(value);
    }
    if (argc >= 5)
    {
        if (!parse_positive_u64(argv[4], value) || value > numeric_limits<size_t>::max())
        {
            cerr << "客户端数量必须是正整数" << endl;
            return 1;
        }
        config.client_count = static_cast<size_t>(value);
    }
    if (argc >= 6)
    {
        if (!parse_positive_u64(argv[5], value) || value > numeric_limits<size_t>::max())
        {
            cerr << "房间容量必须是正整数" << endl;
            return 1;
        }
        config.room_capacity = static_cast<size_t>(value);
    }
    if (argc >= 7)
    {
        if (!parse_positive_u64(argv[6], value) || value > numeric_limits<size_t>::max())
        {
            cerr << "每秒连接数必须是正整数" << endl;
            return 1;
        }
        config.connections_per_second = static_cast<size_t>(value);
    }
    LoadGen load_gen(config);
    if (!load_gen.valid())
    {
        cerr << "压测器配置无效" << endl;
        return 1;
    }
    const bool run_success = load_gen.run();

    if (!run_success)
    {
        cerr << "压测器运行失败" << endl;
    }

    if (!load_gen.write_report(argv[1]))
    {
        cerr << "压测报告写入失败" << endl;
        return 1;
    }

    if (!run_success)
    {
        return 1;
    }
    const LoadGen::Result &result = load_gen.result();
    cout << boolalpha;
    cout << "workload_completed = " << result.workload_completed << endl;
    cout << "correctness_passed = " << result.correctness_passed << endl;
    return result.correctness_passed ? 0 : 1;
}
