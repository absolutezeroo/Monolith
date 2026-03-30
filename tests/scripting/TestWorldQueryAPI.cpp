#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/BlockTimerManager.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/scripting/ShapeCache.h"
#include "voxel/scripting/WorldQueryAPI.h"
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

/// Set up a full scripting + world environment for testing WorldQueryAPI.
struct WorldQueryTestFixture
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunks;
    BlockTimerManager timerMgr{chunks};
    RateLimiter rateLimiter;

    std::unique_ptr<BlockCallbackInvoker> invoker;
    std::unique_ptr<ShapeCache> shapeCache;

    WorldQueryTestFixture()
    {
        voxel::core::Log::init();
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());
        engine.addAllowedPath(getTestScriptsDir());

        auto& lua = engine.getLuaState();
        LuaBindings::registerBlockAPI(lua, registry);

        invoker = std::make_unique<BlockCallbackInvoker>(lua, registry);
        shapeCache = std::make_unique<ShapeCache>(registry, *invoker);

        WorldQueryAPI::registerWorldAPI(
            lua, chunks, registry, *invoker, timerMgr, rateLimiter, nullptr, shapeCache.get());
    }

    void loadTestScript(const std::string& filename)
    {
        auto result = engine.loadScript(getTestScriptsDir() / filename);
        REQUIRE(result.has_value());
    }

    sol::state& lua() { return engine.getLuaState(); }
};

// ============================================================================
// Block Queries (AC: 1, 10)
// ============================================================================

TEST_CASE("WorldQueryAPI: get_block returns string ID for loaded position", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    // Register a test block
    f.lua().script(R"(voxel.register_block({ id = "test:stone", solid = true }))");

    uint16_t stoneId = f.registry.getIdByName("test:stone");
    REQUIRE(stoneId != BLOCK_AIR);

    // Load chunk and place block
    f.chunks.loadChunk({0, 0});
    f.chunks.setBlock({5, 10, 5}, stoneId);

    f.lua().script(R"(test_result = voxel.get_block({x=5, y=10, z=5}))");
    CHECK(f.lua()["test_result"].get<std::string>() == "test:stone");
}

TEST_CASE("WorldQueryAPI: get_block returns nil for unloaded position", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    // No chunks loaded — position (1000, 50, 1000) is unloaded
    f.lua().script(R"(test_result = voxel.get_block({x=1000, y=50, z=1000}))");
    auto t = f.lua()["test_result"].get_type();
    CHECK((t == sol::type::lua_nil || t == sol::type::none));
}

TEST_CASE("WorldQueryAPI: get_block returns nil for out-of-bounds Y", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    f.lua().script(R"(test_result = voxel.get_block({x=0, y=-1, z=0}))");
    auto t1 = f.lua()["test_result"].get_type();
    CHECK((t1 == sol::type::lua_nil || t1 == sol::type::none));

    f.lua().script(R"(test_result2 = voxel.get_block({x=0, y=256, z=0}))");
    auto t2 = f.lua()["test_result2"].get_type();
    CHECK((t2 == sol::type::lua_nil || t2 == sol::type::none));
}

TEST_CASE("WorldQueryAPI: get_block_info returns block properties", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({
        id = "test:glass",
        solid = true,
        transparent = true,
        hardness = 0.5,
        light_emission = 0,
        groups = { brittle = 1 }
    }))");

    f.lua().script(R"(test_info = voxel.get_block_info("test:glass"))");

    auto info = f.lua()["test_info"];
    CHECK(info["id"].get<std::string>() == "test:glass");
    CHECK(info["solid"].get<bool>() == true);
    CHECK(info["transparent"].get<bool>() == true);
    CHECK(info["hardness"].get<float>() == Approx(0.5f));

    // Check groups sub-table
    sol::table groups = info["groups"];
    CHECK(groups["brittle"].get<int>() == 1);
}

TEST_CASE("WorldQueryAPI: get_block_info returns nil for unknown block", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(test_info = voxel.get_block_info("test:nonexistent"))");
    auto infoType = f.lua()["test_info"].get_type();
    CHECK((infoType == sol::type::lua_nil || infoType == sol::type::none));
}

