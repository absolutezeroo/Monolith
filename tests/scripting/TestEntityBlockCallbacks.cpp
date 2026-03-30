#include "voxel/core/Log.h"
#include "voxel/game/PlayerController.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/EntityHandle.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"

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
    LuaBindings::registerEntityAPI(engine.getLuaState());
}

// ============================================================================
// on_entity_inside — fires with entity access
// ============================================================================

TEST_CASE("Entity callback: on_entity_inside fires with entity methods", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_inside.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:cactus"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onEntityInside.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    invoker.invokeOnEntityInside(def, {3, 65, 3}, entity);

    REQUIRE(lua["test_entity_pos_set"].get<bool>() == true);
}

// ============================================================================
// on_entity_fall_on — returns damage modifier
// ============================================================================

TEST_CASE("Entity callback: on_entity_fall_on returns 0 for slime block", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_fall_on.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:slime_block"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onEntityFallOn.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    float damageMul = invoker.invokeOnEntityFallOn(def, {5, 64, 5}, entity, 10.0f);
    REQUIRE(damageMul == Approx(0.0f));

    // Verify fall distance was passed to Lua
    REQUIRE(lua["test_fall_distance"].get<float>() == Approx(10.0f));
}

TEST_CASE("Entity callback: on_entity_fall_on default returns 1.0", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    // Register a block with no entity callbacks using existing test script
    auto loadResult = engine.loadScript(getTestScriptsDir() / "register_block.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:plain_block"));

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    float result = invoker.invokeOnEntityFallOn(def, {0, 0, 0}, entity, 5.0f);
    REQUIRE(result == Approx(1.0f));
}

// ============================================================================
// on_entity_step_on — fires on landing
// ============================================================================

TEST_CASE("Entity callback: on_entity_step_on fires with correct position", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_step_on.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:pressure_plate"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onEntityStepOn.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    invoker.invokeOnEntityStepOn(def, {7, 63, 9}, entity);

    REQUIRE(lua["test_stepped_on"].get<bool>() == true);
    REQUIRE(lua["test_step_pos_x"].get<int>() == 7);
    REQUIRE(lua["test_step_pos_y"].get<int>() == 63);
    REQUIRE(lua["test_step_pos_z"].get<int>() == 9);
}

// ============================================================================
// on_entity_collide — fires with facing and velocity
// ============================================================================

TEST_CASE("Entity callback: on_entity_collide passes facing and velocity", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_collide.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:bumper"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onEntityCollide.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    invoker.invokeOnEntityCollide(def, {4, 64, 4}, entity, "east", {5.0f, -9.8f, 0.0f}, true);

    REQUIRE(lua["test_collide_facing"].get<std::string>() == "east");
    REQUIRE(lua["test_collide_is_impact"].get<bool>() == true);
    REQUIRE(lua["test_collide_vel_y"].get<float>() == Approx(-9.8f));
}

// ============================================================================
// on_projectile_hit — stub field stored but never invoked
// ============================================================================

TEST_CASE("Entity callback: on_projectile_hit field stored (stub)", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_projectile_stub.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:target_block"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onProjectileHit.has_value());

    // Verify the callback is NOT invoked — no invoker method exists for V1
    // The field is stored but the engine never calls it
    auto& lua = engine.getLuaState();
    REQUIRE_FALSE(lua["test_projectile_hit"].valid());
}

// ============================================================================
// EntityHandle methods
// ============================================================================

TEST_CASE("EntityHandle: damage logs without crash", "[scripting][entity]")
{
    voxel::core::Log::init();
    voxel::game::PlayerController player;
    EntityHandle entity(player);

    // Should not crash — V1 just logs
    entity.damage(5.0f);
}

TEST_CASE("EntityHandle: set_velocity modifies player velocity", "[scripting][entity]")
{
    voxel::game::PlayerController player;
    EntityHandle entity(player);

    entity.setVelocity({1.0f, 2.0f, 3.0f});
    auto vel = player.getVelocity();
    REQUIRE(vel.x == Approx(1.0f));
    REQUIRE(vel.y == Approx(2.0f));
    REQUIRE(vel.z == Approx(3.0f));
}

// ============================================================================
// Missing callbacks return safe defaults
// ============================================================================

TEST_CASE("Entity callback: missing callbacks return safe defaults", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "register_block.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:plain_block"));

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    voxel::game::PlayerController player;
    EntityHandle entity(player);

    // These should all return safe defaults without crashing
    invoker.invokeOnEntityInside(def, {0, 0, 0}, entity);
    invoker.invokeOnEntityStepOn(def, {0, 0, 0}, entity);
    float fallResult = invoker.invokeOnEntityFallOn(def, {0, 0, 0}, entity, 5.0f);
    REQUIRE(fallResult == Approx(1.0f));
    invoker.invokeOnEntityCollide(def, {0, 0, 0}, entity, "up", {0.0f, 0.0f, 0.0f}, false);
}

// ============================================================================
// categoryMask includes entity bit
// ============================================================================

TEST_CASE("Entity callback: categoryMask includes bit 7 for entity callbacks", "[scripting][entity]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "entity_fall_on.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:slime_block"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE((def.callbacks->categoryMask() & 0x80) != 0);
}
