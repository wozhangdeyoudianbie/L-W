#include "checkpoint_store.h"
#include "checkpoint_codec.h"
#include<cerrno>
#include<cstddef>
#include<cstdio>
#include<fcntl.h>
#include<string>
#include<sys/stat.h>
#include<sys/types.h>
#include<unistd.h>
#include<utility>

namespace
{
    constexpr std::size_t MAX_CHECKPOINT_FILE_SIZE = 64U * 1024U * 1024U;
    constexpr std::size_t READ_BUFFER_SIZE = 8192;
    // 写入：fd 写满全部数据（处理 EINTR）
    bool write_all(int fd, const std::string &data)
    {
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
            if (written > 0)
            {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        return true;
    }
    // 同步：fsync 落盘（处理 EINTR）
    bool sync_fd(int fd)
    {
        while (::fsync(fd) == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        return true;
    }
    // 提取：路径的父目录
    std::string parent_directory(const std::string &path)
    {
        const std::size_t position = path.find_last_of('/');
        if (position == std::string::npos)
        {
            return ".";
        }
        if (position == 0)
        {
            return "/";
        }
        return path.substr(0, position);
    }
    // 删除：临时文件（失败忽略）
    void remove_temporary_file(const std::string &path)
    {
        ::unlink(path.c_str());
    }
}

// 构造：保存路径与临时文件路径
CheckpointStore::CheckpointStore(std::string path)
    :path_(std::move(path)), temporary_path_(path_ + ".tmp"), valid_(!path_.empty()), started_(false), stopping_(false),
    writing_(false), last_submitted_generation_(0), last_committed_generation_(0), last_failed_generation_(0)
{
}

// 析构：停止后台写线程
CheckpointStore::~CheckpointStore()
{
    stop();
}

// 加载：读取并解码检查点文件（须在 start 前）
CheckpointStore::LoadResult CheckpointStore::load()
{
    LoadResult result;
    std::unique_lock<std::mutex> lock(mutex_);
    if (!valid_ || started_ || stopping_ || writing_ || pending_checkpoint_.has_value())
    {
        result.state = Loadstates::invalid_state;
        return result;
    }
    const int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        if (errno == ENOENT)
        {
            result.state = Loadstates::not_found;
        }
        else
        {
            result.state = Loadstates::io_error;
        }
        return result;
    }
    std::string data;
    char buffer[READ_BUFFER_SIZE];
    bool read_failed = false;
    bool file_too_large = false;
    try
    {
        while (true)
        {
            const ssize_t count = ::read(fd, buffer, sizeof(buffer));
            if (count > 0)
            {
                const std::size_t read_size = static_cast<std::size_t>(count);
                if (read_size > MAX_CHECKPOINT_FILE_SIZE || data.size() > MAX_CHECKPOINT_FILE_SIZE - read_size)
                {
                    file_too_large = true;
                    break;
                }
                data.append(buffer, read_size);
                continue;
            }
            if (count == 0)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            read_failed = true;
            break;
        }
    }
    catch (...)
    {
        close(fd);
        result.state = Loadstates::decode_error;
        return result;
    }
    const bool close_succeeded = (close(fd) == 0);
    if (read_failed || !close_succeeded)
    {
        result.state = Loadstates::io_error;
        return result;
    }
    if (file_too_large)
    {
        result.state = Loadstates::decode_error;
        return result;
    }
    ServerCheckpoint checkpoint;
    const CheckpointCodec::States decode_state = CheckpointCodec::decode(data, checkpoint);
    if (decode_state != CheckpointCodec::States::success)
    {
        result.state = Loadstates::decode_error;
        return result;
    }
    last_submitted_generation_ = checkpoint.generation;
    last_committed_generation_ = checkpoint.generation;
    result.state = Loadstates::success;
    result.checkpoint = std::move(checkpoint);
    return result;
}

// 启动：创建后台写线程
bool CheckpointStore::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!valid_ || started_ || stopping_ || writing_ || pending_checkpoint_.has_value())
    {
        return false;
    }

    try
    {
        thread_ = std::thread(&CheckpointStore::thread_main, this);
    }
    catch (...)
    {
        return false;
    }
    started_ = true;
    return true;
}