TEST_CASE("WorldQueryAPI: get_block_state returns state properties", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    // Register a block with state properties
    f.lua().script(R"(voxel.register_block({
        id = "test:slab",
        solid = true,
        properties = {
            { name = "half", values = {"bottom", "top"} }
        }
    }))");

    uint16_t baseId = f.registry.getIdByName("test:slab");
    REQUIRE(baseId != BLOCK_AIR);

    // Set specific state: half=top
    StateMap stateMap;
    stateMap["half"] = "top";
    uint16_t stateId = f.registry.getStateId(baseId, stateMap);

    f.chunks.loadChunk({0, 0});
    f.chunks.setBlock({3, 5, 3}, stateId);

    f.lua().script(R"(test_state = voxel.get_block_state({x=3, y=5, z=3}))");
    sol::table state = f.lua()["test_state"];
    CHECK(state["half"].get<std::string>() == "top");
}

// ============================================================================
// Block Modifications (AC: 2)
// ============================================================================

TEST_CASE("WorldQueryAPI: set_block places block with callback chain", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    // Register blocks with construct/destruct callbacks
    f.lua().script(R"(
        test_construct_count = 0
        test_destruct_count = 0
        voxel.register_block({
            id = "test:tracked",
            solid = true,
            on_construct = function(pos)
                test_construct_count = test_construct_count + 1
            end,
            on_destruct = function(pos)
                test_destruct_count = test_destruct_count + 1
            end
        })
    )");

    f.chunks.loadChunk({0, 0});

    f.lua().script(R"(test_result = voxel.set_block({x=5, y=10, z=5}, "test:tracked"))");
    CHECK(f.lua()["test_result"].get<bool>() == true);
    CHECK(f.lua()["test_construct_count"].get<int>() == 1);

    // Verify block was placed
    uint16_t id = f.chunks.getBlock({5, 10, 5});
    const auto& def = f.registry.getBlockType(id);
    CHECK(def.stringId == "test:tracked");
}

TEST_CASE("WorldQueryAPI: set_block returns nil for unloaded position", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.lua().script(R"(voxel.register_block({ id = "test:stone", solid = true }))");

    f.lua().script(R"(test_result = voxel.set_block({x=1000, y=50, z=1000}, "test:stone"))");
    auto t = f.lua()["test_result"].get_type();
    CHECK((t == sol::type::lua_nil || t == sol::type::none));
}

TEST_CASE("WorldQueryAPI: dig_block fires destruction chain", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        test_destruct_fired = false
        test_after_destruct_fired = false
        voxel.register_block({
            id = "test:breakable",
            solid = true,
            on_destruct = function(pos)
                test_destruct_fired = true
            end,
            after_destruct = function(pos, oldnode)
                test_after_destruct_fired = true
            end
        })
    )");

    uint16_t breakId = f.registry.getIdByName("test:breakable");
    f.chunks.loadChunk({0, 0});
    f.chunks.setBlock({2, 3, 4}, breakId);

    f.lua().script(R"(test_result = voxel.dig_block({x=2, y=3, z=4}))");
    CHECK(f.lua()["test_result"].get<bool>() == true);
    CHECK(f.lua()["test_destruct_fired"].get<bool>() == true);
    CHECK(f.lua()["test_after_destruct_fired"].get<bool>() == true);

    // Verify block is now air
    CHECK(f.chunks.getBlock({2, 3, 4}) == BLOCK_AIR);
}

TEST_CASE("WorldQueryAPI: dig_block returns false for air", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    f.lua().script(R"(test_result = voxel.dig_block({x=0, y=0, z=0}))");
    CHECK(f.lua()["test_result"].get<bool>() == false);
}

