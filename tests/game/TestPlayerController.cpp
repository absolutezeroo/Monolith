#include "voxel/game/PlayerController.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace voxel;
using namespace voxel::game;
using namespace voxel::world;
using Catch::Matchers::WithinAbs;

namespace
{

/// Helper: set up a BlockRegistry with air (0) and stone (1).
BlockRegistry makeTestRegistry()
{
    BlockRegistry registry;
    // Air is registered by default at index 0

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.hasCollision = true;
    auto result = registry.registerBlock(std::move(stone));
    REQUIRE(result.has_value());

    return registry;
}

/// Helper: create a ChunkManager with a flat stone floor at groundY.
/// Fills blocks at groundY and two layers below with stone across a 5x5 chunk grid.
void setupFlatGround(ChunkManager& cm, const BlockRegistry& registry, int groundY = 64)
{
    // Load a grid of chunks around origin
    for (int cx = -2; cx <= 2; ++cx)
    {
        for (int cz = -2; cz <= 2; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    // Fill blocks at groundY with stone (stateId 1)
    uint16_t stoneId = registry.getIdByName("base:stone");
    for (int cx = -2; cx <= 2; ++cx)
    {
        for (int cz = -2; cz <= 2; ++cz)
        {
            for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
            {
                for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
                {
                    int wx = cx * ChunkSection::SIZE + lx;
                    int wz = cz * ChunkSection::SIZE + lz;
                    for (int y = groundY; y >= groundY - 2; --y)
                    {
                        cm.setBlock(glm::ivec3{wx, y, wz}, stoneId);
                    }
                }
            }
        }
    }
}

} // namespace

TEST_CASE("PlayerController: AABB construction is correct", "[game][physics]")
{
    PlayerController player;
    player.setPosition(glm::dvec3{10.0, 65.0, 20.0});

    auto aabb = player.getAABB();
    CHECK_THAT(static_cast<double>(aabb.min.x), WithinAbs(9.7, 0.01));
    CHECK_THAT(static_cast<double>(aabb.min.y), WithinAbs(65.0, 0.01));
    CHECK_THAT(static_cast<double>(aabb.min.z), WithinAbs(19.7, 0.01));
    CHECK_THAT(static_cast<double>(aabb.max.x), WithinAbs(10.3, 0.01));
    CHECK_THAT(static_cast<double>(aabb.max.y), WithinAbs(66.8, 0.01));
    CHECK_THAT(static_cast<double>(aabb.max.z), WithinAbs(20.3, 0.01));
}

TEST_CASE("PlayerController: eye position is feet + EYE_HEIGHT", "[game][physics]")
{
    PlayerController player;
    player.setPosition(glm::dvec3{5.0, 65.0, 5.0});

    auto eye = player.getEyePosition();
    CHECK_THAT(eye.x, WithinAbs(5.0, 0.01));
    CHECK_THAT(eye.y, WithinAbs(65.0 + 1.62, 0.01));
    CHECK_THAT(eye.z, WithinAbs(5.0, 0.01));
}

TEST_CASE("PlayerController: falling onto flat ground stops", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    // Start above ground (ground top = 65.0, since stone is at y=64 occupying [64,65))
    player.init(glm::dvec3{8.0, 70.0, 8.0}, cm, registry);

    // Simulate ticks: no horizontal movement, just gravity
    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};

    for (int i = 0; i < 200; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);

        if (player.isOnGround())
        {
            break;
        }
    }

    REQUIRE(player.isOnGround());
    CHECK(player.getVelocity().y == 0.0f);
    // Player feet should be at ground top (y=65) with epsilon tolerance
    CHECK(player.getPosition().y >= 64.99);
    CHECK(player.getPosition().y < 65.1);
}

TEST_CASE("PlayerController: walking into solid wall stops", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Build a wall at x=12, from y=65..67
    for (int y = 65; y <= 67; ++y)
    {
        for (int z = 0; z < 16; ++z)
        {
            cm.setBlock(glm::ivec3{12, y, z}, stoneId);
        }
    }

    PlayerController player;
    // Start on the ground at x=10
    player.init(glm::dvec3{10.0, 65.0, 8.0}, cm, registry);

    // First, let the player settle on the ground
    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};
    for (int i = 0; i < 10; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
    }
    REQUIRE(player.isOnGround());

    // Now walk toward the wall in +X direction
    constexpr glm::vec3 walkRight{1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 100; ++i)
    {
        player.tickPhysics(dt, walkRight, cm, registry);
    }

    // Player right edge (pos.x + 0.3) should not penetrate wall at x=12
    double playerRightEdge = player.getPosition().x + 0.3;
    CHECK(playerRightEdge <= 12.01);
    CHECK(playerRightEdge >= 11.5); // Should be close to the wall
}

TEST_CASE("PlayerController: step-up onto 1-block ledge while moving horizontally", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Place an elevated ledge starting at x=12, y=65 (one block above ground at y=64)
    // Extends from x=12 to x=20 so the player has floor to stand on after stepping up
    for (int x = 12; x <= 20; ++x)
    {
        for (int z = 5; z <= 11; ++z)
        {
            cm.setBlock(glm::ivec3{x, 65, z}, stoneId);
        }
    }

    PlayerController player;
    player.init(glm::dvec3{10.0, 65.0, 8.0}, cm, registry);

    // Settle on ground first
    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};
    for (int i = 0; i < 10; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
    }
    REQUIRE(player.isOnGround());

    double startY = player.getPosition().y;

    // Walk toward the step — stop after stepping up and being on the ledge
    constexpr glm::vec3 walkRight{1.0f, 0.0f, 0.0f};
    bool steppedUp = false;
    double highestY = startY;
    for (int i = 0; i < 100; ++i)
    {
        player.tickPhysics(dt, walkRight, cm, registry);
        if (player.getPosition().y > highestY)
        {
            highestY = player.getPosition().y;
        }
        // Stop when the player is on the ledge and past the step
        if (player.getPosition().y > startY + 0.5 && player.getPosition().x > 13.0)
        {
            steppedUp = true;
            break;
        }
    }

    // Player should have stepped up — Y should be at the ledge top (y=66)
    CHECK(steppedUp);
    CHECK(highestY >= startY + 0.9);
    // Player should have moved past x=12
    CHECK(player.getPosition().x > 12.0);
}

