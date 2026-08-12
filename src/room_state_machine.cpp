#include "room_state_machine.h"

// 构造：初始为等待态
Roomstatemachine::Roomstatemachine()
    :state_(States::waiting)
{
}

// 查询：当前状态（等待/进行中/已结束）
Roomstatemachine::States Roomstatemachine::state() const
{
    return state_;
}

// 判断：是否允许加入（仅 waiting 态）
bool Roomstatemachine::can_join() const
{
    return state_ == States::waiting;
}

// 启动：等待 → 进行中
Roomstatemachine::Transitionstates Roomstatemachine::start(bool ready_to_start)
{
    if (state_ != States::waiting)
        return Transitionstates::invalid_state;
    if (!ready_to_start)
        return Transitionstates::condition_not_met;
    state_ = States::running;
    return Transitionstates::success;
}

// 结束：进行中 → 已结束
Roomstatemachine::Transitionstates Roomstatemachine::finish(bool should_finish)
{
    if (state_ != States::running)
        return Transitionstates::invalid_state;
    if (!should_finish)
        return Transitionstates::condition_not_met;
    state_ = States::finished;
    return Transitionstates::success;
}