TEST_CASE("WorldQueryAPI: swap_block does NOT fire callbacks", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        test_swap_construct = 0
        test_swap_destruct = 0
        voxel.register_block({
            id = "test:door_closed",
            solid = true,
            on_construct = function(pos) test_swap_construct = test_swap_construct + 1 end,
            on_destruct = function(pos) test_swap_destruct = test_swap_destruct + 1 end
        })
        voxel.register_block({
            id = "test:door_open",
            solid = false,
            on_construct = function(pos) test_swap_construct = test_swap_construct + 1 end,
            on_destruct = function(pos) test_swap_destruct = test_swap_destruct + 1 end
        })
    )");

    uint16_t closedId = f.registry.getIdByName("test:door_closed");
    f.chunks.loadChunk({0, 0});
    f.chunks.setBlock({1, 1, 1}, closedId);

    // Reset counters after initial setup
    f.lua().script("test_swap_construct = 0; test_swap_destruct = 0");

    f.lua().script(R"(test_result = voxel.swap_block({x=1, y=1, z=1}, "test:door_open"))");
    CHECK(f.lua()["test_result"].get<bool>() == true);
    CHECK(f.lua()["test_swap_construct"].get<int>() == 0);
    CHECK(f.lua()["test_swap_destruct"].get<int>() == 0);

    // Verify block was swapped
    uint16_t newId = f.chunks.getBlock({1, 1, 1});
    const auto& def = f.registry.getBlockType(newId);
    CHECK(def.stringId == "test:door_open");
}

// ============================================================================
// Area Search (AC: 3)
// ============================================================================

TEST_CASE("WorldQueryAPI: find_blocks_in_area returns matching positions", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:brick", solid = true }))");

    uint16_t brickId = f.registry.getIdByName("test:brick");
    f.chunks.loadChunk({0, 0});

    // Place a 5x5x5 cube of bricks
    for (int x = 0; x < 5; ++x)
    {
        for (int y = 0; y < 5; ++y)
        {
            for (int z = 0; z < 5; ++z)
            {
                f.chunks.setBlock({x, y, z}, brickId);
            }
        }
    }

    f.lua().script(R"(
        test_positions = voxel.find_blocks_in_area(
            {x=0, y=0, z=0}, {x=4, y=4, z=4}, "test:brick")
    )");

    sol::table positions = f.lua()["test_positions"];
    int count = 0;
    for (const auto& [key, value] : positions)
    {
        ++count;
    }
    CHECK(count == 125);
}

TEST_CASE("WorldQueryAPI: count_blocks_in_area returns count without position allocation", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:brick", solid = true }))");

    uint16_t brickId = f.registry.getIdByName("test:brick");
    f.chunks.loadChunk({0, 0});

    for (int x = 0; x < 5; ++x)
    {
        for (int y = 0; y < 5; ++y)
        {
            for (int z = 0; z < 5; ++z)
            {
                f.chunks.setBlock({x, y, z}, brickId);
            }
        }
    }

    f.lua().script(R"(
        test_count = voxel.count_blocks_in_area(
            {x=0, y=0, z=0}, {x=4, y=4, z=4}, "test:brick")
    )");

    CHECK(f.lua()["test_count"].get<int>() == 125);
}

TEST_CASE("WorldQueryAPI: find_blocks_in_area supports group:name filter", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        voxel.register_block({ id = "test:iron_ore", solid = true, groups = { ore = 1 } })
        voxel.register_block({ id = "test:gold_ore", solid = true, groups = { ore = 1 } })
        voxel.register_block({ id = "test:dirt", solid = true })
    )");

    uint16_t ironId = f.registry.getIdByName("test:iron_ore");
    uint16_t goldId = f.registry.getIdByName("test:gold_ore");
    uint16_t dirtId = f.registry.getIdByName("test:dirt");
    f.chunks.loadChunk({0, 0});

    f.chunks.setBlock({0, 0, 0}, ironId);
    f.chunks.setBlock({1, 0, 0}, goldId);
    f.chunks.setBlock({2, 0, 0}, dirtId);

    f.lua().script(R"(
        test_count = voxel.count_blocks_in_area(
            {x=0, y=0, z=0}, {x=2, y=0, z=0}, "group:ore")
    )");

    CHECK(f.lua()["test_count"].get<int>() == 2);
}

