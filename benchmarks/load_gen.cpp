#include "load_gen.h"
#include "codec.h"
#include "protocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <sys/socket.h>

#define endl '\n'

namespace
{
    bool read_u16(const std::string &data, std::size_t &offset, std::uint16_t &value)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint16_t))
        {
            return false;
        }
        std::uint16_t network_value = 0;
        std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
        value = ntohs(network_value);
        offset += sizeof(network_value);
        return true;
    }

    bool read_u32(const std::string &data, std::size_t &offset, std::uint32_t &value)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint32_t))
        {
            return false;
        }
        std::uint32_t network_value = 0;
        std::memcpy(&network_value, data.data() + offset, sizeof(network_value));
        value = ntohl(network_value);
        offset += sizeof(network_value);
        return true;
    }

    bool read_u64(const std::string &data, std::size_t &offset, std::uint64_t &value)
    {
        std::uint32_t high = 0;
        std::uint32_t low = 0;
        if (!read_u32(data, offset, high) || !read_u32(data, offset, low))
        {
            return false;
        }
        value = (static_cast<std::uint64_t>(high) << 32) | low;
        return true;
    }

    void append_u16(std::string &data, std::uint16_t value)
    {
        const std::uint16_t network_value = htons(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    void append_u32(std::string &data, std::uint32_t value)
    {
        const std::uint32_t network_value = htonl(value);
        data.append(reinterpret_cast<const char *>(&network_value), sizeof(network_value));
    }

    void append_u64(std::string &data, std::uint64_t value)
    {
        append_u32(data, static_cast<std::uint32_t>(value >> 32));
        append_u32(data, static_cast<std::uint32_t>(value & 0xffffffffULL));
    }

    void append_i32(std::string &data, std::int32_t value)
    {
        append_u32(data, static_cast<std::uint32_t>(value));
    }
}

// 构造：保存配置并初始化资源句柄
LoadGen::LoadGen(Config config)
    :config_(std::move(config)), valid_(false), epoll_fd_(-1), state_(States::created), next_client_index_(0)
{
    valid_ = validate_config();
}

// 析构：清理资源
LoadGen::~LoadGen()
{
    cleanup();
}

// 查询：配置是否有效
bool LoadGen::valid() const
{
    return valid_;
}

// 执行压测全流程：校验配置→初始化→主循环→汇总结果
bool LoadGen::run()
{
    if (!valid_ || state_ != States::created)
    {
        return false;
    }
    if (!initialize())
    {
        record_failure("initialize_failed");
        state_ = States::failed;
        cleanup();
        return false;
    }
    auto time_now = Clock::now();
    transition_to(States::ramp_up, time_now);
    if (!run_loop())
    {
        state_ = States::failed;
    }
    finalize_result();
    cleanup();
    return state_ == States::finished;
}

// 查询：当前阶段状态
LoadGen::States LoadGen::states() const
{
    return state_;
}

// 查询：压测统计结果（仅结束后有意义）
const LoadGen::Result &LoadGen::result() const
{
    return result_;
}

// 转换：压测阶段转报告文字
const char *LoadGen::state_name(States state)
{
    switch (state)
    {
        case States::created:
            return "created";
        case States::ramp_up:
            return "ramp_up";
        case States::warm_up:
            return "warm_up";
        case States::measure:
            return "measure";
        case States::drain:
            return "drain";
        case States::finished:
            return "finished";
        case States::failed:
            return "failed";
    }
    return "unknown";
}

// 转换：失败分类转报告文字
const char *LoadGen::failure_name(Failurestates failure)
{
    switch (failure)
    {
        case Failurestates::connect_error:
            return "connect_error";
        case Failurestates::connect_timeout:
            return "connect_timeout";
        case Failurestates::join_error:
            return "join_error";
        case Failurestates::join_timeout:
            return "join_timeout";
        case Failurestates::socket_error:
            return "socket_error";
        case Failurestates::protocol_error:
            return "protocol_error";
        case Failurestates::unexpected_close:
            return "unexpected_close";
    }
    return "unknown";
}

// 转换：系统错误转报告原因
std::string LoadGen::make_system_error_reason(const char *operation, int error_number)
{
    const std::string operation_name = operation ? operation : "system_call";
    return operation_name + ": errno = " + std::to_string(error_number) + " " + std::strerror(error_number);
}

// 写报告：把统计结果输出到指定目录（目录不存在则创建）
bool LoadGen::write_report(const std::string &directory) const
{
    if (state_ != States::finished && state_ != States::failed)
    {
        return false;
    }
    if (directory.empty())
    {
        return false;
    }
    std::filesystem::path directory_path = std::filesystem::path(directory);
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory_path, filesystem_error);
    if (filesystem_error)
    {
        return false;
    }
    const std::filesystem::path summary_path = directory_path / "summary.txt";
    const std::filesystem::path heartbeat_rtt_path = directory_path / "heartbeat_rtt_us.csv";
    const std::filesystem::path snapshot_interval_path = directory_path / "snapshot_interval_us.csv";
    const std::filesystem::path scheduler_lag_path = directory_path / "scheduler_lag_us.csv";
    if (std::filesystem::exists(summary_path, filesystem_error) || filesystem_error ||
        std::filesystem::exists(heartbeat_rtt_path, filesystem_error) || filesystem_error ||
        std::filesystem::exists(snapshot_interval_path, filesystem_error) || filesystem_error ||
        std::filesystem::exists(scheduler_lag_path, filesystem_error) || filesystem_error)
    {
        return false;
    }
    std::ofstream report(summary_path, std::ios::out);
    std::ofstream heartbeat_rtt_report(heartbeat_rtt_path, std::ios::out);
    std::ofstream snapshot_interval_report(snapshot_interval_path, std::ios::out);
    std::ofstream scheduler_lag_report(scheduler_lag_path, std::ios::out);
    if (!report.is_open() || !heartbeat_rtt_report.is_open() || !snapshot_interval_report.is_open() || !scheduler_lag_report.is_open())
    {
        return false;
    }
    report << std::boolalpha << std::fixed << std::setprecision(3);
    report << "state = " << state_name(state_) << endl;
    report << "config.address = " << config_.address << endl;
    report << "config.port = " << config_.port << endl;
    report << "config.client_count = " << config_.client_count << endl;
    report << "config.room_capacity = " << config_.room_capacity << endl;
    report << "config.connections_per_second = " << config_.connections_per_second << endl;
    report << "config.connect_timeout_ms = " << config_.connect_timeout_ms << endl;
    report << "config.join_timeout_ms = " << config_.join_timeout_ms << endl;
    report << "config.move_interval_ms = " << config_.move_interval_ms << endl;
    report << "config.heartbeat_interval_ms = " << config_.heartbeat_interval_ms << endl;
    report << "config.warm_up_ms = " << config_.warm_up_ms << endl;
    report << "config.measure_ms = " << config_.measure_ms << endl;
    report << "config.drain_ms = " << config_.drain_ms << endl;
    report << "result.first_failure.recorded = " << result_.first_failure.recorded << endl;
    if (result_.first_failure.recorded)
    {
        report << "result.first_failure.state = " << state_name(result_.first_failure.state) << endl;
        report << "result.first_failure.reason = " << result_.first_failure.reason << endl;
        report << "result.first_failure.has_client = " << result_.first_failure.has_client << endl;
        if (result_.first_failure.has_client)
        {
            report << "result.first_failure.client_index = " << result_.first_failure.client_index << endl;
        }
        report << "result.first_failure.active_clients = " << result_.first_failure.active_clients << endl;
    }
    report << "result.connection_attempts = " << result_.connection_attempts << endl;
    report << "result.connection_successes = " << result_.connection_successes << endl;
    report << "result.connection_failures = " << result_.connection_failures << endl;
    report << "result.join_successes = " << result_.join_successes << endl;
    report << "result.join_failures = " << result_.join_failures << endl;
    report << "result.planned_move_frames = " << result_.planned_move_frames << endl;
    report << "result.sent_move_frames = " << result_.sent_move_frames << endl;
    report << "result.missed_move_deadlines = " << result_.missed_move_deadlines << endl;
    report << "result.heartbeat_frames = " << result_.heartbeat_frames << endl;
    report << "result.heartbeat_acks = " << result_.heartbeat_acks << endl;
    report << "result.snapshot_frames = " << result_.snapshot_frames << endl;
    report << "result.tick_gaps = " << result_.tick_gaps << endl;
    report << "result.error_frames = " << result_.error_frames << endl;
    report << "result.protocol_errors = " << result_.protocol_errors << endl;
    report << "result.unexpected_closes = " << result_.unexpected_closes << endl;
    report << "result.actual_move_frames_per_second = " << result_.actual_move_frames_per_second << endl;
    report << "result.snapshot_frames_per_second = " << result_.snapshot_frames_per_second << endl;
    report << "result.heartbeat_rtt.count = " << result_.heartbeat_rtt.count << endl;
    report << "result.heartbeat_rtt.p50_ms = " << result_.heartbeat_rtt.p50_ms << endl;
    report << "result.heartbeat_rtt.p95_ms = " << result_.heartbeat_rtt.p95_ms << endl;
    report << "result.heartbeat_rtt.p99_ms = " << result_.heartbeat_rtt.p99_ms << endl;
    report << "result.heartbeat_rtt.maximum_ms = " << result_.heartbeat_rtt.maximum_ms << endl;
    report << "result.snapshot_interval.count = " << result_.snapshot_interval.count << endl;
    report << "result.snapshot_interval.p50_ms = " << result_.snapshot_interval.p50_ms << endl;
    report << "result.snapshot_interval.p95_ms = " << result_.snapshot_interval.p95_ms << endl;
    report << "result.snapshot_interval.p99_ms = " << result_.snapshot_interval.p99_ms << endl;
    report << "result.snapshot_interval.maximum_ms = " << result_.snapshot_interval.maximum_ms << endl;
    report << "result.scheduler_lag.count = " << result_.scheduler_lag.count << endl;
    report << "result.scheduler_lag.p50_ms = " << result_.scheduler_lag.p50_ms << endl;
    report << "result.scheduler_lag.p95_ms = " << result_.scheduler_lag.p95_ms << endl;
    report << "result.scheduler_lag.p99_ms = " << result_.scheduler_lag.p99_ms << endl;
    report << "result.scheduler_lag.maximum_ms = " << result_.scheduler_lag.maximum_ms << endl;
    report << "result.workload_completed = " << result_.workload_completed << endl;
    report << "result.correctness_passed = " << result_.correctness_passed << endl;
    for (const std::uint64_t sample : heartbeat_rtt_us_)
    {
        heartbeat_rtt_report << sample << endl;
    }
    for (const std::uint64_t sample : snapshot_interval_us_)
    {
        snapshot_interval_report << sample << endl;
    }
    for (const std::uint64_t sample : scheduler_lag_us_)
    {
        scheduler_lag_report << sample << endl;
    }
    report.flush();
    heartbeat_rtt_report.flush();
    snapshot_interval_report.flush();
    scheduler_lag_report.flush();
    return report.good() && heartbeat_rtt_report.good() && snapshot_interval_report.good() && scheduler_lag_report.good();
}

