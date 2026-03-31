#include "voxel/core/Log.h"
#include "voxel/scripting/ComboDetector.h"
#include "voxel/scripting/GlobalEventRegistry.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/BlockRegistry.h"

#include <sol/sol.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using namespace voxel::scripting;
using namespace voxel::world;

static std::filesystem::path getTestScriptsDir()
{
    std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
    return assetsDir.parent_path() / "tests" / "scripting" / "test_scripts";
}

static void setupEngine(ScriptEngine& engine, BlockRegistry& registry, GlobalEventRegistry& events,
    ComboDetector& combos)
{
    voxel::core::Log::init();
    auto initResult = engine.init();
    REQUIRE(initResult.has_value());
    engine.addAllowedPath(getTestScriptsDir());
    LuaBindings::registerBlockAPI(engine.getLuaState(), registry);
    LuaBindings::registerGlobalEventAPI(engine.getLuaState(), events, combos);
}

// ============================================================================
// GlobalEventRegistry — basic event registration and firing
// ============================================================================

TEST_CASE("GlobalEventRegistry: fireEvent invokes registered callback", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_block.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Fire tick event
    events.fireEvent("tick", 0.05f);

    REQUIRE(lua["test_tick_called"].get<bool>() == true);
    REQUIRE(lua["test_tick_dt"].get<float>() > 0.04f);
}

TEST_CASE("GlobalEventRegistry: multiple callbacks fire in order", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();

    // Register order tracking
    lua.script(R"(
        call_order = {}
        voxel.on("tick", function(dt) table.insert(call_order, "first") end)
        voxel.on("tick", function(dt) table.insert(call_order, "second") end)
        voxel.on("tick", function(dt) table.insert(call_order, "third") end)
    )");

    events.fireEvent("tick", 0.05f);

    sol::table order = lua["call_order"];
    REQUIRE(order.size() == 3);
    CHECK(order.get<std::string>(1) == "first");
    CHECK(order.get<std::string>(2) == "second");
    CHECK(order.get<std::string>(3) == "third");
}

TEST_CASE("GlobalEventRegistry: error in callback does not prevent others", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();

    lua.script(R"(
        test_first = false
        test_third = false
        voxel.on("tick", function(dt) test_first = true end)
        voxel.on("tick", function(dt) error("intentional error") end)
        voxel.on("tick", function(dt) test_third = true end)
    )");

    events.fireEvent("tick", 0.05f);

    CHECK(lua["test_first"].get<bool>() == true);
    CHECK(lua["test_third"].get<bool>() == true);
}

// ============================================================================
// Cancelable events
// ============================================================================

TEST_CASE("GlobalEventRegistry: cancelable event returns true when callback returns false",
    "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_cancel.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Fire player_interact with "break" action — should be cancelled
    sol::table posTable = lua.create_table_with("x", 10, "y", 20, "z", 30);
    bool cancelled = events.fireCancelableEvent("player_interact", 0, std::string("break"), posTable, std::string("test:stone"));

    REQUIRE(cancelled == true);
    REQUIRE(lua["test_interact_called"].get<bool>() == true);
    REQUIRE(lua["test_interact_action"].get<std::string>() == "break");
    REQUIRE(lua["test_interact_pos_x"].get<int>() == 10);
}

TEST_CASE("GlobalEventRegistry: cancelable event not cancelled for allowed action", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_cancel.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Fire player_interact with "place" action — should NOT be cancelled
    sol::table posTable = lua.create_table_with("x", 5, "y", 10, "z", 15);
    bool cancelled = events.fireCancelableEvent("player_interact", 0, std::string("place"), posTable, std::string("test:wood"));

    REQUIRE(cancelled == false);
    REQUIRE(lua["test_interact_called"].get<bool>() == true);
    REQUIRE(lua["test_interact_action"].get<std::string>() == "place");
}

TEST_CASE("GlobalEventRegistry: all cancelable callbacks fire even after cancellation", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();

    lua.script(R"(
        cancel_first = false
        cancel_second = false
        cancel_third = false
        voxel.on("key_pressed", function(e, k) cancel_first = true; return false end)
        voxel.on("key_pressed", function(e, k) cancel_second = true; return true end)
        voxel.on("key_pressed", function(e, k) cancel_third = true end)
    )");

    bool cancelled = events.fireCancelableEvent("key_pressed", 0, std::string("w"));
    REQUIRE(cancelled == true);
    CHECK(lua["cancel_first"].get<bool>() == true);
    CHECK(lua["cancel_second"].get<bool>() == true);
    CHECK(lua["cancel_third"].get<bool>() == true);
}

// ============================================================================
// clearAll for hot-reload
// ============================================================================

