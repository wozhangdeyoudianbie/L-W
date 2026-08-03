#include "game_state.h"
#include<cstdint>
#include<iostream>
#include<vector>

namespace
{
    bool check_impl(bool condition, const char *expression, int line)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] line " << line << ": " << expression << '\n';
            return false;
        }
        return true;
    }

#define CHECK(expression)                                      \
    do                                                         \
    {                                                          \
        if (!check_impl((expression), #expression, __LINE__))  \
        {                                                      \
            return false;                                      \
        }                                                      \
    } while (false)

    bool same_player(const PlayerGameState &a, const PlayerGameState &b)
    {
        return a.player_id == b.player_id
            && a.x == b.x
            && a.y == b.y
            && a.hp == b.hp;
    }

    bool same_snapshot(const std::vector<PlayerGameState> &a, const std::vector<PlayerGameState> &b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!same_player(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool test_initial_state()
    {
        Gamestate game;
        PlayerGameState state{};

        CHECK(game.player_count() == 0);
        CHECK(!game.contains(1));
        CHECK(!game.player_state(1, state));
        CHECK(game.snapshot().empty());
        return true;
    }

    bool test_initialize_validation_and_snapshot()
    {
        Gamestate game;

        CHECK(game.initialize({3, 1, 2}) == Gamestate::States::success);
        CHECK(game.player_count() == 3);

        std::vector<PlayerGameState> snapshot = game.snapshot();

        CHECK(snapshot.size() == 3);

        CHECK(snapshot[0].player_id == 1);
        CHECK(snapshot[0].x == 0);
        CHECK(snapshot[0].y == 0);
        CHECK(snapshot[0].hp == 100);

        CHECK(snapshot[1].player_id == 2);
        CHECK(snapshot[1].x == 1);
        CHECK(snapshot[1].y == 0);
        CHECK(snapshot[1].hp == 100);

        CHECK(snapshot[2].player_id == 3);
        CHECK(snapshot[2].x == 2);
        CHECK(snapshot[2].y == 0);
        CHECK(snapshot[2].hp == 100);

        CHECK(game.initialize(std::vector<std::uint64_t>{}) == Gamestate::States::empty_players);
        CHECK(same_snapshot(snapshot, game.snapshot()));

        CHECK(game.initialize({1, 0}) == Gamestate::States::invalid_player_id);
        CHECK(same_snapshot(snapshot, game.snapshot()));

        CHECK(game.initialize({1, 1}) == Gamestate::States::duplicate_player);
        CHECK(same_snapshot(snapshot, game.snapshot()));

        std::vector<std::uint64_t> too_many_players(102, 1);
        CHECK(game.initialize(too_many_players) == Gamestate::States::too_many_players);
        CHECK(same_snapshot(snapshot, game.snapshot()));
        return true;
    }

    bool test_remove_player()
    {
        Gamestate game;

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);
        CHECK(game.remove_player(0) == Gamestate::States::invalid_player_id);
        CHECK(game.remove_player(9) == Gamestate::States::player_not_found);
        CHECK(game.remove_player(1) == Gamestate::States::success);
        CHECK(game.player_count() == 1);
        CHECK(!game.contains(1));
        CHECK(game.contains(2));
        CHECK(game.remove_player(1) == Gamestate::States::player_not_found);
        return true;
    }

    bool test_move_player()
    {
        Gamestate game;
        PlayerGameState state{};

        CHECK(game.initialize({1}) == Gamestate::States::success);
        CHECK(game.move_player(0, 1, 0) == Gamestate::States::invalid_player_id);
        CHECK(game.move_player(9, 1, 0) == Gamestate::States::player_not_found);
        CHECK(game.move_player(1, 0, 0) == Gamestate::States::invalid_move);
        CHECK(game.move_player(1, 2, 0) == Gamestate::States::invalid_move);
        CHECK(game.move_player(1, 1, 1) == Gamestate::States::invalid_move);
        CHECK(game.move_player(1, -1, 0) == Gamestate::States::out_of_bounds);
        CHECK(game.move_player(1, 0, -1) == Gamestate::States::out_of_bounds);

        for (int i = 0; i < 100; ++i)
        {
            CHECK(game.move_player(1, 1, 0) == Gamestate::States::success);
        }

        CHECK(game.move_player(1, 1, 0) == Gamestate::States::out_of_bounds);

        for (int i = 0; i < 100; ++i)
        {
            CHECK(game.move_player(1, 0, 1) == Gamestate::States::success);
        }

        CHECK(game.move_player(1, 0, 1) == Gamestate::States::out_of_bounds);
        CHECK(game.player_state(1, state));
        CHECK(state.x == 100);
        CHECK(state.y == 100);
        CHECK(state.hp == 100);
        return true;
    }

    bool test_attack_invalid_requests()
    {
        Gamestate game;

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);
        std::vector<PlayerGameState> before = game.snapshot();

        Gamestate::Attackresult result = game.attack_player(0, 2);
        CHECK(result.status == Gamestate::States::invalid_player_id);
        CHECK(result.attacker_id == 0);
        CHECK(result.target_player_id == 2);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));

        result = game.attack_player(1, 0);
        CHECK(result.status == Gamestate::States::invalid_player_id);
        CHECK(result.attacker_id == 1);
        CHECK(result.target_player_id == 0);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));

        result = game.attack_player(1, 1);
        CHECK(result.status == Gamestate::States::invalid_target);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));

        result = game.attack_player(9, 2);
        CHECK(result.status == Gamestate::States::attacker_not_found);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));

        result = game.attack_player(1, 9);
        CHECK(result.status == Gamestate::States::target_not_found);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));
        return true;
    }

    bool test_attack_out_of_range()
    {
        Gamestate game;

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);
        CHECK(game.move_player(2, 1, 0) == Gamestate::States::success);

        std::vector<PlayerGameState> before = game.snapshot();
        Gamestate::Attackresult result = game.attack_player(1, 2);

        CHECK(result.status == Gamestate::States::target_out_of_range);
        CHECK(result.attacker_id == 1);
        CHECK(result.target_player_id == 2);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));
        return true;
    }

    bool test_successful_attack()
    {
        Gamestate game;
        PlayerGameState attacker{};
        PlayerGameState target{};

        CHECK(game.initialize({20, 10}) == Gamestate::States::success);

        Gamestate::Attackresult result = game.attack_player(10, 20);

        CHECK(result.status == Gamestate::States::success);
        CHECK(result.attacker_id == 10);
        CHECK(result.target_player_id == 20);
        CHECK(result.remaining_hp == 90);
        CHECK(!result.target_eliminated);

        CHECK(game.player_state(10, attacker));
        CHECK(game.player_state(20, target));
        CHECK(attacker.hp == 100);
        CHECK(target.hp == 90);
        CHECK(attacker.x == 0);
        CHECK(target.x == 1);
        return true;
    }

    bool test_repeated_attack_and_target_death()
    {
        Gamestate game;
        PlayerGameState target{};

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);

        for (int hit = 1; hit <= 9; ++hit)
        {
            Gamestate::Attackresult result = game.attack_player(1, 2);

            CHECK(result.status == Gamestate::States::success);
            CHECK(result.remaining_hp == 100 - hit * 10);
            CHECK(!result.target_eliminated);
        }

        Gamestate::Attackresult lethal_result = game.attack_player(1, 2);

        CHECK(lethal_result.status == Gamestate::States::success);
        CHECK(lethal_result.remaining_hp == 0);
        CHECK(lethal_result.target_eliminated);
        CHECK(game.player_state(2, target));
        CHECK(target.hp == 0);

        std::vector<PlayerGameState> after_death = game.snapshot();
        Gamestate::Attackresult repeated_result = game.attack_player(1, 2);

        CHECK(repeated_result.status == Gamestate::States::target_dead);
        CHECK(repeated_result.remaining_hp == 0);
        CHECK(!repeated_result.target_eliminated);
        CHECK(same_snapshot(after_death, game.snapshot()));
        return true;
    }

    bool test_dead_attacker_rejected()
    {
        Gamestate game;
        PlayerGameState target{};

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);

        for (int hit = 0; hit < 10; ++hit)
        {
            Gamestate::Attackresult result = game.attack_player(2, 1);
            CHECK(result.status == Gamestate::States::success);
        }

        std::vector<PlayerGameState> before = game.snapshot();
        Gamestate::Attackresult result = game.attack_player(1, 2);

        CHECK(result.status == Gamestate::States::attacker_dead);
        CHECK(result.remaining_hp == 0);
        CHECK(!result.target_eliminated);
        CHECK(same_snapshot(before, game.snapshot()));

        CHECK(game.player_state(2, target));
        CHECK(target.hp == 100);
        return true;
    }

    bool test_same_position_attack()
    {
        Gamestate game;

        CHECK(game.initialize({1, 2}) == Gamestate::States::success);
        CHECK(game.move_player(2, -1, 0) == Gamestate::States::success);

        Gamestate::Attackresult result = game.attack_player(1, 2);

        CHECK(result.status == Gamestate::States::success);
        CHECK(result.remaining_hp == 90);
        CHECK(!result.target_eliminated);
        return true;
    }

    bool test_repeated_construct_destroy()
    {
        for (int round = 0; round < 100; ++round)
        {
            Gamestate game;

            CHECK(game.initialize({1, 2}) == Gamestate::States::success);
            CHECK(game.attack_player(1, 2).status == Gamestate::States::success);
            CHECK(game.player_count() == 2);
        }
        return true;
    }

    bool run_test(const char *name, bool (*test)())
    {
        if (!test())
        {
            std::cerr << "[FAIL] " << name << '\n';
            return false;
        }

        std::cout << "[PASS] " << name << '\n';
        return true;
    }
}

int main()
{
    int failed = 0;

    failed += !run_test("initial_state", test_initial_state);
    failed += !run_test("initialize_validation_and_snapshot", test_initialize_validation_and_snapshot);
    failed += !run_test("remove_player", test_remove_player);
    failed += !run_test("move_player", test_move_player);
    failed += !run_test("attack_invalid_requests", test_attack_invalid_requests);
    failed += !run_test("attack_out_of_range", test_attack_out_of_range);
    failed += !run_test("successful_attack", test_successful_attack);
    failed += !run_test("repeated_attack_and_target_death", test_repeated_attack_and_target_death);
    failed += !run_test("dead_attacker_rejected", test_dead_attacker_rejected);
    failed += !run_test("same_position_attack", test_same_position_attack);
    failed += !run_test("repeated_construct_destroy", test_repeated_construct_destroy);

    if (failed != 0)
    {
        std::cerr << failed << " 个 GameState 测试失败\n";
        return 1;
    }

    std::cout << "GameState 全量验收通过\n";
    return 0;
}
