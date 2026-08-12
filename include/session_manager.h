#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "session.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SessionManager
{
public:
    using Clock = Session::Clock;
    static constexpr std::size_t TOKEN_SIZE = 32;
    enum class States
    {
        success,
        wrong_thread,
        invalid_connection,
        already_bound,
        invalid_token,
        session_online,
        session_expired,
        session_not_expired,
        not_bound,
        internal_error
    };
    struct CreateResult
    {
        States state = States::internal_error;
        std::string token;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct LookupResult
    {
        States state = States::not_bound;
        std::string token;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct ResumeResult
    {
        States state = States::internal_error;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct DetachResult
    {
        States state = States::not_bound;
        std::string token;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    struct ExpiredSession
    {
        std::string token;
        std::uint32_t room_id = 0;
        std::uint64_t player_id = 0;
    };
    SessionManager(std::chrono::milliseconds reconnect_timeout);   // 构造：保存重连超时时间
    CreateResult create(const Connection::ConnectionPtr &connection, std::uint32_t room_id, std::uint64_t player_id);   // 创建会话：建立在线会话并签发重连令牌
    LookupResult lookup(const Connection::ConnectionPtr &connection) const;                 // 查询：按连接查找会话信息
    ResumeResult resume(const Connection::ConnectionPtr &connection, const std::string &token, Clock::time_point now);  // 重连：凭令牌将离线会话绑定到新连接
    States rollback_resume(const Connection::ConnectionPtr &connection);                    // 回滚重连：恢复 bind 前的离线状态
    DetachResult detach(const Connection::ConnectionPtr &connection, Clock::time_point now); // 转为离线：解绑连接并登记重连截止时间
    States erase_by_connection(const Connection::ConnectionPtr &connection);                 // 删除：按连接移除会话与绑定关系
    std::vector<ExpiredSession> expired_sessions(Clock::time_point now) const;               // 查询：返回所有已过期的离线会话
    States erase_expired(const std::string &token, Clock::time_point now);                   // 删除：按令牌移除过期会话
private:
    std::string generate_token() const;   // 生成随机重连令牌（十六进制字符串）
    std::chrono::milliseconds reconnect_timeout_;
    std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;
    std::unordered_map<Connection *, std::string> bindings_;
};

#endif
