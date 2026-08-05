#ifndef ROOM_H
#define ROOM_H

#include "connection.h"
#include "game_command.h"
#include "game_state.h"
#include "protocol.h"
#include "room_state_machine.h"
#include<cstddef>
#include<cstdint>
#include<memory>
#include<string>
#include<unordered_map>
#include<vector>

class Room
{
public:
    enum class Joinstates
    {
        success,
        invalid_connection,
        invalid_player_name,
        invalid_state,
        room_full,
        player_id_exhausted
    };
    enum class Commandstates
    {
        success,
        invalid_state,
        player_not_found,
        already_submitted
    };
    enum class Tickstates
    {
        success,
        invalid_state
    };
    struct JoinResult
    {
        Joinstates state;
        std::uint64_t player_id;
        std::vector<MemberInfo> members;
    };
    struct TickResult
    {
        Tickstates state;
        std::uint64_t tick_id;
        std::size_t processed_commands;
        std::size_t successful_commands;
        std::vector<PlayerGameState> snapshot;
    };
    Room(std::uint32_t room_id, std::size_t capacity);
    std::uint32_t id() const;
    std::size_t capacity() const;
    std::size_t member_count() const;
    std::uint64_t tick_id() const;                      // 当前结算编号
    std::size_t pending_command_count() const;          // 待结算的命令数
    Roomstatemachine::States state() const;             // 房间状态：等待/进行中/已结束
    std::vector<PlayerGameState> game_snapshot() const; // 权威状态快照（按玩家 id 排序）
    Roomstatemachine::Transitionstates start(bool ready_to_start);   // 开局（仅 waiting 态，失败不改状态）
    Roomstatemachine::Transitionstates finish(bool should_finish);   // 结束对局（仅 running 态）
    bool contains(std::uint64_t player_id) const;
    JoinResult join(const Connection::ConnectionPtr &connection, const std::string &player_name);  // 玩家加入房间
    bool leave(std::uint64_t player_id);                // 玩家离开（运行中同步清游戏状态）
    Commandstates submit_move(std::uint64_t player_id, std::int32_t dx, std::int32_t dy);   // 提交移动命令，tick 时结算
    Commandstates submit_attack(std::uint64_t player_id, std::uint64_t target_player_id);   // 提交攻击命令，tick 时结算
    TickResult tick();                                  // 结算一批命令，推进一帧
    std::vector<Connection::ConnectionPtr> connections(std::uint64_t excluded_player_id = 0) const;  // 房间内其他玩家连接（用于广播）
private:
    struct Member
    {
        std::uint64_t player_id;
        std::string player_name;
        std::weak_ptr<Connection> connection;
    };
    bool has_pending_command(std::uint64_t player_id) const;    // 该玩家本帧是否已提交过命令
    std::vector<Gamecommand> take_pending_commands();           // 取走并清空待结算命令
    bool process_command(const Movecommand &command);           // 一条移动命令交给权威状态执行
    bool process_command(const Attackcommand &command);         // 一条攻击命令交给权威状态执行
    std::uint32_t room_id_;
    std::size_t capacity_;
    std::uint64_t next_player_id_;
    Roomstatemachine state_machine_;
    Gamestate game_state_;
    std::uint64_t tick_id_;
    std::unordered_map<std::uint64_t, Member> members_;
    std::vector<Gamecommand> pending_commands_;
};

#endif