// ============================================================================
// Raycasting (AC: 4)
// ============================================================================

TEST_CASE("WorldQueryAPI: raycast returns hit info for solid block", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:wall", solid = true }))");

    uint16_t wallId = f.registry.getIdByName("test:wall");
    f.chunks.loadChunk({0, 0});
    f.chunks.setBlock({5, 5, 5}, wallId);

    // Cast ray toward the block
    f.lua().script(R"(
        test_hit = voxel.raycast(0.5, 5.5, 0.5, 1.0, 0.0, 1.0, 20.0)
    )");

    sol::object hit = f.lua()["test_hit"];
    CHECK(hit.get_type() != sol::type::lua_nil);

    if (hit.get_type() == sol::type::table)
    {
        sol::table hitTable = hit.as<sol::table>();
        CHECK(hitTable["block_id"].get<std::string>() == "test:wall");
    }
}

TEST_CASE("WorldQueryAPI: raycast returns nil for no hit", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    // Cast ray into empty air
    f.lua().script(R"(
        test_hit = voxel.raycast(0.5, 128.5, 0.5, 0.0, 1.0, 0.0, 5.0)
    )");

    auto hitType = f.lua()["test_hit"].get_type();
    CHECK((hitType == sol::type::lua_nil || hitType == sol::type::none));
}

// ============================================================================
// Environment Queries (AC: 5)
// ============================================================================

TEST_CASE("WorldQueryAPI: get_biome returns string", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    // BiomeSystem is nullptr in test fixture, should return "unknown"
    f.lua().script(R"(test_biome = voxel.get_biome(0, 0))");
    CHECK(f.lua()["test_biome"].get<std::string>() == "unknown");
}

TEST_CASE("WorldQueryAPI: get_light returns sky and block light", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    f.lua().script(R"(test_light = voxel.get_light({x=0, y=0, z=0}))");
    sol::object result = f.lua()["test_light"];
    REQUIRE(result.get_type() == sol::type::table);

    sol::table light = result.as<sol::table>();
    // Default light values are 0
    CHECK(light["sky"].get<int>() >= 0);
    CHECK(light["block"].get<int>() >= 0);
}

TEST_CASE("WorldQueryAPI: get_light returns nil for unloaded position", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(test_light = voxel.get_light({x=1000, y=50, z=1000}))");
    auto lightType = f.lua()["test_light"].get_type();
    CHECK((lightType == sol::type::lua_nil || lightType == sol::type::none));
}

TEST_CASE("WorldQueryAPI: time_of_day get/set roundtrip", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        voxel.set_time_of_day(0.75)
        test_time = voxel.get_time_of_day()
    )");

    CHECK(f.lua()["test_time"].get<float>() == Approx(0.75f));
}

// ============================================================================
// Scheduled Ticks (AC: 6)
// ============================================================================

TEST_CASE("WorldQueryAPI: schedule_tick adds to timer manager", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    f.lua().script(R"(
        test_result = voxel.schedule_tick({x=5, y=10, z=5}, 20, 0)
    )");

    CHECK(f.lua()["test_result"].get<bool>() == true);
    CHECK(f.timerMgr.scheduledTickCount() == 1);
}

TEST_CASE("WorldQueryAPI: set_node_timer_active pauses and resumes", "[scripting][world-query]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0});

    // Set a wall-clock timer via Lua API
    LuaBindings::registerTimerAPI(f.lua(), f.timerMgr);
    f.lua().script(R"(voxel.set_timer({x=5, y=10, z=5}, 5.0))");
    CHECK(f.timerMgr.activeTimerCount() == 1);

    // Pause
    f.lua().script(R"(voxel.set_node_timer_active({x=5, y=10, z=5}, false))");
    CHECK(f.timerMgr.activeTimerCount() == 0);

    // Resume
    f.lua().script(R"(voxel.set_node_timer_active({x=5, y=10, z=5}, true))");
    CHECK(f.timerMgr.activeTimerCount() == 1);
}

