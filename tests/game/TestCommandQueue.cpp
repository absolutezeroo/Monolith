#include "voxel/game/CommandQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace voxel::game;

TEST_CASE("CommandQueue: push and tryPop return commands in FIFO order", "[game][command]")
{
    CommandQueue queue;

    for (voxel::core::uint32 i = 0; i < 5; ++i)
    {
        queue.push(GameCommand{CommandType::Jump, i, i * 10, JumpPayload{}});
    }

    for (voxel::core::uint32 i = 0; i < 5; ++i)
    {
        auto cmd = queue.tryPop();
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->playerId == i);
        REQUIRE(cmd->tick == i * 10);
        REQUIRE(cmd->type == CommandType::Jump);
    }
}

TEST_CASE("CommandQueue: tryPop on empty queue returns nullopt", "[game][command]")
{
    CommandQueue queue;

    auto cmd = queue.tryPop();
    REQUIRE_FALSE(cmd.has_value());
}

TEST_CASE("CommandQueue: drain consumes all queued commands", "[game][command]")
{
    CommandQueue queue;

    queue.push(GameCommand{CommandType::PlaceBlock, 1, 0, PlaceBlockPayload{{1, 2, 3}, 42}});
    queue.push(GameCommand{CommandType::BreakBlock, 1, 1, BreakBlockPayload{{4, 5, 6}}});
    queue.push(GameCommand{CommandType::Jump, 1, 2, JumpPayload{}});

    REQUIRE(queue.size() == 3);

    std::vector<CommandType> types;
    queue.drain([&types](GameCommand cmd) { types.push_back(cmd.type); });

    REQUIRE(types.size() == 3);
    REQUIRE(types[0] == CommandType::PlaceBlock);
    REQUIRE(types[1] == CommandType::BreakBlock);
    REQUIRE(types[2] == CommandType::Jump);
    REQUIRE(queue.empty());
}

TEST_CASE("CommandQueue: variant payload preserves data", "[game][command]")
{
    CommandQueue queue;

    SECTION("PlaceBlock payload")
    {
        queue.push(GameCommand{CommandType::PlaceBlock, 7, 100, PlaceBlockPayload{{10, 20, 30}, 99}});

        auto cmd = queue.tryPop();
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == CommandType::PlaceBlock);

        auto& payload = std::get<PlaceBlockPayload>(cmd->payload);
        REQUIRE(payload.position.x == 10);
        REQUIRE(payload.position.y == 20);
        REQUIRE(payload.position.z == 30);
        REQUIRE(payload.blockId == 99);
    }

    SECTION("MovePlayer payload")
    {
        queue.push(
            GameCommand{CommandType::MovePlayer, 1, 50, MovePlayerPayload{{0.0f, 0.0f, 1.0f}, true, false}});

        auto cmd = queue.tryPop();
        REQUIRE(cmd.has_value());

        auto& payload = std::get<MovePlayerPayload>(cmd->payload);
        REQUIRE(payload.direction.z == 1.0f);
        REQUIRE(payload.isSprinting == true);
        REQUIRE(payload.isSneaking == false);
    }

    SECTION("ToggleSprint payload")
    {
        queue.push(GameCommand{CommandType::ToggleSprint, 2, 75, ToggleSprintPayload{true}});

        auto cmd = queue.tryPop();
        REQUIRE(cmd.has_value());

        auto& payload = std::get<ToggleSprintPayload>(cmd->payload);
        REQUIRE(payload.enabled == true);
    }
}

TEST_CASE("CommandQueue: size and empty reflect state", "[game][command]")
{
    CommandQueue queue;

    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);

    queue.push(GameCommand{CommandType::Jump, 1, 0, JumpPayload{}});
    REQUIRE_FALSE(queue.empty());
    REQUIRE(queue.size() == 1);

    queue.push(GameCommand{CommandType::Jump, 1, 1, JumpPayload{}});
    REQUIRE(queue.size() == 2);

    auto cmd = queue.tryPop();
    REQUIRE(cmd.has_value());
    REQUIRE(queue.size() == 1);

    cmd = queue.tryPop();
    REQUIRE(cmd.has_value());
    REQUIRE(queue.empty());
}
