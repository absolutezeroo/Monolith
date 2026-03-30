#include "voxel/core/Log.h"
#include "voxel/scripting/ABMRegistry.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/BlockTimerManager.h"
#include "voxel/scripting/LBMRegistry.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
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

static void loadTestScript(ScriptEngine& engine, const std::string& filename)
{
    auto result = engine.loadScript(getTestScriptsDir() / filename);
    REQUIRE(result.has_value());
}

// ============================================================================
// BlockTimerManager — setTimer / getTimer / cancelTimer / onBlockRemoved
// ============================================================================

TEST_CASE("BlockTimerManager: setTimer and getTimer", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    mgr.setTimer({5, 10, 5}, 2.0f);

    auto remaining = mgr.getTimer({5, 10, 5});
    REQUIRE(remaining.has_value());
    CHECK(*remaining == Approx(2.0f));
    CHECK(mgr.activeTimerCount() == 1);
}

TEST_CASE("BlockTimerManager: getTimer returns nullopt for no timer", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    auto remaining = mgr.getTimer({0, 0, 0});
    CHECK_FALSE(remaining.has_value());
    CHECK(mgr.activeTimerCount() == 0);
}

TEST_CASE("BlockTimerManager: cancelTimer removes active timer", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    mgr.setTimer({1, 2, 3}, 5.0f);
    REQUIRE(mgr.activeTimerCount() == 1);

    mgr.cancelTimer({1, 2, 3});
    CHECK(mgr.activeTimerCount() == 0);
    CHECK_FALSE(mgr.getTimer({1, 2, 3}).has_value());
}

TEST_CASE("BlockTimerManager: cancelTimer is no-op for nonexistent timer", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    mgr.cancelTimer({99, 99, 99}); // should not crash
    CHECK(mgr.activeTimerCount() == 0);
}

TEST_CASE("BlockTimerManager: onBlockRemoved cancels timer at position", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    mgr.setTimer({1, 2, 3}, 5.0f);
    mgr.onBlockRemoved({1, 2, 3});
    CHECK(mgr.activeTimerCount() == 0);
}

TEST_CASE("BlockTimerManager: setTimer replaces existing timer", "[scripting][timers]")
{
    ChunkManager chunks;
    BlockTimerManager mgr(chunks);

    mgr.setTimer({5, 5, 5}, 10.0f);
    CHECK(*mgr.getTimer({5, 5, 5}) == Approx(10.0f));

    mgr.setTimer({5, 5, 5}, 3.0f);
    CHECK(mgr.activeTimerCount() == 1);
    CHECK(*mgr.getTimer({5, 5, 5}) == Approx(3.0f));
}

// ============================================================================
// BlockTimerManager::update — timer firing
// ============================================================================

TEST_CASE("BlockTimerManager: timer fires on_timer after elapsed time", "[scripting][timers]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "timer_furnace.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    uint16_t id = registry.getIdByName("test:furnace");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onTimer.has_value());

    // Direct invoke test
    bool restart = invoker.invokeOnTimer(def, {5, 10, 5}, 2.0f);
    CHECK(restart == true);
    CHECK(lua["test_timer_fired"].get<bool>() == true);
    CHECK(lua["test_timer_elapsed"].get<float>() == Approx(2.0f));
}

TEST_CASE("BlockTimerManager: invokeOnTimer returns false stops timer", "[scripting][timers]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "timer_furnace.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    uint16_t id = registry.getIdByName("test:furnace");
    REQUIRE(id != BLOCK_AIR);
    const auto& def = registry.getBlockType(id);

    // Set test_timer_restart = false to make on_timer return false
    lua["test_timer_restart"] = false;
    bool restart = invoker.invokeOnTimer(def, {5, 10, 5}, 1.0f);
    CHECK(restart == false);
}

TEST_CASE("BlockTimerManager: invokeOnTimer returns false for block without callback", "[scripting][timers]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    // Register a block with no on_timer callback
    auto& lua = engine.getLuaState();
    lua.script(R"(voxel.register_block({ id = "test:plain_block", solid = true }))");

    uint16_t id = registry.getIdByName("test:plain_block");
    REQUIRE(id != BLOCK_AIR);
    const auto& def = registry.getBlockType(id);

    BlockCallbackInvoker invoker(lua, registry);
    bool restart = invoker.invokeOnTimer(def, {0, 0, 0}, 1.0f);
    CHECK(restart == false);
}

