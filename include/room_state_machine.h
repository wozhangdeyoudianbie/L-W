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
    States state() const;                    // 当前状态：等待/进行中/已结束
    bool can_join() const;                   // 是否允许加入（仅 waiting 态）
    Transitionstates start(bool ready_to_start);   // 等待 → 进行中
    Transitionstates finish(bool should_finish);   // 进行中 → 已结束
private:
    States state_;
};



#endif
