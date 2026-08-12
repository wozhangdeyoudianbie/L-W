#include "session.h"
#include <cassert>

// 构造：绑定新连接并置为在线
Session::Session(std::uint32_t room_id, std::uint64_t player_id, const Connection::ConnectionPtr &connection)
    :room_id_(room_id), player_id_(player_id), state_(States::online), connection_(connection)
{
}

// 查询：房间号
std::uint32_t Session::room_id() const
{
    return room_id_;
}

// 查询：玩家 id
std::uint64_t Session::player_id() const
{
    return player_id_;
}

// 查询：会话状态（在线/离线）
Session::States Session::state() const
{
    return state_;
}

// 查询：离线重连截止时间（仅离线状态有意义）
Session::Clock::time_point Session::offline_deadline() const
{
    return offline_deadline_;
}

// 查询：当前绑定的连接（弱指针上锁，可能为空）
Connection::ConnectionPtr Session::connection() const
{
    return connection_.lock();
}

// 判断：传入连接是否为当前绑定连接（须在线且地址级相同）
bool Session::matches(const Connection::ConnectionPtr &connection) const
{
    if (state_ != States::online)
    {
        return false;
    }
    if (!connection)
    {
        return false;
    }
    Connection::ConnectionPtr cur = connection_.lock();
    if (!cur)
    {
        return false;
    }
    return cur.get() == connection.get();
}

// 重新绑定：离线会话绑定到新连接（保留原截止时间，供回滚恢复）
bool Session::bind(const Connection::ConnectionPtr &connection, Clock::time_point now)
{
    if (!connection)
    {
        return false;
    }
    if (state_ != States::offline)
    {
        return false;
    }
    if (now >= offline_deadline_)
    {
        return false;
    }
    connection_ = connection;
    state_ = States::online;
    return true;
}

// 转为离线：仅当传入连接就是当前绑定连接时解绑并登记截止时间
bool Session::detach(const Connection::ConnectionPtr &connection, Clock::time_point offline_deadline)
{
    if (!connection)
    {
        return false;
    }
    if (state_ != States::online)
    {
        return false;
    }
    if (!matches(connection))
    {
        return false;
    }
    connection_.reset();
    offline_deadline_ = offline_deadline;
    state_ = States::offline;
    return true;
}

// 查询：离线会话是否已超过重连截止时间
bool Session::expired(Clock::time_point now) const
{
    if (state_ != States::offline)
    {
        return false;
    }
    return now >= offline_deadline_;
}
