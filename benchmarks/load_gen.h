#ifndef LOAD_GEN_H
#define LOAD_GEN_H

#include "buffer.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class LoadGen
{
public:
    enum class States
    {
        created,
        ramp_up,
        warm_up,
        measure,
        drain,
        finished,
        failed
    };
    struct Config
    {
        std::string address = "127.0.0.1";
        std::uint16_t port = 8080;
        std::size_t client_count = 400;
        std::size_t room_capacity = 4;
        std::size_t connections_per_second = 100;
        std::uint64_t connect_timeout_ms = 3000;
        std::uint64_t join_timeout_ms = 3000;
        std::uint64_t move_interval_ms = 100;
        std::uint64_t heartbeat_interval_ms = 2000;
        std::uint64_t warm_up_ms = 10000;
        std::uint64_t measure_ms = 60000;
        std::uint64_t drain_ms = 2000;
    };
    struct SampleSummary
    {
        std::size_t count = 0;
        double p50_ms = 0.0;
        double p95_ms = 0.0;
        double p99_ms = 0.0;
        double maximum_ms = 0.0;
    };
    struct Failure
    {
        bool recorded = false;
        States state = States::created;
        std::string reason;
        bool has_client = false;
        std::size_t client_index = 0;
        std::size_t active_clients = 0;
    };
    struct Result
    {
        std::size_t connection_attempts = 0;
        std::size_t connection_successes = 0;
        std::size_t connection_failures = 0;
        std::size_t join_successes = 0;
        std::size_t join_failures = 0;
        std::uint64_t planned_move_frames = 0;
        std::uint64_t sent_move_frames = 0;
        std::uint64_t missed_move_deadlines = 0;
        std::uint64_t heartbeat_frames = 0;
        std::uint64_t heartbeat_acks = 0;
        std::uint64_t snapshot_frames = 0;
        std::uint64_t tick_gaps = 0;
        std::uint64_t error_frames = 0;
        std::uint64_t protocol_errors = 0;
        std::uint64_t unexpected_closes = 0;
        double actual_move_frames_per_second = 0.0;
        double snapshot_frames_per_second = 0.0;
        SampleSummary heartbeat_rtt;
        SampleSummary snapshot_interval;
        SampleSummary scheduler_lag;
        bool workload_completed = false;
        bool correctness_passed = false;
        Failure first_failure;
    };
    explicit LoadGen(Config config);          // 构造：保存配置并初始化资源句柄
    ~LoadGen();                               // 析构：清理资源
    LoadGen(const LoadGen &) = delete;
    LoadGen &operator=(const LoadGen &) = delete;
    bool valid() const;                       // 查询：配置是否有效
    bool run();                               // 执行压测全流程
    States states() const;                    // 查询：当前阶段状态
    const Result &result() const;             // 查询：压测统计结果（仅结束后有意义）
    bool write_report(const std::string &directory) const;    // 写报告：成功或失败结果输出到指定目录
private:
    using Clock = std::chrono::steady_clock;
    enum class Clientstates
    {
        connecting,
        joining,
        active,
        closing,
        closed,
        failed
    };
    enum class Failurestates
    {
        connect_error,
        connect_timeout,
        join_error,
        join_timeout,
        heartbeat_timeout,
        socket_error,
        protocol_error,
        unexpected_close
    };
    struct Client
    {
        std::size_t index = 0;
        int fd = -1;
        Clientstates state = Clientstates::connecting;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
        std::uint64_t next_heartbeat_seq = 1;
        std::uint64_t last_tick_id = 0;
        bool has_tick = false;
        bool has_snapshot = false;
        bool next_move_positive = true;
        Buffer read_buffer;
        Buffer write_buffer;
        Clock::time_point state_deadline{};
        Clock::time_point next_heartbeat_time{};
        Clock::time_point next_move_time{};
        Clock::time_point last_snapshot_time{};
        std::unordered_map<std::uint64_t, Clock::time_point> pending_heartbeats;
    };
    bool validate_config() const;                            // 校验：配置参数合法性
    bool initialize();                                       // 初始化：创建 epoll 实例
    void cleanup();                                          // 清理：关闭所有客户端并销毁 epoll
    bool run_loop();                                         // 主循环：轮询事件并驱动客户端
    void transition_to(States state, Clock::time_point now); // 阶段切换：更新状态并设置截止时间
    bool start_due_clients(Clock::time_point now);           // 启动到期客户端：按速率创建连接
    bool create_client(std::size_t index, Clock::time_point now);    // 创建客户端：非阻塞 connect 并登记
    void drive_active_clients(Clock::time_point now);        // 驱动活跃客户端：到点发心跳与移动
    void check_client_deadlines(Clock::time_point now);      // 检查超时：连接/加入/心跳超时
    bool all_clients_active() const;                         // 查询：是否所有客户端都已激活
    bool poll_once(int timeout_ms);                          // 轮询：等待一次 epoll 事件并分发
    int calculate_wait_timeout_ms(Clock::time_point now) const;   // 计算：下一次 epoll 等待毫秒数
    bool update_client_events(const Client &client, std::uint32_t events);    // 事件处理：按可读/可写更新单个客户端
    bool finish_connect(Client &client, Clock::time_point now);   // 完成连接：非阻塞 connect 后续处理
    bool read_from_client(Client &client, Clock::time_point now); // 读取：socket 读入并拆帧处理
    bool write_to_client(Client &client);                    // 写入：写缓冲发往 socket
    bool process_frame(Client &client, std::uint16_t type, const std::string &payload, Clock::time_point now);    // 处理帧：解析服务器响应
    bool queue_join(Client &client);                         // 排队：构造 join 帧
    bool queue_heartbeat(Client &client, Clock::time_point now);   // 排队：构造心跳帧
    bool queue_move(Client &client);                         // 排队：构造 move 帧
    bool queue_frame(Client &client, std::uint16_t type, const std::string &payload);   // 排队：编码一帧追加到写缓冲
    void record_failure(const std::string &reason);          // 记录：保存无具体客户端的第一次失败
    void record_failure(std::size_t client_index, const std::string &reason);  // 记录：保存具体客户端的第一次失败
    std::size_t active_client_count() const;                 // 统计：当前 active 客户端数量
    void fail_client(int fd, Failurestates failure);         // 失败：记录原因并关闭连接
    void close_client(int fd, Clientstates final_state);     // 关闭：以指定终态关闭并更新统计
    void record_snapshot(Client &client, std::uint64_t tick_id, Clock::time_point now); // 记录快照：统计间隔与 tick 缺口
    void record_heartbeat_ack(Client &client, std::uint64_t seq, Clock::time_point now); // 记录心跳 ack：统计往返时延
    void record_scheduler_lag(Clock::time_point expected, Clock::time_point actual);     // 记录调度延迟：保存落后样本
    void finalize_result();                                  // 汇总：填充 Result 派生统计
    static const char *state_name(States state);             // 转换：压测阶段转报告文字
    static const char *failure_name(Failurestates failure);  // 转换：失败分类转报告文字
    static std::string make_system_error_reason(const char *operation, int error_number);   // 转换：系统错误转报告原因
    static SampleSummary summarize_samples(std::vector<std::uint64_t> samples);         // 汇总样本：p50/p95/p99 与最大值
    Config config_;
    bool valid_;
    int epoll_fd_;
    States state_;
    std::size_t next_client_index_;
    std::unordered_map<int, Client> clients_;
    Clock::time_point next_connect_time_;
    Clock::time_point phase_deadline_;
    Clock::time_point measure_started_time_;
    Clock::time_point measure_finished_time_;
    Result result_;
    std::vector<std::uint64_t> heartbeat_rtt_us_;
    std::vector<std::uint64_t> snapshot_interval_us_;
    std::vector<std::uint64_t> scheduler_lag_us_;
};

#endif
