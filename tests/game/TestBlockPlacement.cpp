#include "voxel/game/CommandQueue.h"
#include "voxel/game/EventBus.h"
#include "voxel/game/GameCommand.h"
#include "voxel/game/PlayerController.h"
#include "voxel/math/AABB.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel;
using namespace voxel::game;
using namespace voxel::world;

namespace
{

/// Helper: set up a BlockRegistry with air (0), stone (1), dirt (2), tall_grass (3=isBuildableTo).
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

/// Simulate the PlaceBlock command handler logic (mirrors GameApp::handleBlockInteraction).
bool executePlaceBlock(
    const PlaceBlockPayload& payload,
    ChunkManager& cm,
    const BlockRegistry& registry,
    const math::AABB& playerAABB,
    EventBus& eventBus)
{
    glm::ivec3 pos{payload.position.x, payload.position.y, payload.position.z};

    // Validate: target must be air or isBuildableTo
    uint16_t targetBlock = cm.getBlock(pos);
    if (targetBlock != BLOCK_AIR)
    {
        const auto& targetDef = registry.getBlockType(targetBlock);
        if (!targetDef.isBuildableTo)
        {
            return false; // Reject
        }
    }

    // Validate: block AABB must not overlap player AABB
    math::AABB blockBox{
        math::Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
        math::Vec3{static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1), static_cast<float>(pos.z + 1)}};
    if (blockBox.intersects(playerAABB))
    {
        return false; // Can't place inside yourself
    }

    cm.setBlock(pos, payload.blockId);
    eventBus.publish<EventType::BlockPlaced>(BlockPlacedEvent{payload.position, payload.blockId});
    return true;
}

/// Simulate the BreakBlock command handler logic (mirrors GameApp::handleBlockInteraction).
bool executeBreakBlock(
    const BreakBlockPayload& payload,
    ChunkManager& cm,
    EventBus& eventBus)
{
    glm::ivec3 pos{payload.position.x, payload.position.y, payload.position.z};
    uint16_t previousId = cm.getBlock(pos);
    if (previousId == BLOCK_AIR)
    {
        return false;
    }
    cm.setBlock(pos, BLOCK_AIR);
    eventBus.publish<EventType::BlockBroken>(BlockBrokenEvent{payload.position, previousId});
    return true;
}

} // namespace

TEST_CASE("Block placement at previousPos sets correct block ID", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t dirtId = registry.getIdByName("base:dirt");

    // Place dirt at (0, 65, 0) — air block above ground
    math::AABB playerAABB{math::Vec3{-0.3f, 70.0f, -0.3f}, math::Vec3{0.3f, 71.8f, 0.3f}};
    PlaceBlockPayload payload{math::IVec3{0, 65, 0}, dirtId};

    bool result = executePlaceBlock(payload, cm, registry, playerAABB, eventBus);
    CHECK(result);
    CHECK(cm.getBlock(glm::ivec3{0, 65, 0}) == dirtId);
}

TEST_CASE("Block placement rejected when overlapping player AABB", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Player standing at feet Y=65, eye height 1.62, AABB: (x: -0.3..0.3, y: 65..66.8, z: -0.3..0.3)
    math::AABB playerAABB{math::Vec3{-0.3f, 65.0f, -0.3f}, math::Vec3{0.3f, 66.8f, 0.3f}};

    // Try to place at (0, 65, 0) — inside the player
    PlaceBlockPayload payload{math::IVec3{0, 65, 0}, stoneId};

    bool result = executePlaceBlock(payload, cm, registry, playerAABB, eventBus);
    CHECK_FALSE(result);
    CHECK(cm.getBlock(glm::ivec3{0, 65, 0}) == BLOCK_AIR); // Block not placed
}

TEST_CASE("Block placement into isBuildableTo block succeeds", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t tallGrassId = registry.getIdByName("base:tall_grass");
    uint16_t stoneId = registry.getIdByName("base:stone");

    // Place tall grass first
    cm.setBlock(glm::ivec3{5, 65, 5}, tallGrassId);
    CHECK(cm.getBlock(glm::ivec3{5, 65, 5}) == tallGrassId);

    // Now place stone on top of tall grass — should succeed (isBuildableTo = true)
    math::AABB playerAABB{math::Vec3{10.0f, 65.0f, 10.0f}, math::Vec3{10.6f, 66.8f, 10.6f}};
    PlaceBlockPayload payload{math::IVec3{5, 65, 5}, stoneId};

    bool result = executePlaceBlock(payload, cm, registry, playerAABB, eventBus);
    CHECK(result);
    CHECK(cm.getBlock(glm::ivec3{5, 65, 5}) == stoneId); // Replaced tall grass
}