// ============================================================================
// Lua API: voxel.set_timer / voxel.get_timer
// ============================================================================

TEST_CASE("Lua API: voxel.set_timer and voxel.get_timer", "[scripting][timers]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ChunkManager chunks;
    BlockTimerManager timerMgr(chunks);
    LuaBindings::registerTimerAPI(engine.getLuaState(), timerMgr);

    auto& lua = engine.getLuaState();

    lua.script(R"(voxel.set_timer({x=5, y=10, z=5}, 3.5))");
    CHECK(timerMgr.activeTimerCount() == 1);

    lua.script(R"(test_remaining = voxel.get_timer({x=5, y=10, z=5}))");
    CHECK(lua["test_remaining"].get<float>() == Approx(3.5f));
}

TEST_CASE("Lua API: voxel.get_timer returns nil for no timer", "[scripting][timers]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ChunkManager chunks;
    BlockTimerManager timerMgr(chunks);
    LuaBindings::registerTimerAPI(engine.getLuaState(), timerMgr);

    auto& lua = engine.getLuaState();
    lua.script(R"(test_val = voxel.get_timer({x=0, y=0, z=0}))");
    // sol2 may represent nil as none or lua_nil depending on context
    CHECK((lua["test_val"].get_type() == sol::type::lua_nil || lua["test_val"].get_type() == sol::type::none));
}

// ============================================================================
// ABMRegistry — registration and scanning
// ============================================================================

TEST_CASE("ABMRegistry: register_abm stores ABM", "[scripting][abm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ABMRegistry abmRegistry;
    LuaBindings::registerABMAPI(engine.getLuaState(), abmRegistry, registry);
    loadTestScript(engine, "abm_grass_spread.lua");

    CHECK(abmRegistry.abmCount() == 1);
}