// 校验：检查配置参数合法性
bool LoadGen::validate_config() const
{
    if (config_.address.empty() || config_.port == 0)
    {
        return false;
    }
    in_addr temp{};
    if (inet_pton(AF_INET, config_.address.c_str(), &temp) != 1)
    {
        return false;
    }
    if (config_.client_count <= 0 || config_.room_capacity <= 0 || config_.connections_per_second <= 0)
    {
        return false;
    }
    if (config_.move_interval_ms < 50 || config_.room_capacity > std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    const std::size_t room_count = config_.client_count / config_.room_capacity;
    if (room_count > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    if (config_.client_count % config_.room_capacity != 0)
    {
        return false;
    }
    if (config_.connect_timeout_ms <= 0 || config_.join_timeout_ms <= 0 || config_.move_interval_ms <= 0 || config_.heartbeat_interval_ms <= 0 || config_.warm_up_ms <= 0 || config_.measure_ms <= 0 || config_.drain_ms <= 0)
    {
        return false;
    }
    if (config_.measure_ms < config_.move_interval_ms || config_.measure_ms < config_.heartbeat_interval_ms)
    {
        return false;
    }
    return true;
}

// 初始化：创建 epoll 实例（失败返回 false）
bool LoadGen::initialize()
{
    if (epoll_fd_ != -1)
    {
        record_failure("initialize_epoll_already_exists");
        return false;
    }
    if (!clients_.empty())
    {
        record_failure("initialize_clients_not_empty");
        return false;
    }

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1)
    {
        const int epoll_error = errno;
        record_failure(
            make_system_error_reason("epoll_create1", epoll_error));
        return false;
    }
    return true;
}

// 清理：关闭所有客户端连接并销毁 epoll
void LoadGen::cleanup()
{
    for (auto &[fd, value] : clients_)
    {
        if (fd != -1)
        {
            close(fd);
        }
    }
    clients_.clear();
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
    }
    epoll_fd_ = -1;
}