// ============================================================================
// Pattern Matching (AC: 7)
// ============================================================================

TEST_CASE("WorldQueryAPI: check_pattern matches relative offsets", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        voxel.register_block({ id = "test:iron", solid = true })
        voxel.register_block({ id = "test:gold", solid = true })
    )");

    uint16_t ironId = f.registry.getIdByName("test:iron");
    uint16_t goldId = f.registry.getIdByName("test:gold");
    f.chunks.loadChunk({0, 0});

    f.chunks.setBlock({5, 5, 5}, ironId);
    f.chunks.setBlock({6, 5, 5}, goldId);

    // Check pattern centered at (5,5,5): iron at origin, gold at +1x
    f.lua().script(R"(
        test_match = voxel.check_pattern({x=5, y=5, z=5}, {
            { x=0, y=0, z=0, name="test:iron" },
            { x=1, y=0, z=0, name="test:gold" }
        })
    )");
    CHECK(f.lua()["test_match"].get<bool>() == true);

    // Check pattern that doesn't match
    f.lua().script(R"(
        test_nomatch = voxel.check_pattern({x=5, y=5, z=5}, {
            { x=0, y=0, z=0, name="test:gold" },
            { x=1, y=0, z=0, name="test:iron" }
        })
    )");
    CHECK(f.lua()["test_nomatch"].get<bool>() == false);
}

TEST_CASE("WorldQueryAPI: check_box_pattern verifies solid cube", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:brick", solid = true }))");

    uint16_t brickId = f.registry.getIdByName("test:brick");
    f.chunks.loadChunk({0, 0});

    // 3x3x1 platform
    for (int x = 0; x < 3; ++x)
    {
        for (int z = 0; z < 3; ++z)
        {
            f.chunks.setBlock({x, 0, z}, brickId);
        }
    }

    f.lua().script(R"(
        test_box = voxel.check_box_pattern(
            {x=0, y=0, z=0}, {x=2, y=0, z=2}, "test:brick")
    )");

    sol::table result = f.lua()["test_box"];
    CHECK(result["match"].get<bool>() == true);
    CHECK(result["count"].get<int>() == 9);
}

TEST_CASE("WorldQueryAPI: check_ring detects complete perimeter", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:beacon_base", solid = true }))");

    uint16_t baseId = f.registry.getIdByName("test:beacon_base");
    f.chunks.loadChunk({0, 0});

    // Build a ring of radius 1 around (5, 0, 5)
    // Ring blocks: (4,0,4), (5,0,4), (6,0,4),
    //              (4,0,5),          (6,0,5),
    //              (4,0,6), (5,0,6), (6,0,6)
    for (int x = -1; x <= 1; ++x)
    {
        for (int z = -1; z <= 1; ++z)
        {
            if (x == 0 && z == 0) continue; // skip center
            f.chunks.setBlock({5 + x, 0, 5 + z}, baseId);
        }
    }

    f.lua().script(R"(
        test_ring = voxel.check_ring({x=5, y=0, z=5}, 0, 1, "test:beacon_base")
    )");

    sol::table result = f.lua()["test_ring"];
    CHECK(result["complete"].get<bool>() == true);
    CHECK(result["count"].get<int>() == result["total"].get<int>());
}

// ============================================================================
// Settings API (AC: 8)
// ============================================================================

TEST_CASE("WorldQueryAPI: get_setting returns nil for unknown setting", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(test_setting = voxel.get_setting("nonexistent"))");
    auto settingType = f.lua()["test_setting"].get_type();
    CHECK((settingType == sol::type::lua_nil || settingType == sol::type::none));
}

TEST_CASE("WorldQueryAPI: set_setting respects whitelist", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(
        test_ok = voxel.set_setting("time_speed", "2.0")
        test_val = voxel.get_setting("time_speed")
    )");
    CHECK(f.lua()["test_ok"].get<bool>() == true);
    CHECK(f.lua()["test_val"].get<std::string>() == "2.0");

    // Non-whitelisted setting should fail
    f.lua().script(R"(test_fail = voxel.set_setting("admin_password", "hunter2"))");
    CHECK(f.lua()["test_fail"].get<bool>() == false);
}