TEST_CASE("Block placement into solid block fails", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t dirtId = registry.getIdByName("base:dirt");

    // Try to place dirt at (0, 64, 0) — where stone already is (not isBuildableTo)
    math::AABB playerAABB{math::Vec3{10.0f, 65.0f, 10.0f}, math::Vec3{10.6f, 66.8f, 10.6f}};
    PlaceBlockPayload payload{math::IVec3{0, 64, 0}, dirtId};

    bool result = executePlaceBlock(payload, cm, registry, playerAABB, eventBus);
    CHECK_FALSE(result);
    CHECK(cm.getBlock(glm::ivec3{0, 64, 0}) == stoneId); // Stone still there
}

TEST_CASE("BlockPlacedEvent published with correct position and blockId", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t dirtId = registry.getIdByName("base:dirt");

    // Subscribe to BlockPlaced events
    bool eventReceived = false;
    math::IVec3 receivedPos{};
    uint16_t receivedBlockId = 0;
    eventBus.subscribe<EventType::BlockPlaced>([&](const BlockPlacedEvent& e) {
        eventReceived = true;
        receivedPos = e.position;
        receivedBlockId = e.blockId;
    });

    math::AABB playerAABB{math::Vec3{10.0f, 65.0f, 10.0f}, math::Vec3{10.6f, 66.8f, 10.6f}};
    PlaceBlockPayload payload{math::IVec3{3, 65, 3}, dirtId};

    executePlaceBlock(payload, cm, registry, playerAABB, eventBus);

    CHECK(eventReceived);
    CHECK(receivedPos.x == 3);
    CHECK(receivedPos.y == 65);
    CHECK(receivedPos.z == 3);
    CHECK(receivedBlockId == dirtId);
}

TEST_CASE("BreakBlock command sets block to BLOCK_AIR", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t stoneId = registry.getIdByName("base:stone");
    CHECK(cm.getBlock(glm::ivec3{0, 64, 0}) == stoneId);

    BreakBlockPayload payload{math::IVec3{0, 64, 0}};
    bool result = executeBreakBlock(payload, cm, eventBus);
    CHECK(result);
    CHECK(cm.getBlock(glm::ivec3{0, 64, 0}) == BLOCK_AIR);
}

TEST_CASE("BlockBrokenEvent published with correct previousBlockId", "[placement]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);
    EventBus eventBus;

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Subscribe to BlockBroken events
    bool eventReceived = false;
    uint16_t receivedPrevId = 0;
    math::IVec3 receivedPos{};
    eventBus.subscribe<EventType::BlockBroken>([&](const BlockBrokenEvent& e) {
        eventReceived = true;
        receivedPrevId = e.previousBlockId;
        receivedPos = e.position;
    });

    BreakBlockPayload payload{math::IVec3{0, 64, 0}};
    executeBreakBlock(payload, cm, eventBus);

    CHECK(eventReceived);
    CHECK(receivedPrevId == stoneId);
    CHECK(receivedPos.x == 0);
    CHECK(receivedPos.y == 64);
    CHECK(receivedPos.z == 0);
}

TEST_CASE("Command queue push/drain for PlaceBlock and BreakBlock", "[placement]")
{
    CommandQueue queue;

    // Push a PlaceBlock command
    queue.push(GameCommand{
        CommandType::PlaceBlock,
        0,
        42,
        PlaceBlockPayload{math::IVec3{1, 65, 2}, 5}});

    // Push a BreakBlock command
    queue.push(GameCommand{
        CommandType::BreakBlock,
        0,
        43,
        BreakBlockPayload{math::IVec3{3, 64, 4}}});

    int placeCount = 0;
    int breakCount = 0;
    queue.drain([&](GameCommand cmd) {
        switch (cmd.type)
        {
        case CommandType::PlaceBlock:
        {
            auto& p = std::get<PlaceBlockPayload>(cmd.payload);
            CHECK(p.position.x == 1);
            CHECK(p.position.y == 65);
            CHECK(p.position.z == 2);
            CHECK(p.blockId == 5);
            CHECK(cmd.tick == 42);
            ++placeCount;
            break;
        }
        case CommandType::BreakBlock:
        {
            auto& p = std::get<BreakBlockPayload>(cmd.payload);
            CHECK(p.position.x == 3);
            CHECK(p.position.y == 64);
            CHECK(p.position.z == 4);
            CHECK(cmd.tick == 43);
            ++breakCount;
            break;
        }
        default:
            break;
        }
    });

    CHECK(placeCount == 1);
    CHECK(breakCount == 1);
}