// 主循环：轮询事件并驱动客户端，直到进入 finished/failed
bool LoadGen::run_loop()
{
    while (state_ != States::finished && state_ != States::failed)
    {
        auto now = Clock::now();
        switch (state_)
        {
            case States::ramp_up:
                {
                    if (!start_due_clients(now))
                    {
                        record_failure("start_due_clients_failed");
                        transition_to(States::failed, now);
                        break;
                    }
                    if (all_clients_active())
                    {
                        transition_to(States::warm_up, now);
                    }
                    break;
                }
            case States::warm_up:
                {
                    if (now >= phase_deadline_)
                    {
                        transition_to(States::measure, now);
                    }
                    break;
                }
            case States::measure:
                {
                    if (now >= phase_deadline_)
                    {
                        transition_to(States::drain, now);
                    }
                    break;
                }
            case States::drain:
                {
                    if (now >= phase_deadline_)
                    {
                        transition_to(States::finished, now);
                    }
                    break;
                }
            case States::created:
                {
                    record_failure("run_loop_entered_created_state");
                    transition_to(States::failed, now);
                    break;
                }
            case States::finished:
                {
                    break;
                }
            case States::failed:
                {
                    break;
                }
        }
        if (state_ == States::finished || state_ == States::failed)
        {
            break;
        }
        check_client_deadlines(now);
        if (state_ == States::failed)
        {
            break;
        }
        if (state_ == States::ramp_up || state_ == States::warm_up || state_ == States::measure)
        {
            drive_active_clients(now);
        }
        if (state_ == States::failed)
        {
            break;
        }
        now = Clock::now();
        int timeout = calculate_wait_timeout_ms(now);
        if (!poll_once(timeout))
        {
            record_failure("poll_once_failed");
            transition_to(States::failed, now);
        }
    }
    return state_ == States::finished;
}

// 阶段切换：更新当前状态并设置该阶段的截止时间
void LoadGen::transition_to(States state, Clock::time_point now)
{
    state_ = state;
    switch (state_)
    {
        case States::created:
            {
                break;
            }
        case States::ramp_up:
            {
                next_connect_time_ = now;
                break;
            }
        case States::warm_up:
            {
                const auto move_interval = std::chrono::milliseconds(config_.move_interval_ms);
                for (auto &entry : clients_)
                {
                    Client &client = entry.second;
                    if (client.state != Clientstates::active)
                    {
                        continue;
                    }
                    while (client.next_move_time <= now)
                    {
                        client.next_move_time += move_interval;
                    }
                }
                phase_deadline_ = now + std::chrono::milliseconds(config_.warm_up_ms);
                break;
            }
        case States::measure:
            {
                measure_started_time_ = now;
                phase_deadline_ = now + std::chrono::milliseconds(config_.measure_ms);
                break;
            }
        case States::drain:
            {
                measure_finished_time_ = now;
                phase_deadline_ = now + std::chrono::milliseconds(config_.drain_ms);
                break;
            }
        case States::finished:
            {
                break;
            }
        case States::failed:
            {
                break;
            }
    }
}

// 启动到期客户端：按 connections_per_second 速率创建连接
bool LoadGen::start_due_clients(Clock::time_point now)
{
    const auto connect = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / static_cast<double>(config_.connections_per_second)));
    if (connect <= Clock::duration::zero())
    {
        record_failure("connection_interval_not_positive");
        return false;
    }
    while (next_client_index_ < config_.client_count && now >= next_connect_time_)
    {
        if (!create_client(next_client_index_, now))
        {
            return false;
        }
        ++next_client_index_;
        next_connect_time_ += connect;
    }
    return true;
}

