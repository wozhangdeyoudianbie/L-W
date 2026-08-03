#include "room_state_machine.h"

Roomstatemachine::Roomstatemachine()
    :state_(States::waiting)
{
}

Roomstatemachine::States Roomstatemachine::state() const
{
    return state_;
}

bool Roomstatemachine::can_join() const
{
    return state_ == States::waiting;
}

Roomstatemachine::Transitionstates Roomstatemachine::start(bool ready_to_start)
{
    if (state_ != States::waiting)
        return Transitionstates::invalid_state;
    if (!ready_to_start)
        return Transitionstates::condition_not_met;
    state_ = States::running;
    return Transitionstates::success;
}

Roomstatemachine::Transitionstates Roomstatemachine::finish(bool should_finish)
{
    if (state_ != States::running)
        return Transitionstates::invalid_state;
    if (!should_finish)
        return Transitionstates::condition_not_met;
    state_ = States::finished;
    return Transitionstates::success;
}
