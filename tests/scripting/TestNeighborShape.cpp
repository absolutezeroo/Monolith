#include "voxel/core/Log.h"
#include "voxel/math/AABB.h"
#include "voxel/physics/Raycast.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/NeighborNotifier.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/scripting/ShapeCache.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"

#include <sol/sol.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace voxel::scripting;
using namespace voxel::world;
using Catch::Approx;

static std::filesystem::path getTestScriptsDir()
{
    std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
    return assetsDir.parent_path() / "tests" / "scripting" / "test_scripts";
}

static void setupEngine(ScriptEngine& engine, BlockRegistry& registry)
{
    voxel::core::Log::init();
    auto initResult = engine.init();
    REQUIRE(initResult.has_value());
    engine.addAllowedPath(getTestScriptsDir());
    LuaBindings::registerBlockAPI(engine.getLuaState(), registry);
}

// ============================================================================
// NeighborNotifier — basic notification and cascade
// ============================================================================

TEST_CASE("NeighborNotifier: on_neighbor_changed fires for 6 neighbors", "[scripting][neighbor]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ChunkManager chunks;

    // Register a block with on_neighbor_changed that records calls
    auto& lua = engine.getLuaState();
    lua.script(R"(
        notification_count = 0
        voxel.register_block({
            id = "test:observer",
            solid = true,
            on_neighbor_changed = function(pos, neighbor_pos, neighbor_node)
                notification_count = notification_count + 1
            end,
        })
    )");

    // Load chunk and place observer blocks in a plus pattern around (8, 64, 8)
    chunks.loadChunk({0, 0});
    auto* column = chunks.getChunkColumn({0, 0});
    REQUIRE(column != nullptr);

    uint16_t observerId = registry.getIdByName("test:observer");
    REQUIRE(observerId != BLOCK_AIR);

    glm::ivec3 center{8, 64, 8};
    // Place observers at all 6 neighbors
    static constexpr glm::ivec3 OFFSETS[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    for (const auto& offset : OFFSETS)
    {
        glm::ivec3 pos = center + offset;
        chunks.setBlock(pos, observerId);
    }

    BlockCallbackInvoker invoker(lua, registry);
    NeighborNotifier notifier(chunks, registry, invoker);

    // Change the center block — all 6 observers should get notified
    chunks.setBlock(center, observerId);
    notifier.notifyNeighbors(center);

    int count = lua["notification_count"].get<int>();
    CHECK(count == 6);
}

TEST_CASE("NeighborNotifier: can_survive=false causes block removal", "[scripting][neighbor]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ChunkManager chunks;

    auto& lua = engine.getLuaState();

    // Register a support block and a dependent block (like a torch on a wall)
    lua.script(R"(
        voxel.register_block({
            id = "test:support",
            solid = true,
        })
        voxel.register_block({
            id = "test:dependent",
            solid = false,
            can_survive = function(pos)
                -- survives only if block below is not air
                local below = voxel.get_neighbor_at(pos, "down")
                return below.id ~= "base:air"
            end,
        })
    )");

    // Need neighbor API for can_survive
    LuaBindings::registerNeighborAPI(lua, chunks, registry);

    chunks.loadChunk({0, 0});

    uint16_t supportId = registry.getIdByName("test:support");
    uint16_t dependentId = registry.getIdByName("test:dependent");
    REQUIRE(supportId != BLOCK_AIR);
    REQUIRE(dependentId != BLOCK_AIR);

    // Place support at (8, 63, 8), dependent at (8, 64, 8)
    chunks.setBlock({8, 63, 8}, supportId);
    chunks.setBlock({8, 64, 8}, dependentId);

    // Verify dependent is placed
    CHECK(chunks.getBlock({8, 64, 8}) == dependentId);

    BlockCallbackInvoker invoker(lua, registry);
    NeighborNotifier notifier(chunks, registry, invoker);

    // Remove support block — dependent should cascade-break
    chunks.setBlock({8, 63, 8}, BLOCK_AIR);
    notifier.notifyNeighbors({8, 63, 8});

    // Dependent should now be air
    CHECK(chunks.getBlock({8, 64, 8}) == BLOCK_AIR);
}

// ============================================================================
// ShapeCache — collision shape queries
// ============================================================================

TEST_CASE("ShapeCache: default blocks return empty span", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        voxel.register_block({
            id = "test:plain",
            solid = true,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);
    ShapeCache cache(registry, invoker);

    uint16_t plainId = registry.getIdByName("test:plain");
    REQUIRE(plainId != BLOCK_AIR);

    auto shapes = cache.getCollisionShape({0, 64, 0}, plainId);
    CHECK(shapes.empty()); // No custom shape — caller uses default
}

TEST_CASE("ShapeCache: custom collision shape from Lua callback", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        voxel.register_block({
            id = "test:slab",
            solid = true,
            get_collision_shape = function(pos)
                return {{0, 0, 0, 1, 0.5, 1}}
            end,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);
    ShapeCache cache(registry, invoker);

    uint16_t slabId = registry.getIdByName("test:slab");
    REQUIRE(slabId != BLOCK_AIR);

    auto shapes = cache.getCollisionShape({0, 64, 0}, slabId);
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0].min.x == Approx(0.0f));
    CHECK(shapes[0].min.y == Approx(0.0f));
    CHECK(shapes[0].max.y == Approx(0.5f));
    CHECK(shapes[0].max.x == Approx(1.0f));
}

TEST_CASE("ShapeCache: invalidate marks entry dirty", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        call_count = 0
        voxel.register_block({
            id = "test:counter_shape",
            solid = true,
            get_collision_shape = function(pos)
                call_count = call_count + 1
                return {{0, 0, 0, 1, 0.5, 1}}
            end,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);
    ShapeCache cache(registry, invoker);

    uint16_t id = registry.getIdByName("test:counter_shape");
    REQUIRE(id != BLOCK_AIR);

    // First call — should invoke Lua
    cache.getCollisionShape({0, 64, 0}, id);
    CHECK(lua["call_count"].get<int>() == 1);

    // Second call — cached, Lua not invoked
    cache.getCollisionShape({0, 64, 0}, id);
    CHECK(lua["call_count"].get<int>() == 1);

    // Invalidate and re-query — should invoke Lua again
    cache.invalidate({0, 64, 0});
    cache.getCollisionShape({0, 64, 0}, id);
    CHECK(lua["call_count"].get<int>() == 2);
}

TEST_CASE("ShapeCache: selection falls back to collision", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        voxel.register_block({
            id = "test:selfall",
            solid = true,
            get_collision_shape = function(pos)
                return {{0, 0, 0, 1, 0.5, 1}}
            end,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);
    ShapeCache cache(registry, invoker);

    uint16_t id = registry.getIdByName("test:selfall");
    REQUIRE(id != BLOCK_AIR);

    auto selShapes = cache.getSelectionShape({0, 64, 0}, id);
    REQUIRE(selShapes.size() == 1);
    CHECK(selShapes[0].max.y == Approx(0.5f));
}

// ============================================================================
// rayIntersectsAABB — unit test
// ============================================================================

TEST_CASE("rayIntersectsAABB: hit and miss", "[physics][shape]")
{
    voxel::math::AABB box{{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 1.0f}};

    // Ray pointing straight at the box center
    glm::vec3 origin{0.5f, -1.0f, 0.5f};
    glm::vec3 dir{0.0f, 1.0f, 0.0f};
    glm::vec3 invDir{1e30f, 1.0f, 1e30f};

    float tMin = 0.0f;
    CHECK(voxel::physics::rayIntersectsAABB(origin, invDir, box, tMin));
    CHECK(tMin == Approx(1.0f));

    // Ray pointing away
    glm::vec3 origin2{5.0f, 5.0f, 5.0f};
    glm::vec3 dir2{1.0f, 0.0f, 0.0f};
    glm::vec3 invDir2{1.0f, 1e30f, 1e30f};
    float tMin2 = 0.0f;
    CHECK_FALSE(voxel::physics::rayIntersectsAABB(origin2, invDir2, box, tMin2));
}

// ============================================================================
// InvokeCanAttachAt — default fallback
// ============================================================================

TEST_CASE("invokeCanAttachAt: defaults to isSolid when no callback", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        voxel.register_block({
            id = "test:solid_block",
            solid = true,
        })
        voxel.register_block({
            id = "test:nonsolid_block",
            solid = false,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);

    uint16_t solidId = registry.getIdByName("test:solid_block");
    uint16_t nonsolidId = registry.getIdByName("test:nonsolid_block");
    REQUIRE(solidId != BLOCK_AIR);
    REQUIRE(nonsolidId != BLOCK_AIR);

    const auto& solidDef = registry.getBlockType(solidId);
    const auto& nonsolidDef = registry.getBlockType(nonsolidId);

    CHECK(invoker.invokeCanAttachAt(solidDef, {0, 0, 0}, "up") == true);
    CHECK(invoker.invokeCanAttachAt(nonsolidDef, {0, 0, 0}, "up") == false);
}

TEST_CASE("invokeCanAttachAt: Lua callback overrides default", "[scripting][shape]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto& lua = engine.getLuaState();
    lua.script(R"(
        voxel.register_block({
            id = "test:fence",
            solid = false,
            can_attach_at = function(pos, face)
                return face == "up" or face == "down"
            end,
        })
    )");

    BlockCallbackInvoker invoker(lua, registry);

    uint16_t fenceId = registry.getIdByName("test:fence");
    REQUIRE(fenceId != BLOCK_AIR);
    const auto& fenceDef = registry.getBlockType(fenceId);

    CHECK(invoker.invokeCanAttachAt(fenceDef, {0, 0, 0}, "up") == true);
    CHECK(invoker.invokeCanAttachAt(fenceDef, {0, 0, 0}, "east") == false);
}