// 创建客户端：非阻塞 connect 并登记到 clients_
bool LoadGen::create_client(std::size_t index, Clock::time_point now)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1)
    {
        const int socket_error = errno;
        record_failure(index, make_system_error_reason("socket", socket_error));
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.address.c_str(), &address.sin_addr) != 1)
    {
        record_failure(index, "inet_pton_failed");
        close(fd);
        return false;
    }
    Client client;
    client.index = index;
    client.fd = fd;
    client.state = Clientstates::connecting;
    client.room_id = static_cast<std::uint32_t>(index / config_.room_capacity + 1);
    client.state_deadline = now + std::chrono::milliseconds(config_.connect_timeout_ms);
    ++result_.connection_attempts;
    const int connect_result = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (connect_result == -1 && errno != EINPROGRESS)
    {
        const int connect_error = errno;
        ++result_.connection_failures;
        record_failure(
            index,
            make_system_error_reason("connect", connect_error));
        close(fd);
        return false;
    }
    auto insert_result = clients_.emplace(fd, std::move(client));
    if (!insert_result.second)
    {
        ++result_.connection_failures;
        record_failure(index, "clients_emplace_duplicate_fd");
        close(fd);
        return false;
    }
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        const int epoll_error = errno;
        record_failure(
            index,
            make_system_error_reason("epoll_ctl_add", epoll_error));
        clients_.erase(fd);
        ++result_.connection_failures;
        close(fd);
        return false;
    }
    return true;
}

// 驱动活跃客户端：到点发送心跳与移动命令
void LoadGen::drive_active_clients(Clock::time_point now)
{
    const auto heartbeat_interval = std::chrono::milliseconds(config_.heartbeat_interval_ms);
    const auto move_interval = std::chrono::milliseconds(config_.move_interval_ms);
    for (auto &[fd, client] : clients_)
    {
        if (client.state != Clientstates::active)
        {
            continue;
        }
        if (now >= client.next_heartbeat_time)
        {
            if (!queue_heartbeat(client, now))
            {
                fail_client(fd, Failurestates::protocol_error);
                return;
            }
            do
            {
                client.next_heartbeat_time += heartbeat_interval;
            } while (client.next_heartbeat_time <= now);
        }
        if ((state_ == States::warm_up || state_ == States::measure) && now >= client.next_move_time)
        {
            const auto expected_time = client.next_move_time;
            std::uint64_t due_move_count = 0;
            do
            {
                ++due_move_count;
                client.next_move_time += move_interval;
            } while (client.next_move_time <= now);
            if (state_ == States::measure)
            {
                result_.planned_move_frames += due_move_count;
                if (due_move_count > 1)
                {
                    result_.missed_move_deadlines += due_move_count - 1;
                }
                record_scheduler_lag(expected_time, now);
            }
            if (!queue_move(client))
            {
                fail_client(fd, Failurestates::protocol_error);
                return;
            }
        }
        if (!client.write_buffer.empty() && !write_to_client(client))
        {
            fail_client(fd, Failurestates::socket_error);
            return;
        }
    }
}

// 检查超时：处理连接/加入超时与心跳超时
void LoadGen::check_client_deadlines(Clock::time_point now)
{
    const auto heartbeat_timeout = std::chrono::milliseconds(config_.heartbeat_interval_ms);
    for (auto &[fd, client] : clients_)
    {
        if (client.state == Clientstates::connecting && now >= client.state_deadline)
        {
            fail_client(fd, Failurestates::connect_timeout);
            return;
        }
        if (client.state == Clientstates::joining && now >= client.state_deadline)
        {
            fail_client(fd, Failurestates::join_timeout);
            return;
        }
        if (client.state != Clientstates::active)
        {
            continue;
        }
        for (const auto &heartbeat : client.pending_heartbeats)
        {
            if (now >= heartbeat.second + heartbeat_timeout)
            {
                fail_client(fd, Failurestates::socket_error);
                return;
            }
        }
    }
}

// 查询：是否所有客户端都已进入 active
bool LoadGen::all_clients_active() const
{
    if (next_client_index_ != config_.client_count || clients_.size() != config_.client_count)
    {
        return false;
    }
    for (const auto &entry : clients_)
    {
        if (entry.second.state != Clientstates::active)
        {
            return false;
        }
    }
    return true;
}

// 轮询：等待一次 epoll 事件并分发
bool LoadGen::poll_once(int timeout_ms)
{
    constexpr int MAX_EVENTS = 256;
    epoll_event events[MAX_EVENTS];
    const int event_count = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
    if (event_count == -1)
    {
        if (errno == EINTR)
        {
            return true;
        }

        const int epoll_error = errno;
        record_failure(
            make_system_error_reason("epoll_wait", epoll_error));
        return false;
    }
    for (int i = 0;i < event_count;i++)
    {
        const int fd = events[i].data.fd;
        auto it = clients_.find(fd);
        if (it == clients_.end())
        {
            continue;
        }
        if (!update_client_events(it->second, events[i].events))
        {
            record_failure("update_client_events_failed");
            return false;
        }
    }
    return true;
}

