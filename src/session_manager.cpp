#include "session_manager.h"
#include <array>
#include <cerrno>
#include <sys/random.h>
#include <utility>

// 构造：保存重连超时时间
SessionManager::SessionManager(std::chrono::milliseconds reconnect_timeout)
    :reconnect_timeout_(reconnect_timeout)
{
}

// 生成随机重连令牌（十六进制字符串）
std::string SessionManager::generate_token() const
{
    static constexpr char HEX[] = "0123456789abcdef";
    std::array<unsigned char, TOKEN_SIZE / 2> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        ssize_t count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0)
        {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        return {};
    }
    std::string token;
    token.reserve(TOKEN_SIZE);
    for (unsigned char byte : bytes)
    {
        token.push_back(HEX[(byte >> 4) & 0x0F]);
        token.push_back(HEX[byte & 0x0F]);
    }
    return token;
}

// 创建会话：为连接建立在线会话并签发重连令牌
SessionManager::CreateResult SessionManager::create(const Connection::ConnectionPtr &connection, std::uint32_t room_id, std::uint64_t player_id)
{
    CreateResult result{};
    if (!connection)
    {
        result.state = States::invalid_connection;
        return result;
    }
    if (bindings_.find(connection.get()) != bindings_.end())
    {
        result.state = States::already_bound;
        return result;
    }
    std::string token = generate_token();
    if (token.empty())
    {
        result.state = States::internal_error;
        return result;
    }
    auto session_result = sessions_.emplace(token, std::make_unique<Session>(room_id, player_id, connection));
    if (!session_result.second)
    {
        result.state = States::internal_error;
        return result;
    }
    auto bind_result = bindings_.emplace(connection.get(), token);
    if (!bind_result.second)
    {
        sessions_.erase(session_result.first);
        result.state = States::internal_error;
        return result;
    }
    result.state = States::success;
    result.token = token;
    result.room_id = room_id;
    result.player_id = player_id;
    return result;
}

// 查询：按连接查找会话信息
SessionManager::LookupResult SessionManager::lookup(const Connection::ConnectionPtr &connection) const
{
    LookupResult result{};
    if (!connection)
    {
        result.state = States::invalid_connection;
        return result;
    }
    auto bind_it = bindings_.find(connection.get());
    if (bind_it == bindings_.end())
    {
        result.state = States::not_bound;
        return result;
    }
    const std::string &token = bind_it->second;
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        result.state = States::internal_error;
        return result;
    }
    result.state = States::success;
    result.token = token;
    result.room_id = session_it->second->room_id();
    result.player_id = session_it->second->player_id();
    return result;
}

// 重连：凭令牌将离线会话绑定到新连接
SessionManager::ResumeResult SessionManager::resume(const Connection::ConnectionPtr &connection, const std::string &token, Clock::time_point now)
{
    ResumeResult result{};
    if (!connection)
    {
        result.state = States::invalid_connection;
        return result;
    }
    if (bindings_.find(connection.get()) != bindings_.end())
    {
        result.state = States::already_bound;
        return result;
    }
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        result.state = States::invalid_token;
        return result;
    }
    Session *session = session_it->second.get();
    if (session->state() != Session::States::offline)
    {
        result.state = States::session_online;
        return result;
    }
    if (session->expired(now))
    {
        result.state = States::session_expired;
        return result;
    }
    bindings_.emplace(connection.get(), token);
    if (!session->bind(connection, now))
    {
        bindings_.erase(connection.get());
        result.state = States::internal_error;
        return result;
    }
    result.state = States::success;
    result.room_id = session->room_id();
    result.player_id = session->player_id();
    return result;
}

// 回滚重连：恢复 bind 前的离线状态与原截止时间
SessionManager::States SessionManager::rollback_resume(const Connection::ConnectionPtr &connection)
{
    if (!connection)
    {
        return States::invalid_connection;
    }
    auto bind_it = bindings_.find(connection.get());
    if (bind_it == bindings_.end())
    {
        return States::not_bound;
    }
    const std::string token = bind_it->second;
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        return States::internal_error;
    }
    Session *session = session_it->second.get();
    if (!session->detach(connection, session->offline_deadline()))
    {
        return States::internal_error;
    }
    bindings_.erase(bind_it);
    return States::success;
}

// 转为离线：解绑连接并登记重连截止时间
SessionManager::DetachResult SessionManager::detach(const Connection::ConnectionPtr &connection, Clock::time_point now)
{
    DetachResult result{};
    if (!connection)
    {
        result.state = States::invalid_connection;
        return result;
    }
    auto bind_it = bindings_.find(connection.get());
    if (bind_it == bindings_.end())
    {
        result.state = States::not_bound;
        return result;
    }
    const std::string token = bind_it->second;
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        result.state = States::internal_error;
        return result;
    }
    Session *session = session_it->second.get();
    if (!session->detach(connection, now + reconnect_timeout_))
    {
        result.state = States::internal_error;
        return result;
    }
    bindings_.erase(bind_it);
    result.state = States::success;
    result.token = token;
    result.room_id = session->room_id();
    result.player_id = session->player_id();
    return result;
}

// 删除：按连接移除会话与绑定关系
SessionManager::States SessionManager::erase_by_connection(const Connection::ConnectionPtr &connection)
{
    if (!connection)
    {
        return States::invalid_connection;
    }
    auto bind_it = bindings_.find(connection.get());
    if (bind_it == bindings_.end())
    {
        return States::not_bound;
    }
    const std::string token = bind_it->second;
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        return States::internal_error;
    }
    bindings_.erase(bind_it);
    sessions_.erase(session_it);
    return States::success;
}

// 查询：返回所有已过期的离线会话（只收集身份，不修改映射）
std::vector<SessionManager::ExpiredSession> SessionManager::expired_sessions(Clock::time_point now) const
{
    std::vector<ExpiredSession> expired{};
    for (const auto &entry : sessions_)
    {
        const Session *session = entry.second.get();
        if (session->state() != Session::States::offline || !session->expired(now))
        {
            continue;
        }
        ExpiredSession item;
        item.token = entry.first;
        item.room_id = session->room_id();
        item.player_id = session->player_id();
        expired.push_back(std::move(item));
    }
    return expired;
}

// 删除：按令牌移除过期会话
SessionManager::States SessionManager::erase_expired(const std::string &token, SessionManager::Clock::time_point now)
{
    auto session_it = sessions_.find(token);
    if (session_it == sessions_.end())
    {
        return States::invalid_token;
    }
    Session *session = session_it->second.get();
    if (session->state() != Session::States::offline)
    {
        return States::session_online;
    }
    if (!session->expired(now))
    {
        return States::session_not_expired;
    }
    sessions_.erase(session_it);
    return States::success;
}
