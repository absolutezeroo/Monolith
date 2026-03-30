#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"

#include <sol/sol.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace voxel::scripting;
using namespace voxel::world;

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
// on_rightclick callback
// ============================================================================

TEST_CASE("Block interaction: on_rightclick fires and receives position", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_rightclick.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:interactive_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onRightclick.has_value());

    BlockCallbackInvoker invoker(lua, registry);
    invoker.invokeOnRightclick(def, {1, 2, 3}, id, 0);

    CHECK(lua["test_rightclick_pos_x"].get<int>() == 1);
    CHECK(lua["test_rightclick_pos_y"].get<int>() == 2);
    CHECK(lua["test_rightclick_pos_z"].get<int>() == 3);
}

// ============================================================================
// on_punch callback
// ============================================================================

TEST_CASE("Block interaction: on_punch fires and receives position", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_punch.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:punchable_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onPunch.has_value());

    BlockCallbackInvoker invoker(lua, registry);
    invoker.invokeOnPunch(def, {10, 20, 30}, id, 0);

    CHECK(lua["test_punch_pos_x"].get<int>() == 10);
    CHECK(lua["test_punch_pos_y"].get<int>() == 20);
    CHECK(lua["test_punch_pos_z"].get<int>() == 30);
}

// ============================================================================
// Sustained interaction: on_interact_start
// ============================================================================

TEST_CASE("Block interaction: on_interact_start returns true", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_sustained.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:grindstone");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onInteractStart.has_value());

    BlockCallbackInvoker invoker(lua, registry);
    bool started = invoker.invokeOnInteractStart(def, {5, 10, 5}, 0);
    REQUIRE(started == true);
    CHECK(lua["test_interact_started"].get<bool>() == true);
}

// ============================================================================
// Sustained interaction: on_interact_step
// ============================================================================

TEST_CASE("Block interaction: on_interact_step returns continue/stop", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_sustained.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:grindstone");
    const auto& def = registry.getBlockType(id);

    BlockCallbackInvoker invoker(lua, registry);

    SECTION("returns true when elapsed < 3.0")
    {
        bool shouldContinue = invoker.invokeOnInteractStep(def, {5, 10, 5}, 0, 1.5f);
        CHECK(shouldContinue == true);
        CHECK(lua["test_interact_elapsed"].get<float>() == 1.5f);
    }

    SECTION("returns false when elapsed >= 3.0")
    {
        bool shouldContinue = invoker.invokeOnInteractStep(def, {5, 10, 5}, 0, 3.0f);
        CHECK(shouldContinue == false);
    }
}

// ============================================================================
// Sustained interaction: on_interact_cancel with reason
// ============================================================================

TEST_CASE("Block interaction: on_interact_cancel receives reason string", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_sustained.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:grindstone");
    const auto& def = registry.getBlockType(id);

    BlockCallbackInvoker invoker(lua, registry);
    bool allowed = invoker.invokeOnInteractCancel(def, {5, 10, 5}, 0, 2.0f, "moved_away");
    CHECK(allowed == true);
    CHECK(lua["test_cancel_reason"].get<std::string>() == "moved_away");
}

// ============================================================================
// Priority: on_interact_start takes priority over on_rightclick
// ============================================================================

TEST_CASE("Block interaction: on_interact_start takes priority over on_rightclick", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_priority.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:priority_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onInteractStart.has_value());
    REQUIRE(def.callbacks->onRightclick.has_value());

    BlockCallbackInvoker invoker(lua, registry);

    // When on_interact_start is present, it should fire first
    bool started = invoker.invokeOnInteractStart(def, {0, 0, 0}, 0);
    CHECK(started == true);
    CHECK(lua["test_which_fired"].get<std::string>() == "interact_start");
}

TEST_CASE("Block interaction: on_rightclick fires when no on_interact_start", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_rightclick.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:interactive_block");
    const auto& def = registry.getBlockType(id);

    // This block has on_rightclick but NOT on_interact_start
    CHECK_FALSE(def.callbacks->onInteractStart.has_value());
    CHECK(def.callbacks->onRightclick.has_value());

    BlockCallbackInvoker invoker(lua, registry);
    invoker.invokeOnRightclick(def, {7, 8, 9}, id, 0);
    CHECK(lua["test_rightclick_pos_x"].get<int>() == 7);
}

// ============================================================================
// Missing callbacks return safe defaults
// ============================================================================

TEST_CASE("Block interaction: missing callbacks return safe defaults", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:simple_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    BlockCallbackInvoker invoker(lua, registry);

    // Invoking on a block with no interaction callbacks should return defaults without crash
    CHECK(invoker.invokeOnInteractStart(def, {0, 0, 0}, 0) == false);
    CHECK(invoker.invokeOnInteractStep(def, {0, 0, 0}, 0, 1.0f) == false);
    CHECK(invoker.invokeOnInteractCancel(def, {0, 0, 0}, 0, 1.0f, "test") == true);

    // Void callbacks: should not crash
    invoker.invokeOnRightclick(def, {0, 0, 0}, id, 0);
    invoker.invokeOnPunch(def, {0, 0, 0}, id, 0);
    invoker.invokeOnInteractStop(def, {0, 0, 0}, 0, 1.0f);
    invoker.invokeOnSecondaryUse(def, 0);
}

// ============================================================================
// on_interact_stop callback
// ============================================================================

TEST_CASE("Block interaction: on_interact_stop fires correctly", "[scripting][interaction]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "interaction_sustained.lua");

    auto& lua = engine.getLuaState();
    uint16_t id = registry.getIdByName("test:grindstone");
    const auto& def = registry.getBlockType(id);

    BlockCallbackInvoker invoker(lua, registry);
    invoker.invokeOnInteractStop(def, {5, 10, 5}, 0, 2.5f);

    CHECK(lua["test_interact_stopped"].get<bool>() == true);
    CHECK(lua["test_interact_stop_elapsed"].get<float>() == 2.5f);
}
