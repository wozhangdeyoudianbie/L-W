#include "room_state_machine.h"
#include <iostream>
#include <string>

namespace
{
    bool expect(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << message << '\n';
            return false;
        }
        return true;
    }

    bool test_initial_state()
    {
        Roomstatemachine machine;

        if (!expect(machine.state() == Roomstatemachine::States::waiting, "initial state is waiting"))
        {
            return false;
        }
        if (!expect(machine.can_join(), "waiting state allows join"))
        {
            return false;
        }

        return true;
    }

    bool test_start_transition()
    {
        Roomstatemachine machine;

        if (!expect(machine.start(false) == Roomstatemachine::Transitionstates::condition_not_met, "start condition not met"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::waiting && machine.can_join(), "failed start keeps waiting"))
        {
            return false;
        }

        if (!expect(machine.start(true) == Roomstatemachine::Transitionstates::success, "start succeeds"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::running && !machine.can_join(), "start enters running"))
        {
            return false;
        }

        if (!expect(machine.start(false) == Roomstatemachine::Transitionstates::invalid_state, "repeated start reports invalid state"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::running, "repeated start keeps running"))
        {
            return false;
        }

        return true;
    }

    bool test_finish_transition()
    {
        Roomstatemachine machine;

        if (!expect(machine.finish(true) == Roomstatemachine::Transitionstates::invalid_state, "finish before start"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::waiting, "invalid finish keeps waiting"))
        {
            return false;
        }

        if (!expect(machine.start(true) == Roomstatemachine::Transitionstates::success, "start before finish"))
        {
            return false;
        }
        if (!expect(machine.finish(false) == Roomstatemachine::Transitionstates::condition_not_met, "finish condition not met"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::running, "failed finish keeps running"))
        {
            return false;
        }

        if (!expect(machine.finish(true) == Roomstatemachine::Transitionstates::success, "finish succeeds"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::finished && !machine.can_join(), "finish enters finished"))
        {
            return false;
        }

        if (!expect(machine.finish(true) == Roomstatemachine::Transitionstates::invalid_state, "repeated finish"))
        {
            return false;
        }
        if (!expect(machine.start(true) == Roomstatemachine::Transitionstates::invalid_state, "finished cannot restart"))
        {
            return false;
        }
        if (!expect(machine.state() == Roomstatemachine::States::finished, "invalid transitions keep finished"))
        {
            return false;
        }

        return true;
    }

    bool test_repeated_lifecycle()
    {
        for (int i = 0; i < 1000; ++i)
        {
            Roomstatemachine machine;
            if (!expect(machine.start(true) == Roomstatemachine::Transitionstates::success, "repeated lifecycle start"))
            {
                return false;
            }
            if (!expect(machine.finish(true) == Roomstatemachine::Transitionstates::success, "repeated lifecycle finish"))
            {
                return false;
            }
        }

        return true;
    }
}

int main()
{
    struct TestCase
    {
        const char *name;
        bool (*function)();
    };

    const TestCase tests[] = {
        {"initial_state", test_initial_state},
        {"start_transition", test_start_transition},
        {"finish_transition", test_finish_transition},
        {"repeated_lifecycle", test_repeated_lifecycle}
    };

    for (const TestCase &test : tests)
    {
        if (!test.function())
        {
            return 1;
        }
        std::cout << "[PASS] " << test.name << '\n';
    }

    std::cout << "Room 状态机基础验收通过\n";
    return 0;
}
