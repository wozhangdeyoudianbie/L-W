#ifndef CHECKPOINT_STORE_H
#define CHECKPOINT_STORE_H

#include "checkpoint.h"
#include<condition_variable>
#include<cstdint>
#include<mutex>
#include<optional>
#include<string>
#include<thread>

class CheckpointStore
{
public:
    enum class Loadstates
    {
        success,
        not_found,
        invalid_state,
        io_error,
        decode_error
    };
    struct LoadResult
    {
        Loadstates state = Loadstates::io_error;
        ServerCheckpoint checkpoint;
    };
    struct States
    {
        std::uint64_t last_submitted_generation = 0;
        std::uint64_t last_committed_generation = 0;
        std::uint64_t last_failed_generation = 0;
    };
    using Status = States;                       // 兼容现有测试使用的旧类型名
    explicit CheckpointStore(std::string path);   // 构造：保存路径与临时文件路径
    ~CheckpointStore();                           // 析构：停止后台写线程
    CheckpointStore(const CheckpointStore &) = delete;
    CheckpointStore &operator=(const CheckpointStore &) = delete;
    LoadResult load();                            // 加载：读取并解码检查点文件（须在 start 前）
    bool start();                                 // 启动：创建后台写线程
    bool submit(ServerCheckpoint checkpoint);     // 提交：投递一份检查点给后台线程（代号须递增）
    bool flush();                                 // 等待：直到全部已提交检查点落盘
    bool stop();                                  // 停止：退出后台线程并返回是否全部落盘
    States states() const;                       // 查询：已提交/已落盘/失败的最新代号
    Status status() const;                       // 兼容现有测试使用的旧函数名
private:
    void thread_main();                           // 后台线程入口：循环取待写检查点并落盘
    bool commit_checkpoint(const ServerCheckpoint &checkpoint);   // 落盘：编码→写临时文件→fsync→rename
    std::string path_;
    std::string temporary_path_;
    bool valid_;
    mutable std::mutex mutex_;
    std::condition_variable task_condition_;
    std::condition_variable idle_condition_;
    std::thread thread_;
    bool started_;
    bool stopping_;
    bool writing_;
    std::optional<ServerCheckpoint> pending_checkpoint_;
    std::uint64_t last_submitted_generation_;
    std::uint64_t last_committed_generation_;
    std::uint64_t last_failed_generation_;
};

#endif