TEST_CASE("PlayerController: corner collision — two walls", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Build an L-shaped wall:
    // Wall along X at z=12
    for (int x = 10; x <= 15; ++x)
    {
        for (int y = 65; y <= 67; ++y)
        {
            cm.setBlock(glm::ivec3{x, y, 12}, stoneId);
        }
    }
    // Wall along Z at x=15
    for (int z = 5; z <= 12; ++z)
    {
        for (int y = 65; y <= 67; ++y)
        {
            cm.setBlock(glm::ivec3{15, y, z}, stoneId);
        }
    }

    PlayerController player;
    player.init(glm::dvec3{13.0, 65.0, 10.0}, cm, registry);

    // Settle on ground
    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};
    for (int i = 0; i < 10; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
    }
    REQUIRE(player.isOnGround());

    // Walk diagonally into the corner (+X, +Z)
    glm::vec3 diagonal = glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f});
    for (int i = 0; i < 100; ++i)
    {
        player.tickPhysics(dt, diagonal, cm, registry);

        // Player must not clip through either wall
        double rightEdge = player.getPosition().x + 0.3;
        double frontEdge = player.getPosition().z + 0.3;
        CHECK(rightEdge <= 15.01);
        CHECK(frontEdge <= 12.01);
    }
}

TEST_CASE("PlayerController: spawn inside block pushes to valid position", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Fill blocks at y=65 and y=66 to trap the player
    for (int x = 7; x <= 9; ++x)
    {
        for (int z = 7; z <= 9; ++z)
        {
            cm.setBlock(glm::ivec3{x, 65, z}, stoneId);
            cm.setBlock(glm::ivec3{x, 66, z}, stoneId);
        }
    }

    PlayerController player;
    // Spawn inside the solid block at y=65
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    // Player should have been pushed above the obstruction (y >= 67)
    CHECK(player.getPosition().y >= 67.0);
}

TEST_CASE("PlayerController: gravity accelerates downward", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    // No ground — just empty chunks
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    PlayerController player;
    player.setPosition(glm::dvec3{8.0, 100.0, 8.0});

    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};

    // One tick of gravity
    player.tickPhysics(dt, noMove, cm, registry);

    // Velocity should be negative (falling)
    CHECK(player.getVelocity().y < 0.0f);
    // Position should have moved down
    CHECK(player.getPosition().y < 100.0);

    // Several more ticks — velocity magnitude should increase (acceleration)
    float prevVelY = player.getVelocity().y;
    player.tickPhysics(dt, noMove, cm, registry);
    CHECK(player.getVelocity().y < prevVelY);
}

TEST_CASE("PlayerController: terminal velocity is capped", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    PlayerController player;
    player.setPosition(glm::dvec3{8.0, 10000.0, 8.0});

    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};

    // Simulate many ticks to reach terminal velocity
    for (int i = 0; i < 500; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
    }

    // Velocity should be capped at -TERMINAL_VELOCITY
    CHECK(player.getVelocity().y >= -PlayerController::TERMINAL_VELOCITY);
    CHECK(player.getVelocity().y <= -PlayerController::TERMINAL_VELOCITY + 2.0f);
}

TEST_CASE("PlayerController: step-up works when player straddles block boundary", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Place a step-up ledge at x=12, z=5 only (not z=4).
    // Player at z=4.8 spans [4.5, 5.1], straddling blocks z=4 and z=5.
    // The obstacle is only in z=5 — step-up must check the full footprint to detect it.
    for (int x = 12; x <= 20; ++x)
    {
        cm.setBlock(glm::ivec3{x, 65, 5}, stoneId);
    }

    PlayerController player;
    player.init(glm::dvec3{10.0, 65.0, 4.8}, cm, registry);

    // Settle on ground
    constexpr float dt = 0.05f;
    constexpr glm::vec3 noMove{0.0f};
    for (int i = 0; i < 10; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
    }
    REQUIRE(player.isOnGround());

    double startY = player.getPosition().y;

    // Walk toward the step in +X
    constexpr glm::vec3 walkRight{1.0f, 0.0f, 0.0f};
    bool steppedUp = false;
    for (int i = 0; i < 100; ++i)
    {
        player.tickPhysics(dt, walkRight, cm, registry);
        if (player.getPosition().y > startY + 0.5 && player.getPosition().x > 13.0)
        {
            steppedUp = true;
            break;
        }
    }

    CHECK(steppedUp);
    CHECK(player.getPosition().x > 12.0);
}

TEST_CASE("PlayerController: spawn safety with fractional Y detects head-level block", "[game][physics]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Place a solid block at y=67. Player at y=65.3 spans [65.3, 67.1],
    // overlapping blocks 65, 66, 67. Block at 67 should be detected.
    for (int x = 7; x <= 9; ++x)
    {
        for (int z = 7; z <= 9; ++z)
        {
            cm.setBlock(glm::ivec3{x, 67, z}, stoneId);
        }
    }

    PlayerController player;
    // Spawn at fractional Y — head extends into the solid block at y=67
    player.init(glm::dvec3{8.0, 65.3, 8.0}, cm, registry);

    // Player should have been pushed above the obstruction (y >= 68)
    CHECK(player.getPosition().y >= 68.0);
}
