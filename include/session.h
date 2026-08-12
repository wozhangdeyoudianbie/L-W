#ifndef SESSION_H
#define SESSION_H

#include "connection.h"
#include <chrono>
#include <cstdint>

class Session
{
public:
    using Clock = std::chrono::steady_clock;
    enum class States
    {
        online,
        offline
    };
    Session(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection);   // 构造：绑定连接并置为在线
    std::uint32_t room_id() const;                                 // 查询：房间号
    std::uint64_t player_id() const;                               // 查询：玩家 id
    States state() const;                                          // 查询：会话状态（在线/离线）
    Connection::ConnectionPtr connection() const;                  // 查询：当前绑定的连接（可能为空）
    Clock::time_point offline_deadline() const;                    // 查询：离线重连截止时间
    bool matches(const Connection::ConnectionPtr &connection) const;    // 判断：传入连接是否为当前绑定连接
    bool bind(const Connection::ConnectionPtr &connection, Clock::time_point now);      // 重新绑定：离线会话绑定到新连接
    bool detach(const Connection::ConnectionPtr &connection, Clock::time_point offline_deadline);   // 转为离线：仅当传入连接是当前绑定时解绑
    bool expired(Clock::time_point now) const;                     // 查询：离线会话是否已超过重连截止时间
private:
    std::uint32_t room_id_;
    std::uint64_t player_id_;
    States state_;
    std::weak_ptr<Connection> connection_;
    Clock::time_point offline_deadline_;
};

#endif
