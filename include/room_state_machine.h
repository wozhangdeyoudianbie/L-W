#ifndef ROOM_STATE_MACHINE
#define ROOM_STATE_MACHINE

class Roomstatemachine
{
public:
    enum class States
    {
        waiting,
        running,
        finished
    };
    enum class Transitionstates
    {
        success,
        invalid_state,
        condition_not_met
    };
    Roomstatemachine();
    States state() const;
    bool can_join() const;
    Transitionstates start(bool ready_to_start);
    Transitionstates finish(bool should_finish);
private:
    States state_;
};



#endif