// 计算：下一次 epoll 等待的毫秒数
int LoadGen::calculate_wait_timeout_ms(Clock::time_point now) const
{
    Clock::time_point next_time = Clock::time_point::max();
    auto use_earlier_time = [&next_time](Clock::time_point value)
    {
        if (value < next_time)
        {
            next_time = value;
        }
    };
    if (state_ == States::ramp_up && next_client_index_ < config_.client_count)
    {
        use_earlier_time(next_connect_time_);
    }
    if (state_ == States::warm_up || state_ == States::measure || state_ == States::drain)
    {
        use_earlier_time(phase_deadline_);
    }
    const auto heartbeat_timeout = std::chrono::milliseconds(config_.heartbeat_interval_ms);
    for (const auto &entry : clients_)
    {
        const Client &client = entry.second;
        if (client.state == Clientstates::connecting || client.state == Clientstates::joining)
        {
            use_earlier_time(client.state_deadline);
            continue;
        }
        if (client.state != Clientstates::active)
        {
            continue;
        }
        if (state_ == States::ramp_up || state_ == States::warm_up || state_ == States::measure)
        {
            use_earlier_time(client.next_heartbeat_time);
        }
        if (state_ == States::warm_up || state_ == States::measure)
        {
            use_earlier_time(client.next_move_time);
        }
        for (const auto &heartbeat : client.pending_heartbeats)
        {
            use_earlier_time(heartbeat.second + heartbeat_timeout);
        }
    }
    if (next_time == Clock::time_point::max())
    {
        return 1000;
    }
    if (next_time <= now)
    {
        return 0;
    }
    const auto timeout = std::chrono::ceil<std::chrono::milliseconds>(next_time - now).count();
    if (timeout > std::numeric_limits<int>::max())
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(timeout);
}

// 事件处理：按可读/可写状态更新单个客户端
bool LoadGen::update_client_events(const Client &client, std::uint32_t events)
{
    int fd = client.fd;
    auto it = clients_.find(fd);
    if (it == clients_.end())
    {
        return true;
    }
    const auto now = Clock::now();
    if (it->second.state == Clientstates::connecting)
    {
        const std::uint32_t connect_events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        if ((events & connect_events) == 0)
        {
            return true;
        }
        if (!finish_connect(it->second, now))
        {
            const Failurestates failure = it->second.state == Clientstates::connecting ? Failurestates::connect_error : Failurestates::protocol_error;
            fail_client(fd, failure);
            return false;
        }
        it = clients_.find(fd);
        if (it == clients_.end())
        {
            return false;
        }
        if (!it->second.write_buffer.empty() && !write_to_client(it->second))
        {
            fail_client(fd, Failurestates::socket_error);
            return false;
        }
    }
    it = clients_.find(fd);
    if (it == clients_.end())
    {
        return false;
    }
    if ((events & (EPOLLIN | EPOLLRDHUP)) != 0)
    {
        const Clientstates state_before_read = it->second.state;
        if (!read_from_client(it->second, now))
        {
            it = clients_.find(fd);
            if (it == clients_.end())
            {
                return false;
            }
            if (it->second.state == Clientstates::failed)
            {
                const Failurestates failure =
                    state_before_read == Clientstates::joining
                    ? Failurestates::join_error
                    : Failurestates::protocol_error;

                record_failure(
                    it->second.index,
                    failure_name(failure));

                close_client(fd, Clientstates::failed);
                transition_to(States::failed, now);
                return false;
            }
            if (it->second.state == Clientstates::closing)
            {
                const Failurestates failure = state_before_read == Clientstates::joining && it->second.player_id == 0 ? Failurestates::join_error : Failurestates::unexpected_close;
                fail_client(fd, failure);
                return false;
            }
            fail_client(fd, Failurestates::socket_error);
            return false;
        }
    }
    it = clients_.find(fd);
    if (it == clients_.end())
    {
        return false;
    }
    if ((events & EPOLLOUT) != 0 && !write_to_client(it->second))
    {
        fail_client(fd, Failurestates::socket_error);
        return false;
    }
    if ((events & EPOLLERR) != 0)
    {
        fail_client(fd, Failurestates::socket_error);
        return false;
    }
    if ((events & EPOLLHUP) != 0)
    {
        const Failurestates failure = it->second.state == Clientstates::joining ? Failurestates::join_error : Failurestates::unexpected_close;
        fail_client(fd, failure);
        return false;
    }
    return true;
}

// 完成连接：非阻塞 connect 的可写事件后续处理
bool LoadGen::finish_connect(Client &client, Clock::time_point now)
{
    if (client.state != Clientstates::connecting)
    {
        return false;
    }
    int socket_error = 0;
    socklen_t error_size = sizeof(socket_error);
    if (getsockopt(client.fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) == -1 || socket_error != 0)
    {
        return false;
    }
    ++result_.connection_successes;
    client.state = Clientstates::joining;
    client.state_deadline = now + std::chrono::milliseconds(config_.join_timeout_ms);
    return queue_join(client);
}

