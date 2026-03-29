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

/// Helper: set up a BlockRegistry with air (0), stone (1), ladder (2), cobweb (3), cactus (4).
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

    BlockDefinition ladder;
    ladder.stringId = "base:ladder";
    ladder.hasCollision = false;
    ladder.isClimbable = true;
    auto ladderResult = registry.registerBlock(std::move(ladder));
    REQUIRE(ladderResult.has_value());

    BlockDefinition cobweb;
    cobweb.stringId = "base:cobweb";
    cobweb.hasCollision = false;
    cobweb.moveResistance = 7;
    auto cobwebResult = registry.registerBlock(std::move(cobweb));
    REQUIRE(cobwebResult.has_value());

    BlockDefinition cactus;
    cactus.stringId = "base:cactus";
    cactus.hasCollision = true;
    cactus.damagePerSecond = 1;
    auto cactusResult = registry.registerBlock(std::move(cactus));
    REQUIRE(cactusResult.has_value());

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

/// Helper: settle player on ground by running ticks with no movement.
void settleOnGround(PlayerController& player, ChunkManager& cm, const BlockRegistry& registry, int maxTicks = 20)
{
    constexpr float dt = 0.05f;
    MovementInput noMove;
    for (int i = 0; i < maxTicks; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
        if (player.isOnGround())
        {
            break;
        }
    }
}

} // namespace

// ===== Existing Story 7.2 tests (updated to use MovementInput) =====

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
    MovementInput noMove;

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
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Now walk toward the wall in +X direction
    MovementInput walkRight;
    walkRight.wishDir = {1.0f, 0.0f, 0.0f};
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
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    double startY = player.getPosition().y;

    // Walk toward the step — stop after stepping up and being on the ledge
    MovementInput walkRight;
    walkRight.wishDir = {1.0f, 0.0f, 0.0f};
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
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Walk diagonally into the corner (+X, +Z)
    MovementInput diagonal;
    diagonal.wishDir = glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f});
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
    MovementInput noMove;

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
    MovementInput noMove;

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
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    double startY = player.getPosition().y;

    // Walk toward the step in +X
    MovementInput walkRight;
    walkRight.wishDir = {1.0f, 0.0f, 0.0f};
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

// ===== Story 7.3 new tests =====

TEST_CASE("PlayerController: jump from ground sets upward velocity, then falls back", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    double groundY = player.getPosition().y;

    // Jump
    MovementInput jumpInput;
    jumpInput.jump = true;
    player.tickPhysics(dt, jumpInput, cm, registry);

    // Velocity should be positive (upward) after jump
    CHECK_THAT(static_cast<double>(player.getVelocity().y),
               WithinAbs(PlayerController::JUMP_VELOCITY - PlayerController::GRAVITY * dt, 0.5));
    CHECK_FALSE(player.isOnGround());

    // Simulate until player lands back
    MovementInput noMove;
    for (int i = 0; i < 200; ++i)
    {
        player.tickPhysics(dt, noMove, cm, registry);
        if (player.isOnGround())
        {
            break;
        }
    }

    REQUIRE(player.isOnGround());
    CHECK_THAT(player.getPosition().y, WithinAbs(groundY, 0.1));
}

TEST_CASE("PlayerController: jump while airborne has no effect", "[game][physics][movement]")
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
    player.setPosition(glm::dvec3{8.0, 100.0, 8.0});

    constexpr float dt = 0.05f;

    // First tick — gravity starts, player is airborne
    MovementInput noMove;
    player.tickPhysics(dt, noMove, cm, registry);
    CHECK_FALSE(player.isOnGround());

    float velYBeforeJump = player.getVelocity().y;

    // Try to jump while airborne
    MovementInput jumpInput;
    jumpInput.jump = true;
    player.tickPhysics(dt, jumpInput, cm, registry);

    // Velocity should NOT have been set to JUMP_VELOCITY — should continue falling
    // (it should be more negative than before, not positive)
    CHECK(player.getVelocity().y < velYBeforeJump);
}

TEST_CASE("PlayerController: sprint speed is WALK_SPEED * 1.3", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Sprint in +X direction
    MovementInput sprintInput;
    sprintInput.wishDir = {1.0f, 0.0f, 0.0f};
    sprintInput.sprint = true;
    player.tickPhysics(dt, sprintInput, cm, registry);

    CHECK_THAT(static_cast<double>(player.getVelocity().x),
               WithinAbs(PlayerController::SPRINT_SPEED, 0.01));
}

TEST_CASE("PlayerController: sneak speed is WALK_SPEED * 0.3", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Sneak in +X direction
    MovementInput sneakInput;
    sneakInput.wishDir = {1.0f, 0.0f, 0.0f};
    sneakInput.sneak = true;
    player.tickPhysics(dt, sneakInput, cm, registry);

    CHECK_THAT(static_cast<double>(player.getVelocity().x),
               WithinAbs(PlayerController::SNEAK_SPEED, 0.01));
}