TEST_CASE("GlobalEventRegistry: clearAll removes all callbacks", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();
    lua.script(R"(voxel.on("tick", function(dt) end))");

    REQUIRE(events.callbackCount("tick") == 1);

    events.clearAll();
    REQUIRE(events.callbackCount("tick") == 0);
}

// ============================================================================
// Unknown event name warning
// ============================================================================

TEST_CASE("GlobalEventRegistry: unknown event name registers with warning", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();

    // Register unknown event — should log warning but still work
    lua.script(R"(voxel.on("custom_event_xyz", function() end))");

    REQUIRE(events.callbackCount("custom_event_xyz") == 1);
}

// ============================================================================
// ComboDetector
// ============================================================================

TEST_CASE("ComboDetector: fires callback after key sequence within window", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_input.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Simulate "w", "w" sequence within 0.5s window
    combos.onKeyPress("w", 1.0f, sol::make_object(lua, 0));
    REQUIRE(lua["test_combo_fired"].get<bool>() == false);

    combos.onKeyPress("w", 1.3f, sol::make_object(lua, 0));
    REQUIRE(lua["test_combo_fired"].get<bool>() == true);
    REQUIRE(lua["test_combo_name"].get<std::string>() == "dash");
}

TEST_CASE("ComboDetector: does not fire if window exceeded", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_input.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Simulate "w", "w" sequence outside 0.5s window
    combos.onKeyPress("w", 1.0f, sol::make_object(lua, 0));
    combos.onKeyPress("w", 2.0f, sol::make_object(lua, 0)); // 1.0s gap > 0.5s window

    REQUIRE(lua["test_combo_fired"].get<bool>() == false);
}

TEST_CASE("ComboDetector: clearAll removes combos", "[scripting][global-events]")
{
    ComboDetector combos;
    REQUIRE(combos.comboCount() == 0);

    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();
    lua.script(R"(voxel.register_combo("test", {"a", "b"}, 1.0, function() end))");
    REQUIRE(combos.comboCount() == 1);

    combos.clearAll();
    REQUIRE(combos.comboCount() == 0);
}

// ============================================================================
// Tick timing warning (AC: 5)
// ============================================================================

TEST_CASE("GlobalEventRegistry: tick event fires with delta time", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto& lua = engine.getLuaState();

    lua.script(R"(
        test_dt_sum = 0
        voxel.on("tick", function(dt) test_dt_sum = test_dt_sum + dt end)
    )");

    events.fireEvent("tick", 0.05f);
    events.fireEvent("tick", 0.05f);
    events.fireEvent("tick", 0.05f);

    float sum = lua["test_dt_sum"].get<float>();
    CHECK(sum > 0.14f);
    CHECK(sum < 0.16f);
}

// ============================================================================
// Block events via GlobalEventRegistry
// ============================================================================

TEST_CASE("GlobalEventRegistry: block_placed event fires with pos and id", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_block.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    // Simulate block_placed event
    sol::table posTable = lua.create_table_with("x", 5, "y", 64, "z", 10);
    events.fireEvent("block_placed", posTable, std::string("base:stone"));

    REQUIRE(lua["test_block_placed_called"].get<bool>() == true);
    REQUIRE(lua["test_block_placed_pos_x"].get<int>() == 5);
    REQUIRE(lua["test_block_placed_id"].get<std::string>() == "base:stone");
}

TEST_CASE("GlobalEventRegistry: block_broken event fires with pos and id", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "global_events_block.lua");
    REQUIRE(loadResult.has_value());

    auto& lua = engine.getLuaState();

    sol::table posTable = lua.create_table_with("x", 3, "y", 72, "z", 8);
    events.fireEvent("block_broken", posTable, std::string("base:dirt"));

    REQUIRE(lua["test_block_broken_called"].get<bool>() == true);
    REQUIRE(lua["test_block_broken_pos_y"].get<int>() == 72);
    REQUIRE(lua["test_block_broken_id"].get<std::string>() == "base:dirt");
}

// ============================================================================
// callbackCount
// ============================================================================

TEST_CASE("GlobalEventRegistry: callbackCount returns correct count", "[scripting][global-events]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    GlobalEventRegistry events;
    ComboDetector combos;
    setupEngine(engine, registry, events, combos);

    REQUIRE(events.callbackCount("tick") == 0);

    auto& lua = engine.getLuaState();
    lua.script(R"(voxel.on("tick", function(dt) end))");
    REQUIRE(events.callbackCount("tick") == 1);

    lua.script(R"(voxel.on("tick", function(dt) end))");
    REQUIRE(events.callbackCount("tick") == 2);

    REQUIRE(events.callbackCount("block_placed") == 0);
}
