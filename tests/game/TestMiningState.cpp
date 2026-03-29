#include "voxel/game/MiningState.h"
#include "voxel/game/PlayerController.h"
#include "voxel/physics/Raycast.h"
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

/// Helper: set up a BlockRegistry with air (0), stone (1), dirt (2), glass (3=isBuildableTo).
BlockRegistry makeTestRegistry()
{
    BlockRegistry registry;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.hasCollision = true;
    stone.hardness = 1.5f;
    auto result = registry.registerBlock(std::move(stone));
    REQUIRE(result.has_value());

    BlockDefinition dirt;
    dirt.stringId = "base:dirt";
    dirt.isSolid = true;
    dirt.hasCollision = true;
    dirt.hardness = 0.5f;
    auto dirtResult = registry.registerBlock(std::move(dirt));
    REQUIRE(dirtResult.has_value());

    BlockDefinition tallGrass;
    tallGrass.stringId = "base:tall_grass";
    tallGrass.isSolid = false;
    tallGrass.hasCollision = false;
    tallGrass.isBuildableTo = true;
    tallGrass.hardness = 0.0f;
    auto grassResult = registry.registerBlock(std::move(tallGrass));
    REQUIRE(grassResult.has_value());

    return registry;
}

/// Helper: create a ChunkManager with a flat stone floor at groundY.
void setupFlatGround(ChunkManager& cm, const BlockRegistry& registry, int groundY = 64)
{
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t stoneId = registry.getIdByName("base:stone");
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
            {
                for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
                {
                    int wx = cx * ChunkSection::SIZE + lx;
                    int wz = cz * ChunkSection::SIZE + lz;
                    cm.setBlock(glm::ivec3{wx, groundY, wz}, stoneId);
                }
            }
        }
    }
}

/// Helper: make a RaycastResult pointing at a specific block.
physics::RaycastResult makeHit(const glm::ivec3& blockPos, float distance = 3.0f)
{
    physics::RaycastResult r;
    r.hit = true;
    r.blockPos = blockPos;
    r.previousPos = blockPos + glm::ivec3{0, 1, 0};
    r.face = renderer::BlockFace::PosY;
    r.distance = distance;
    return r;
}

} // namespace

TEST_CASE("calculateBreakTime", "[mining]")
{
    SECTION("returns hardness * 1.5")
    {
        BlockDefinition def;
        def.hardness = 2.0f;
        CHECK_THAT(calculateBreakTime(def), WithinAbs(3.0, 0.001));
    }

    SECTION("returns hardness * 1.5 for low hardness")
    {
        BlockDefinition def;
        def.hardness = 0.5f;
        CHECK_THAT(calculateBreakTime(def), WithinAbs(0.75, 0.001));
    }

    SECTION("minimum break time is 0.05s")
    {
        BlockDefinition def;
        def.hardness = 0.0f;
        CHECK_THAT(calculateBreakTime(def), WithinAbs(MIN_BREAK_TIME, 0.001));
    }

    SECTION("very small hardness still respects minimum")
    {
        BlockDefinition def;
        def.hardness = 0.01f;
        CHECK(calculateBreakTime(def) >= MIN_BREAK_TIME);
    }
}

TEST_CASE("MiningState reset", "[mining]")
{
    MiningState state;
    state.targetBlock = glm::ivec3{1, 2, 3};
    state.progress = 0.5f;
    state.breakTime = 3.0f;
    state.crackStage = 5;
    state.isMining = true;

    state.reset();

    CHECK(state.targetBlock == glm::ivec3{0});
    CHECK(state.progress == 0.0f);
    CHECK(state.breakTime == 0.0f);
    CHECK(state.crackStage == -1);
    CHECK_FALSE(state.isMining);
}