// ============================================================================
// Rate Limiting (AC: 9)
// ============================================================================

TEST_CASE("WorldQueryAPI: rate limiter enforces set_block limit", "[scripting][world-query]")
{
    WorldQueryTestFixture f;

    f.lua().script(R"(voxel.register_block({ id = "test:spam", solid = true }))");
    f.chunks.loadChunk({0, 0});

    // Exhaust the rate limit
    for (int i = 0; i < RateLimiter::SET_BLOCK_LIMIT; ++i)
    {
        (void)f.rateLimiter.checkSetBlock();
    }

    // Next call should return nil
    f.lua().script(R"(test_result = voxel.set_block({x=0, y=0, z=0}, "test:spam"))");
    auto nilType = f.lua()["test_result"].get_type();
    CHECK((nilType == sol::type::lua_nil || nilType == sol::type::none));

    // After reset, calls work again
    f.rateLimiter.resetTick();
    f.lua().script(R"(test_result2 = voxel.set_block({x=0, y=0, z=0}, "test:spam"))");
    CHECK(f.lua()["test_result2"].get<bool>() == true);
}

TEST_CASE("RateLimiter: checkSetBlock tracks count correctly", "[scripting][world-query]")
{
    voxel::core::Log::init();
    RateLimiter limiter;

    for (int i = 0; i < RateLimiter::SET_BLOCK_LIMIT; ++i)
    {
        CHECK(limiter.checkSetBlock() == true);
    }
    CHECK(limiter.checkSetBlock() == false);

    limiter.resetTick();
    CHECK(limiter.checkSetBlock() == true);
}

TEST_CASE("RateLimiter: checkRaycast enforces limit", "[scripting][world-query]")
{
    voxel::core::Log::init();
    RateLimiter limiter;

    for (int i = 0; i < RateLimiter::RAYCAST_LIMIT; ++i)
    {
        CHECK(limiter.checkRaycast() == true);
    }
    CHECK(limiter.checkRaycast() == false);
}

TEST_CASE("RateLimiter: checkFindArea enforces limit", "[scripting][world-query]")
{
    voxel::core::Log::init();
    RateLimiter limiter;

    for (int i = 0; i < RateLimiter::FIND_AREA_LIMIT; ++i)
    {
        CHECK(limiter.checkFindArea() == true);
    }
    CHECK(limiter.checkFindArea() == false);
}

TEST_CASE("RateLimiter: checkPattern enforces limit", "[scripting][world-query]")
{
    voxel::core::Log::init();
    RateLimiter limiter;

    for (int i = 0; i < RateLimiter::PATTERN_LIMIT; ++i)
    {
        CHECK(limiter.checkPattern() == true);
    }
    CHECK(limiter.checkPattern() == false);
}

TEST_CASE("RateLimiter: checkSchedule enforces limit", "[scripting][world-query]")
{
    voxel::core::Log::init();
    RateLimiter limiter;

    for (int i = 0; i < RateLimiter::SCHEDULE_LIMIT; ++i)
    {
        CHECK(limiter.checkSchedule() == true);
    }
    CHECK(limiter.checkSchedule() == false);
}

// ============================================================================
// Integration: 5x5x5 cube + area search (AC: 11)
// ============================================================================

TEST_CASE("WorldQueryAPI: integration test — 5x5x5 cube placement and area search", "[scripting][world-query][integration]")
{
    WorldQueryTestFixture f;
    f.chunks.loadChunk({0, 0}); // Must load chunk before Lua script places blocks
    f.loadTestScript("world_query_test.lua");

    // The test script places a 5x5x5 cube and verifies find_blocks_in_area
    CHECK(f.lua()["test_cube_count"].get<int>() == 125);
    CHECK(f.lua()["test_cube_search_count"].get<int>() == 125);
}