// 读取：从 socket 读数据进读缓冲并拆帧处理
bool LoadGen::read_from_client(Client &client, Clock::time_point now)
{
    char data[4096];
    bool peer_closed = false;
    while (1)
    {
        const ssize_t read_size = recv(client.fd, data, sizeof(data), 0);
        if (read_size > 0)
        {
            client.read_buffer.append(data, static_cast<std::size_t>(read_size));
            continue;
        }
        if (read_size == 0)
        {
            peer_closed = true;
            break;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        return false;
    }
    const bool decode_success = Codec::decode(client.read_buffer, [this, &client, now](std::uint16_t type, const std::string &payload)
    {
        return process_frame(client, type, payload, now);
    });
    if (!decode_success)
    {
        if (client.state != Clientstates::failed)
        {
            ++result_.protocol_errors;
            client.state = Clientstates::failed;
        }
        return false;
    }
    if (peer_closed)
    {
        if (!client.read_buffer.empty())
        {
            ++result_.protocol_errors;
            client.state = Clientstates::failed;
            return false;
        }
        client.state = Clientstates::closing;
        return false;
    }
    return true;
}

// 写入：把写缓冲中的数据发往 socket
bool LoadGen::write_to_client(Client &client)
{
    while (!client.write_buffer.empty())
    {
        const ssize_t write_size = send(client.fd, client.write_buffer.peek(), client.write_buffer.readable_bytes(), MSG_NOSIGNAL);
        if (write_size > 0)
        {
            client.write_buffer.retrieve(static_cast<std::size_t>(write_size));
            continue;
        }
        if (write_size == 0)
        {
            return false;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return true;
        }
        return false;
    }
    return true;
}

// 处理帧：解析服务器响应（join_ok/快照/心跳 ack 等）
bool LoadGen::process_frame(Client &client, std::uint16_t type, const std::string &payload, Clock::time_point now)
{
    auto protocol_failure = [this, &client]()
    {
        ++result_.protocol_errors;
        client.state = Clientstates::failed;
        return false;
    };
    const MessageType message_type = static_cast<MessageType>(type);
    switch (message_type)
    {
        case MessageType::join_ok:
            {
                if (client.state != Clientstates::joining)
                {
                    return protocol_failure();
                }
                std::size_t offset = 0;
                std::uint32_t room_id = 0;
                std::uint64_t player_id = 0;
                std::uint16_t token_size = 0;
                if (!read_u32(payload, offset, room_id) || !read_u64(payload, offset, player_id) || !read_u16(payload, offset, token_size))
                {
                    return protocol_failure();
                }
                if (token_size == 0 || token_size > Protocol::MAX_TOKEN_SIZE || offset > payload.size() || payload.size() - offset < token_size)
                {
                    return protocol_failure();
                }
                offset += token_size;
                std::uint16_t member_count = 0;
                if (!read_u16(payload, offset, member_count))
                {
                    return protocol_failure();
                }
                for (std::uint16_t i = 0;i < member_count;i++)
                {
                    std::uint64_t member_id = 0;
                    std::uint16_t name_size = 0;
                    if (!read_u64(payload, offset, member_id) || !read_u16(payload, offset, name_size))
                    {
                        return protocol_failure();
                    }
                    if (member_id == 0 || name_size == 0 || name_size > Protocol::MAX_PLAYER_NAME_SIZE || offset > payload.size() || payload.size() - offset < name_size)
                    {
                        return protocol_failure();
                    }
                    offset += name_size;
                }
                if (offset != payload.size() || room_id != client.room_id || player_id == 0)
                {
                    return protocol_failure();
                }
                client.player_id = player_id;
                client.state = Clientstates::active;
                client.state_deadline = Clock::time_point{};
                client.next_heartbeat_time = now + std::chrono::milliseconds(config_.heartbeat_interval_ms);
                client.next_move_time = now + std::chrono::milliseconds(config_.move_interval_ms);
                ++result_.join_successes;
                return true;
            }
        case MessageType::player_joined:
            {
                if (client.state != Clientstates::active)
                {
                    return protocol_failure();
                }
                std::size_t offset = 0;
                std::uint32_t room_id = 0;
                std::uint64_t player_id = 0;
                std::uint16_t name_size = 0;
                if (!read_u32(payload, offset, room_id) || !read_u64(payload, offset, player_id) || !read_u16(payload, offset, name_size))
                {
                    return protocol_failure();
                }
                if (room_id != client.room_id || player_id == 0 || name_size == 0 || name_size > Protocol::MAX_PLAYER_NAME_SIZE || offset > payload.size() || payload.size() - offset != name_size)
                {
                    return protocol_failure();
                }
                return true;
            }
        case MessageType::state_snapshot:
            {
                if (client.state != Clientstates::active)
                {
                    return protocol_failure();
                }
                std::size_t offset = 0;
                std::uint32_t room_id = 0;
                std::uint64_t tick_id = 0;
                std::uint16_t state_count = 0;
                if (!read_u32(payload, offset, room_id) || !read_u64(payload, offset, tick_id) || !read_u16(payload, offset, state_count))
                {
                    return protocol_failure();
                }
                constexpr std::size_t STATE_SIZE = sizeof(std::uint64_t) + sizeof(std::uint32_t) * 3;
                if (room_id != client.room_id || tick_id == 0 || state_count != config_.room_capacity || offset > payload.size() || payload.size() - offset != static_cast<std::size_t>(state_count) * STATE_SIZE)
                {
                    return protocol_failure();
                }
                bool contains_self = false;
                std::vector<std::uint64_t> player_ids;
                player_ids.reserve(state_count);
                for (std::uint16_t i = 0;i < state_count;i++)
                {
                    std::uint64_t player_id = 0;
                    std::uint32_t x = 0;
                    std::uint32_t y = 0;
                    std::uint32_t hp = 0;
                    if (!read_u64(payload, offset, player_id) || !read_u32(payload, offset, x) || !read_u32(payload, offset, y) || !read_u32(payload, offset, hp))
                    {
                        return protocol_failure();
                    }
                    if (player_id == 0 || std::find(player_ids.begin(), player_ids.end(), player_id) != player_ids.end())
                    {
                        return protocol_failure();
                    }
                    player_ids.push_back(player_id);
                    if (player_id == client.player_id)
                    {
                        contains_self = true;
                    }
                }
                if (!contains_self || offset != payload.size())
                {
                    return protocol_failure();
                }
                record_snapshot(client, tick_id, now);
                return client.state != Clientstates::failed;
            }
        case MessageType::heartbeat_ack:
            {
                if (client.state != Clientstates::active)
                {
                    return protocol_failure();
                }
                std::size_t offset = 0;
                std::uint64_t seq = 0;
                if (!read_u64(payload, offset, seq) || offset != payload.size())
                {
                    return protocol_failure();
                }
                record_heartbeat_ack(client, seq, now);
                return client.state != Clientstates::failed;
            }
        case MessageType::error:
            {
                std::size_t offset = 0;
                std::uint16_t request_type = 0;
                std::uint16_t error_code = 0;
                if (!read_u16(payload, offset, request_type) || !read_u16(payload, offset, error_code) || offset != payload.size())
                {
                    return protocol_failure();
                }
                const MessageType failed_request = static_cast<MessageType>(request_type);
                if (failed_request != MessageType::join && failed_request != MessageType::leave &&
                    failed_request != MessageType::chat && failed_request != MessageType::move &&
                    failed_request != MessageType::attack && failed_request != MessageType::resume)
                {
                    return protocol_failure();
                }
                if (error_code <
                        static_cast<std::uint16_t>(
                            ErrorCode::room_not_found) ||
                    error_code >
                        static_cast<std::uint16_t>(
                            ErrorCode::resume_failed))
                {
                    return protocol_failure();
                }

                const std::string reason =
                    "server_error: request_type=" +
                    std::to_string(request_type) +
                    " error_code=" +
                    std::to_string(error_code);

                record_failure(client.index, reason);

                ++result_.error_frames;

                if (client.state == Clientstates::joining)
                {
                    ++result_.join_failures;
                }

                client.state = Clientstates::failed;
                return false;
            }
        default:
            {
                return protocol_failure();
            }
    }
}

// 排队：构造并排队 join 帧
bool LoadGen::queue_join(Client &client)
{
    const std::string player_name = "load-client-" + std::to_string(client.index + 1);
    if (client.room_id == 0 || player_name.empty() || player_name.size() > Protocol::MAX_PLAYER_NAME_SIZE)
    {
        return false;
    }
    std::string payload;
    append_u32(payload, client.room_id);
    append_u16(payload, static_cast<std::uint16_t>(player_name.size()));
    payload.append(player_name);
    return queue_frame(client, static_cast<std::uint16_t>(MessageType::join), payload);
}

// 排队：构造并排队心跳帧
bool LoadGen::queue_heartbeat(Client &client, Clock::time_point now)
{
    if (client.next_heartbeat_seq == 0)
    {
        return false;
    }
    const std::uint64_t seq = client.next_heartbeat_seq;
    auto insert_result = client.pending_heartbeats.emplace(seq, now);
    if (!insert_result.second)
    {
        return false;
    }
    std::string payload;
    append_u64(payload, seq);
    if (!queue_frame(client, static_cast<std::uint16_t>(MessageType::heartbeat), payload))
    {
        client.pending_heartbeats.erase(seq);
        return false;
    }
    ++client.next_heartbeat_seq;
    if (state_ == States::measure)
    {
        ++result_.heartbeat_frames;
    }
    return true;
}

// 排队：构造并排队 move 帧
bool LoadGen::queue_move(Client &client)
{
    const std::int32_t dy = client.next_move_positive ? 1 : -1;
    std::string payload;
    append_i32(payload, 0);
    append_i32(payload, dy);
    if (!queue_frame(client, static_cast<std::uint16_t>(MessageType::move), payload))
    {
        return false;
    }
    client.next_move_positive = !client.next_move_positive;
    if (state_ == States::measure)
    {
        ++result_.sent_move_frames;
    }
    return true;
}

// 排队：编码一帧并追加到写缓冲
bool LoadGen::queue_frame(Client &client, std::uint16_t type, const std::string &payload)
{
    std::string frame;
    if (!Codec::encode(type, payload, frame))
    {
        return false;
    }
    client.write_buffer.append(frame);
    return true;
}

std::size_t LoadGen::active_client_count() const
{
    std::size_t cnt = 0;
    for (auto &it : clients_)
    {
        if (it.second.state == Clientstates::active)
        {
            ++cnt;
        }
    }
    return cnt;
}

void LoadGen::record_failure(const std::string &reason)
{
    if (result_.first_failure.recorded)
    {
        return;
    }
    result_.first_failure.state = state_;
    result_.first_failure.reason = reason;
    result_.first_failure.has_client = false;
    result_.first_failure.client_index = 0;
    result_.first_failure.active_clients = active_client_count();
    result_.first_failure.recorded = true;
}

void LoadGen::record_failure(std::size_t client_index, const std::string &reason)
{
    if (result_.first_failure.recorded)
    {
        return;
    }
    result_.first_failure.state = state_;
    result_.first_failure.reason = reason;
    result_.first_failure.has_client = true;
    result_.first_failure.client_index = client_index;
    result_.first_failure.active_clients = active_client_count();
    result_.first_failure.recorded = true;
}

// 失败：记录失败原因并关闭连接
void LoadGen::fail_client(int fd, Failurestates failure)
{
    auto it = clients_.find(fd);
    if (it == clients_.end())
    {
        record_failure("fail_client_unknown_fd");
        transition_to(States::failed, Clock::now());
        return;
    }
    switch (failure)
    {
        case Failurestates::connect_error:
        case Failurestates::connect_timeout:
            {
                ++result_.connection_failures;
                break;
            }
        case Failurestates::join_error:
        case Failurestates::join_timeout:
            {
                ++result_.join_failures;
                break;
            }
        case Failurestates::socket_error:
            {
                if (it->second.state == Clientstates::connecting)
                {
                    ++result_.connection_failures;
                }
                else if (it->second.state == Clientstates::joining)
                {
                    ++result_.join_failures;
                }
                break;
            }
        case Failurestates::protocol_error:
            {
                ++result_.protocol_errors;
                break;
            }
        case Failurestates::unexpected_close:
            {
                ++result_.unexpected_closes;
                break;
            }
    }
    record_failure(it->second.index, failure_name(failure));
    close_client(fd, Clientstates::failed);
    transition_to(States::failed, Clock::now());
}

// 关闭：以指定终态关闭连接并更新统计
void LoadGen::close_client(int fd, Clientstates final_state)
{
    auto it = clients_.find(fd);
    if (it == clients_.end())
    {
        return;
    }
    it->second.state = final_state;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (it->second.fd != -1)
    {
        close(it->second.fd);
        it->second.fd = -1;
    }
    clients_.erase(it);
}

// 记录快照：统计快照间隔与 tick 缺口
void LoadGen::record_snapshot(Client &client, std::uint64_t tick_id, Clock::time_point now)
{
    if (client.has_tick && tick_id <= client.last_tick_id)
    {
        ++result_.protocol_errors;
        client.state = Clientstates::failed;
        return;
    }
    if (state_ == States::measure)
    {
        ++result_.snapshot_frames;
        if (client.has_tick && client.last_snapshot_time >= measure_started_time_)
        {
            if (tick_id > client.last_tick_id + 1)
            {
                result_.tick_gaps += tick_id - client.last_tick_id - 1;
            }
            if (now > client.last_snapshot_time)
            {
                const auto interval = std::chrono::duration_cast<std::chrono::microseconds>(now - client.last_snapshot_time).count();
                snapshot_interval_us_.push_back(static_cast<std::uint64_t>(interval));
            }
        }
    }
    client.last_tick_id = tick_id;
    client.has_tick = true;
    client.last_snapshot_time = now;
    client.has_snapshot = true;
}

// 记录心跳 ack：统计往返时延样本
void LoadGen::record_heartbeat_ack(Client &client, std::uint64_t seq, Clock::time_point now)
{
    auto it = client.pending_heartbeats.find(seq);
    if (it == client.pending_heartbeats.end() || now < it->second)
    {
        ++result_.protocol_errors;
        client.state = Clientstates::failed;
        return;
    }
    Clock::time_point sent_time = it->second;
    client.pending_heartbeats.erase(it);
    bool measured_heartbeat = measure_started_time_ != Clock::time_point{} && sent_time >= measure_started_time_ && (measure_finished_time_ == Clock::time_point{} || sent_time < measure_finished_time_);
    if (!measured_heartbeat)
    {
        return;
    }
    ++result_.heartbeat_acks;
    auto temp = std::chrono::duration_cast<std::chrono::microseconds>(now - sent_time).count();
    heartbeat_rtt_us_.push_back(static_cast<std::uint64_t>(temp));
}

// 记录调度延迟：保存调度器落后样本
void LoadGen::record_scheduler_lag(Clock::time_point expected, Clock::time_point actual)
{
    if (actual <= expected)
    {
        scheduler_lag_us_.push_back(0);
        return;
    }
    auto temp = std::chrono::duration_cast<std::chrono::microseconds>(actual - expected).count();
    scheduler_lag_us_.push_back(static_cast<std::uint64_t>(temp));
}

// 汇总：填充 Result 中的派生统计（速率/正确性/分位数）
void LoadGen::finalize_result()
{
    result_.heartbeat_rtt = summarize_samples(heartbeat_rtt_us_);
    result_.snapshot_interval = summarize_samples(snapshot_interval_us_);
    result_.scheduler_lag = summarize_samples(scheduler_lag_us_);
    if (measure_finished_time_ > measure_started_time_)
    {
        double measure_seconds = std::chrono::duration<double>(measure_finished_time_ - measure_started_time_).count();
        if (measure_seconds > 0.0)
        {
            result_.actual_move_frames_per_second =
                static_cast<double>(result_.sent_move_frames) / measure_seconds;
            result_.snapshot_frames_per_second =
                static_cast<double>(result_.snapshot_frames) / measure_seconds;
        }
    }
    bool client_workload_complete = true;
    for (const auto &entry : clients_)
    {
        if (!entry.second.pending_heartbeats.empty() || !entry.second.write_buffer.empty() || !entry.second.has_snapshot)
        {
            client_workload_complete = false;
            break;
        }
    }
    result_.workload_completed = state_ == States::finished && all_clients_active() &&
        client_workload_complete &&
        result_.connection_attempts == config_.client_count &&
        result_.connection_successes == config_.client_count &&
        result_.join_successes == config_.client_count &&
        measure_finished_time_ > measure_started_time_;
    bool move_accounting_valid = result_.planned_move_frames >= result_.sent_move_frames &&
        result_.planned_move_frames - result_.sent_move_frames == result_.missed_move_deadlines;
    result_.correctness_passed = result_.workload_completed && result_.connection_failures == 0 &&
        result_.join_failures == 0 &&
        result_.planned_move_frames > 0 &&
        result_.sent_move_frames > 0 &&
        move_accounting_valid &&
        result_.heartbeat_frames > 0 &&
        result_.heartbeat_frames == result_.heartbeat_acks &&
        result_.heartbeat_rtt.count == result_.heartbeat_acks &&
        result_.snapshot_frames > 0 &&
        result_.tick_gaps == 0 &&
        result_.error_frames == 0 &&
        result_.protocol_errors == 0 &&
        result_.unexpected_closes == 0;
}

// 汇总样本：计算 p50/p95/p99 与最大值
LoadGen::SampleSummary LoadGen::summarize_samples(std::vector<std::uint64_t> samples)
{
    SampleSummary summary;
    if (samples.empty())
    {
        return summary;
    }
    std::sort(samples.begin(), samples.end());
    auto percentile = [&samples](double value)
    {
        const double rank = std::ceil(value * static_cast<double>(samples.size()));
        const std::size_t index = rank <= 1.0 ? 0 : static_cast<std::size_t>(rank) - 1;
        return static_cast<double>(samples[index]) / 1000.0;
    };
    summary.count = samples.size();
    summary.p50_ms = percentile(0.50);
    summary.p95_ms = percentile(0.95);
    summary.p99_ms = percentile(0.99);
    summary.maximum_ms = static_cast<double>(samples.back()) / 1000.0;
    return summary;
}