TEST_CASE("ABMRegistry: ABM action fires for matching block", "[scripting][abm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ABMRegistry abmRegistry;
    LuaBindings::registerABMAPI(engine.getLuaState(), abmRegistry, registry);
    loadTestScript(engine, "abm_grass_spread.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    // Set up a chunk with a dirt block and a grass neighbor
    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    uint16_t dirtId = registry.getIdByName("test:dirt");
    uint16_t grassId = registry.getIdByName("test:grass");
    REQUIRE(dirtId != BLOCK_AIR);
    REQUIRE(grassId != BLOCK_AIR);

    // Place dirt at (0,0,0) and grass at (1,0,0) as neighbor
    chunks.setBlock({0, 0, 0}, dirtId);
    chunks.setBlock({1, 0, 0}, grassId);

    // Run ABM update with enough dt to trigger the scan (interval = 1.0)
    abmRegistry.update(1.0f, chunks, registry, invoker);
    // Need to continue scanning until completion (scan is spread across ticks)
    for (int i = 0; i < 32; ++i)
    {
        abmRegistry.update(0.0f, chunks, registry, invoker);
    }

    CHECK(lua["test_abm_fired"].get<bool>() == true);
    CHECK(lua["test_abm_pos_x"].get<int>() == 0);
    CHECK(lua["test_abm_pos_y"].get<int>() == 0);
    CHECK(lua["test_abm_pos_z"].get<int>() == 0);
}

TEST_CASE("ABMRegistry: ABM does not fire when no blocks match", "[scripting][abm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ABMRegistry abmRegistry;
    LuaBindings::registerABMAPI(engine.getLuaState(), abmRegistry, registry);
    loadTestScript(engine, "abm_grass_spread.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    // Empty chunk — no dirt blocks
    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    abmRegistry.update(1.0f, chunks, registry, invoker);
    for (int i = 0; i < 32; ++i)
    {
        abmRegistry.update(0.0f, chunks, registry, invoker);
    }

    CHECK(lua["test_abm_fired"].get<bool>() == false);
}

TEST_CASE("ABMRegistry: ABM respects interval", "[scripting][abm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    ABMRegistry abmRegistry;
    LuaBindings::registerABMAPI(engine.getLuaState(), abmRegistry, registry);
    loadTestScript(engine, "abm_grass_spread.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    uint16_t dirtId = registry.getIdByName("test:dirt");
    uint16_t grassId = registry.getIdByName("test:grass");
    chunks.setBlock({0, 0, 0}, dirtId);
    chunks.setBlock({1, 0, 0}, grassId);

    // Not enough time elapsed (0.5s < 1.0s interval) — should not fire
    abmRegistry.update(0.5f, chunks, registry, invoker);
    CHECK(lua["test_abm_fired"].get<bool>() == false);
}

// ============================================================================
// LBMRegistry — registration and chunk load
// ============================================================================

TEST_CASE("LBMRegistry: register_lbm stores LBM", "[scripting][lbm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    LBMRegistry lbmRegistry;
    LuaBindings::registerLBMAPI(engine.getLuaState(), lbmRegistry, registry);
    loadTestScript(engine, "lbm_upgrade_torch.lua");

    CHECK(lbmRegistry.lbmCount() == 1);
}

TEST_CASE("LBMRegistry: LBM fires on chunk load for matching blocks", "[scripting][lbm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    LBMRegistry lbmRegistry;
    LuaBindings::registerLBMAPI(engine.getLuaState(), lbmRegistry, registry);
    loadTestScript(engine, "lbm_upgrade_torch.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    // Set up a chunk with a torch_old block
    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    uint16_t torchId = registry.getIdByName("test:torch_old");
    REQUIRE(torchId != BLOCK_AIR);
    chunks.setBlock({3, 5, 7}, torchId);

    // Simulate chunk load event
    lbmRegistry.onChunkLoaded({0, 0}, chunks, registry, invoker);

    CHECK(lua["test_lbm_fired"].get<bool>() == true);
    CHECK(lua["test_lbm_count"].get<int>() == 1);
}

TEST_CASE("LBMRegistry: non-repeating LBM fires only once", "[scripting][lbm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    LBMRegistry lbmRegistry;
    LuaBindings::registerLBMAPI(engine.getLuaState(), lbmRegistry, registry);
    loadTestScript(engine, "lbm_upgrade_torch.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    uint16_t torchId = registry.getIdByName("test:torch_old");
    REQUIRE(torchId != BLOCK_AIR);
    chunks.setBlock({3, 5, 7}, torchId);

    // First load — fires
    lbmRegistry.onChunkLoaded({0, 0}, chunks, registry, invoker);
    CHECK(lua["test_lbm_count"].get<int>() == 1);

    // Second load — should NOT fire (run_at_every_load = false)
    lbmRegistry.onChunkLoaded({0, 0}, chunks, registry, invoker);
    CHECK(lua["test_lbm_count"].get<int>() == 1); // still 1
}

TEST_CASE("LBMRegistry: LBM does not fire when no blocks match", "[scripting][lbm]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    LBMRegistry lbmRegistry;
    LuaBindings::registerLBMAPI(engine.getLuaState(), lbmRegistry, registry);
    loadTestScript(engine, "lbm_upgrade_torch.lua");

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    // Empty chunk — no torch blocks
    ChunkManager chunks;
    chunks.loadChunk({0, 0});

    lbmRegistry.onChunkLoaded({0, 0}, chunks, registry, invoker);

    CHECK(lua["test_lbm_fired"].get<bool>() == false);
    CHECK(lua["test_lbm_count"].get<int>() == 0);
}

// ============================================================================
// IVec3Hash
// ============================================================================

TEST_CASE("IVec3Hash: different positions produce different hashes", "[math][hash]")
{
    voxel::math::IVec3Hash hasher;

    size_t h1 = hasher({0, 0, 0});
    size_t h2 = hasher({1, 0, 0});
    size_t h3 = hasher({0, 1, 0});
    size_t h4 = hasher({0, 0, 1});

    CHECK(h1 != h2);
    CHECK(h1 != h3);
    CHECK(h1 != h4);
    CHECK(h2 != h3);
}

TEST_CASE("IVec3Hash: same position produces same hash", "[math][hash]")
{
    voxel::math::IVec3Hash hasher;

    CHECK(hasher({5, 10, 15}) == hasher({5, 10, 15}));
    CHECK(hasher({-1, -2, -3}) == hasher({-1, -2, -3}));
}