TEST_CASE("PlayerController: sneak edge detection stops player at edge", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    // Create a platform with a clear edge: remove ground beyond x=10
    for (int x = 11; x <= 20; ++x)
    {
        for (int z = 5; z <= 11; ++z)
        {
            for (int y = 62; y <= 64; ++y)
            {
                cm.setBlock(glm::ivec3{x, y, z}, BLOCK_AIR);
            }
        }
    }

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Walk (sneaking) toward the edge in +X
    MovementInput sneakWalk;
    sneakWalk.wishDir = {1.0f, 0.0f, 0.0f};
    sneakWalk.sneak = true;

    for (int i = 0; i < 200; ++i)
    {
        player.tickPhysics(dt, sneakWalk, cm, registry);
    }

    // Player should still be on ground (not fallen off)
    CHECK(player.isOnGround());
    // Minecraft-style: player can overhang the edge up to ~0.3 blocks (half player width).
    // The left corner (x - 0.3) must still be above ground (block x <= 10).
    // So max player x = 11.0 + 0.3 - epsilon ≈ 11.29 (left corner at 10.99 → block 10 → ground)
    // Player center should NOT go past ~11.3 (where left corner would be at 11.0 → block 11 → void)
    CHECK(player.getPosition().x <= 11.35);
    // But player SHOULD overhang past the platform edge (x > 10.7 means right corner past 11.0)
    CHECK(player.getPosition().x > 10.7);
}

TEST_CASE("PlayerController: sneaking across flat ground moves freely (no invisible walls)", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    double startX = player.getPosition().x;

    // Sneak in +X across multiple blocks of flat ground
    MovementInput sneakWalk;
    sneakWalk.wishDir = {1.0f, 0.0f, 0.0f};
    sneakWalk.sneak = true;

    for (int i = 0; i < 100; ++i)
    {
        player.tickPhysics(dt, sneakWalk, cm, registry);
    }

    // Player should have moved significantly (sneak speed 1.295 m/s * 5s = ~6.5 blocks)
    double distanceMoved = player.getPosition().x - startX;
    CHECK(distanceMoved > 5.0);
    CHECK(player.isOnGround());
}