TEST_CASE("PlayerController::updateMining - progress accumulation", "[mining]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.setPosition(glm::dvec3{0.0, 67.0, 0.0}); // Above the ground

    // Stone at (0, 64, 0) has hardness 1.5 → breakTime = 1.5 * 1.5 = 2.25s
    auto hit = makeHit(glm::ivec3{0, 64, 0}, 3.0f);
    constexpr float dt = 0.05f; // 20Hz tick

    SECTION("progress accumulates correctly over multiple ticks")
    {
        // First tick: start mining
        bool completed = player.updateMining(dt, hit, true, cm, registry);
        CHECK_FALSE(completed);
        CHECK(player.getMiningState().isMining);
        CHECK_THAT(player.getMiningState().progress, WithinAbs(dt / 2.25, 0.001));

        // Second tick: progress continues
        completed = player.updateMining(dt, hit, true, cm, registry);
        CHECK_FALSE(completed);
        CHECK_THAT(player.getMiningState().progress, WithinAbs(2.0 * dt / 2.25, 0.001));
    }

    SECTION("progress resets when LMB released")
    {
        // Start mining
        player.updateMining(dt, hit, true, cm, registry);
        CHECK(player.getMiningState().isMining);

        // Release LMB
        player.updateMining(dt, hit, false, cm, registry);
        CHECK_FALSE(player.getMiningState().isMining);
        CHECK(player.getMiningState().progress == 0.0f);
        CHECK(player.getMiningState().crackStage == -1);
    }

    SECTION("progress resets when target block changes")
    {
        // Start mining block at (0, 64, 0)
        player.updateMining(dt, hit, true, cm, registry);
        CHECK(player.getMiningState().isMining);
        float prevProgress = player.getMiningState().progress;
        CHECK(prevProgress > 0.0f);

        // Change target to different block
        auto hit2 = makeHit(glm::ivec3{1, 64, 0}, 3.0f);
        player.updateMining(dt, hit2, true, cm, registry);

        // Progress should have reset and restarted from 0
        CHECK(player.getMiningState().targetBlock == glm::ivec3{1, 64, 0});
        CHECK_THAT(player.getMiningState().progress, WithinAbs(dt / 2.25, 0.001));
    }

    SECTION("progress resets when no raycast hit")
    {
        // Start mining
        player.updateMining(dt, hit, true, cm, registry);
        CHECK(player.getMiningState().isMining);

        // No hit
        physics::RaycastResult noHit;
        noHit.hit = false;
        player.updateMining(dt, noHit, true, cm, registry);
        CHECK_FALSE(player.getMiningState().isMining);
        CHECK(player.getMiningState().progress == 0.0f);
    }

    SECTION("mining completes when progress reaches 1.0")
    {
        // Stone: breakTime = 2.25s. Need 2.25 / 0.05 = 45 ticks
        bool completed = false;
        for (int i = 0; i < 100; ++i)
        {
            completed = player.updateMining(dt, hit, true, cm, registry);
            if (completed)
            {
                break;
            }
        }
        CHECK(completed);
        // After completion, mining state should be reset
        CHECK_FALSE(player.getMiningState().isMining);
    }
}

TEST_CASE("PlayerController::updateMining - crack stage", "[mining]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.setPosition(glm::dvec3{0.0, 67.0, 0.0});

    auto hit = makeHit(glm::ivec3{0, 64, 0}, 3.0f);
    constexpr float dt = 0.05f;

    // Stone: breakTime = 2.25s
    // After 1 tick: progress = 0.05/2.25 = 0.0222 → stage = floor(0.222) = 0
    player.updateMining(dt, hit, true, cm, registry);
    CHECK(player.getMiningState().crackStage == 0);

    // After enough ticks to reach ~50%: stage should be 5
    float breakTime = 2.25f;
    int halfTicks = static_cast<int>(std::ceil(breakTime * 0.5f / dt));
    for (int i = 1; i < halfTicks; ++i)
    {
        player.updateMining(dt, hit, true, cm, registry);
    }
    // Progress should be roughly 0.5 → stage 5
    CHECK(player.getMiningState().crackStage >= 4);
    CHECK(player.getMiningState().crackStage <= 5);
}

TEST_CASE("PlayerController::updateMining - mining air block", "[mining]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.setPosition(glm::dvec3{0.0, 67.0, 0.0});

    // Target an air block above the ground
    auto hitAir = makeHit(glm::ivec3{0, 65, 0}, 2.0f);
    constexpr float dt = 0.05f;

    bool completed = player.updateMining(dt, hitAir, true, cm, registry);
    CHECK_FALSE(completed);
    CHECK_FALSE(player.getMiningState().isMining);
}

TEST_CASE("PlayerController::resetMining", "[mining]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    PlayerController player;
    player.setPosition(glm::dvec3{0.0, 67.0, 0.0});

    auto hit = makeHit(glm::ivec3{0, 64, 0}, 3.0f);
    constexpr float dt = 0.05f;

    // Start mining
    player.updateMining(dt, hit, true, cm, registry);
    CHECK(player.getMiningState().isMining);

    // Explicit reset
    player.resetMining();
    CHECK_FALSE(player.getMiningState().isMining);
    CHECK(player.getMiningState().progress == 0.0f);
    CHECK(player.getMiningState().crackStage == -1);
}