// 提交：投递一份检查点给后台线程（代号须递增）
bool CheckpointStore::submit(ServerCheckpoint checkpoint)
{
    const std::uint64_t generation = checkpoint.generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !started_ || stopping_ || generation == 0 || generation <= last_submitted_generation_)
        {
            return false;
        }
        pending_checkpoint_ = std::move(checkpoint);
        last_submitted_generation_ = generation;
    }
    task_condition_.notify_one();
    return true;
}

// 等待：直到全部已提交检查点落盘
bool CheckpointStore::flush()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!valid_)
    {
        return false;
    }
    idle_condition_.wait(lock, [this]()
    {
        return !pending_checkpoint_.has_value() && !writing_;
    });
    return last_submitted_generation_ == last_committed_generation_;
}

// 停止：退出后台线程并返回是否全部落盘
bool CheckpointStore::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_)
        {
            return valid_ && last_submitted_generation_ == last_committed_generation_;
        }
        stopping_ = true;
    }
    task_condition_.notify_one();
    if (thread_.joinable())
    {
        thread_.join();
    }
    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
        stopping_ = false;
        writing_ = false;
        complete = last_submitted_generation_ == last_committed_generation_;
    }
    idle_condition_.notify_all();
    return complete;
}

// 查询：已提交/已落盘/失败的最新代号
CheckpointStore::States CheckpointStore::states() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return States
    {
        last_submitted_generation_,
        last_committed_generation_,
        last_failed_generation_
    };
}

// 兼容现有测试使用的旧函数名
CheckpointStore::Status CheckpointStore::status() const
{
    return states();
}

// 后台线程入口：循环取待写检查点并落盘
void CheckpointStore::thread_main()
{
    while (true)
    {
        ServerCheckpoint checkpoint;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            task_condition_.wait(lock, [this]()
            {
                return stopping_ || pending_checkpoint_.has_value();
            });
            if (stopping_ && !pending_checkpoint_.has_value())
            {
                break;
            }
            checkpoint = std::move(*pending_checkpoint_);
            pending_checkpoint_.reset();
            writing_ = true;
        }
        bool committed = false;
        try
        {
            committed = commit_checkpoint(checkpoint);
        }
        catch (...)
        {
            committed = false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            writing_ = false;
            if (committed)
            {
                last_committed_generation_ = checkpoint.generation;
            }
            else
            {
                last_failed_generation_ = checkpoint.generation;
            }
        }
        idle_condition_.notify_all();
    }
    idle_condition_.notify_all();
}

// 落盘：编码→写临时文件→fsync→rename（原子替换）
bool CheckpointStore::commit_checkpoint(const ServerCheckpoint &checkpoint)
{
    std::string data;
    if (CheckpointCodec::encode(checkpoint, data) != CheckpointCodec::States::success)
    {
        return false;
    }
    const int fd = open(temporary_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        return false;
    }
    if (!write_all(fd, data) || !sync_fd(fd))
    {
        close(fd);
        remove_temporary_file(temporary_path_);
        return false;
    }
    if (close(fd) == -1)
    {
        remove_temporary_file(temporary_path_);
        return false;
    }
    if (rename(temporary_path_.c_str(), path_.c_str()) == -1)
    {
        remove_temporary_file(temporary_path_);
        return false;
    }
    const std::string directory = parent_directory(path_);
    const int directory_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd == -1)
    {
        return false;
    }
    const bool directory_synced = sync_fd(directory_fd);
    const bool directory_closed = (::close(directory_fd) == 0);
    return directory_synced && directory_closed;
}