TEST_CASE("PlayerController: sneaking at block boundary transitions smoothly", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    // Place player right at the boundary between blocks x=10 and x=11
    // Right corner at x = 10.7 + 0.3 = 11.0 — exactly on the seam
    PlayerController player;
    player.init(glm::dvec3{10.7, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    double startX = player.getPosition().x;

    // Sneak in +X — should cross the block boundary without getting stuck
    MovementInput sneakWalk;
    sneakWalk.wishDir = {1.0f, 0.0f, 0.0f};
    sneakWalk.sneak = true;

    for (int i = 0; i < 40; ++i)
    {
        player.tickPhysics(dt, sneakWalk, cm, registry);
    }

    // Player should have moved past the block boundary (2 seconds of sneak = ~2.6 blocks)
    CHECK(player.getPosition().x > startX + 2.0);
    CHECK(player.isOnGround());
}

TEST_CASE("PlayerController: sneaking near chunk boundary (x=0) crosses smoothly", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    // Start near chunk boundary (chunk -1 ends at x=-1, chunk 0 starts at x=0)
    PlayerController player;
    player.init(glm::dvec3{-0.5, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Sneak in +X across the chunk boundary
    MovementInput sneakWalk;
    sneakWalk.wishDir = {1.0f, 0.0f, 0.0f};
    sneakWalk.sneak = true;

    for (int i = 0; i < 40; ++i)
    {
        player.tickPhysics(dt, sneakWalk, cm, registry);
    }

    // Player should cross x=0 chunk boundary without getting stuck
    CHECK(player.getPosition().x > 1.0);
    CHECK(player.isOnGround());
}

TEST_CASE("PlayerController: sneaking onto 1-block step up works", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Create a 1-block step at x=12: ground rises from y=64 to y=65
    for (int x = 12; x <= 20; ++x)
    {
        for (int z = 5; z <= 11; ++z)
        {
            cm.setBlock(glm::ivec3{x, 65, z}, stoneId);
        }
    }

    PlayerController player;
    player.init(glm::dvec3{10.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Sneak toward the step in +X — step-up should work while sneaking
    MovementInput sneakWalk;
    sneakWalk.wishDir = {1.0f, 0.0f, 0.0f};
    sneakWalk.sneak = true;

    for (int i = 0; i < 200; ++i)
    {
        player.tickPhysics(dt, sneakWalk, cm, registry);
    }

    // Player should have stepped up and moved past x=12
    CHECK(player.getPosition().x > 12.0);
    CHECK(player.isOnGround());
}

TEST_CASE("PlayerController: climbable block disables gravity and allows vertical movement", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t ladderId = registry.getIdByName("base:ladder");

    // Place ladder blocks from y=65 to y=70
    for (int y = 65; y <= 70; ++y)
    {
        cm.setBlock(glm::ivec3{8, y, 8}, ladderId);
    }

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);

    // Move into the climbable block area and climb up (Space = jump in input → climb up)
    MovementInput climbUp;
    climbUp.jump = true; // Space key = climb up in climbable context
    player.tickPhysics(dt, climbUp, cm, registry);

    CHECK(player.isInClimbable());
    // Velocity should be positive (upward) at SNEAK_SPEED
    CHECK_THAT(static_cast<double>(player.getVelocity().y),
               WithinAbs(PlayerController::SNEAK_SPEED, 0.01));

    // Climb down (Shift = sneak → climb down)
    MovementInput climbDown;
    climbDown.sneak = true;
    player.tickPhysics(dt, climbDown, cm, registry);

    CHECK_THAT(static_cast<double>(player.getVelocity().y),
               WithinAbs(-PlayerController::SNEAK_SPEED, 0.01));

    // No keys → stay in place (zero velocity)
    MovementInput noKeys;
    player.tickPhysics(dt, noKeys, cm, registry);

    CHECK_THAT(static_cast<double>(player.getVelocity().y), WithinAbs(0.0, 0.01));
    CHECK_THAT(static_cast<double>(player.getVelocity().x), WithinAbs(0.0, 0.01));
    CHECK_THAT(static_cast<double>(player.getVelocity().z), WithinAbs(0.0, 0.01));
}

TEST_CASE("PlayerController: move resistance reduces speed", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t cobwebId = registry.getIdByName("base:cobweb");

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);
    REQUIRE(player.isOnGround());

    // Place cobweb blocks at player position (after settling, so scanOverlappingBlocks picks them up)
    for (int y = 65; y <= 66; ++y)
    {
        cm.setBlock(glm::ivec3{8, y, 8}, cobwebId);
    }

    // Walk in +X through cobweb (resistance = 7)
    MovementInput walkRight;
    walkRight.wishDir = {1.0f, 0.0f, 0.0f};
    player.tickPhysics(dt, walkRight, cm, registry);

    // Expected speed = WALK_SPEED / (1 + 7) = WALK_SPEED / 8
    float expectedSpeed = PlayerController::WALK_SPEED / 8.0f;
    CHECK_THAT(static_cast<double>(std::abs(player.getVelocity().x)),
               WithinAbs(expectedSpeed, 0.05));
}

TEST_CASE("PlayerController: air control reduces horizontal acceleration while airborne", "[game][physics][movement]")
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
    player.setPosition(glm::dvec3{8.0, 100.0, 8.0});

    constexpr float dt = 0.05f;

    // First tick to start falling (establish airborne state)
    MovementInput noMove;
    player.tickPhysics(dt, noMove, cm, registry);
    CHECK_FALSE(player.isOnGround());

    // Try to move in +X while airborne
    MovementInput moveRight;
    moveRight.wishDir = {1.0f, 0.0f, 0.0f};
    player.tickPhysics(dt, moveRight, cm, registry);

    // Air control: velocity.x should be much less than WALK_SPEED
    // After one tick: vel.x = 0 + (WALK_SPEED - 0) * AIR_CONTROL = WALK_SPEED * 0.02
    float expectedVelX = PlayerController::WALK_SPEED * PlayerController::AIR_CONTROL;
    CHECK_THAT(static_cast<double>(player.getVelocity().x),
               WithinAbs(expectedVelX, 0.01));

    // Compare with ground speed for reference
    CHECK(std::abs(player.getVelocity().x) < PlayerController::WALK_SPEED * 0.1f);
}

TEST_CASE("PlayerController: leaving climbable block resumes gravity", "[game][physics][movement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    uint16_t ladderId = registry.getIdByName("base:ladder");

    // Place ladder at a single position
    cm.setBlock(glm::ivec3{8, 65, 8}, ladderId);
    cm.setBlock(glm::ivec3{8, 66, 8}, ladderId);

    PlayerController player;
    player.init(glm::dvec3{8.0, 65.0, 8.0}, cm, registry);

    constexpr float dt = 0.05f;
    settleOnGround(player, cm, registry);

    // Verify we're in a climbable block
    MovementInput noKeys;
    player.tickPhysics(dt, noKeys, cm, registry);
    REQUIRE(player.isInClimbable());

    // Move out of the climbable block by walking in +X
    MovementInput walkRight;
    walkRight.wishDir = {1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 20; ++i)
    {
        player.tickPhysics(dt, walkRight, cm, registry);
    }

    // After walking 20 ticks at WALK_SPEED (~4.3 blocks), player is well past the ladder
    REQUIRE(player.getPosition().x > 9.3);
    CHECK_FALSE(player.isInClimbable());
}
