#include "voxel/core/Log.h"
#include "voxel/renderer/ParticleManager.h"
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

// ============================================================================
// on_animate_tick — fires with pos and random function
// ============================================================================

TEST_CASE("Visual callback: on_animate_tick fires with pos and random", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_animate_tick.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:fire"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onAnimateTick.has_value());
    REQUIRE((def.callbacks->categoryMask() & 0x200) != 0);

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    sol::object randomFn = lua["math"]["random"];
    invoker.invokeOnAnimateTick(def, {5, 70, 10}, randomFn);

    REQUIRE(lua["test_animate_called"].get<bool>() == true);
    REQUIRE(lua["test_animate_pos_x"].get<int>() == 5);
    REQUIRE(lua["test_random_works"].get<bool>() == true);
}

// ============================================================================
// get_color — returns tint value based on position
// ============================================================================

TEST_CASE("Visual callback: get_color returns tint value", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_get_color.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:tinted_grass"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->getColor.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    // High altitude → bright green
    auto color1 = invoker.invokeGetColor(def, {0, 100, 0});
    REQUIRE(color1.has_value());
    REQUIRE(color1.value() == 0x00FF00);

    // Low altitude → dark green
    auto color2 = invoker.invokeGetColor(def, {0, 32, 0});
    REQUIRE(color2.has_value());
    REQUIRE(color2.value() == 0x006600);
}

TEST_CASE("Visual callback: get_color returns nullopt for block without callback", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_animate_tick.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:fire"));
    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    auto color = invoker.invokeGetColor(def, {0, 0, 0});
    REQUIRE_FALSE(color.has_value());
}

// ============================================================================
// on_pick_block — returns custom item ID
// ============================================================================

TEST_CASE("Visual callback: on_pick_block returns custom item", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_pick_block.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:double_slab"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onPickBlock.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    std::string result = invoker.invokeOnPickBlock(def, {10, 64, 20});
    REQUIRE(result == "test:single_slab");
}

TEST_CASE("Visual callback: on_pick_block defaults to stringId when no callback", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_animate_tick.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:fire"));
    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    std::string result = invoker.invokeOnPickBlock(def, {0, 0, 0});
    REQUIRE(result == "test:fire");
}

// ============================================================================
// categoryMask — Bit 9 (0x200) set for visual callbacks
// ============================================================================

TEST_CASE("Visual callback: categoryMask includes Bit 9 for visual", "[scripting][visual]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "visual_get_color.lua");
    REQUIRE(loadResult.has_value());

    const auto& def = registry.getBlockType(registry.getIdByName("test:tinted_grass"));
    REQUIRE(def.callbacks != nullptr);
    REQUIRE((def.callbacks->categoryMask() & 0x200) != 0);
}

// ============================================================================
// ParticleManager — CPU-side simulation
// ============================================================================

TEST_CASE("ParticleManager: addParticle and update", "[renderer][particle]")
{
    // Test CPU-only behavior (no GPU init needed)
    voxel::renderer::ParticleManager pm;

    voxel::renderer::Particle p;
    p.pos = {0.0f, 10.0f, 0.0f};
    p.velocity = {1.0f, 0.0f, 0.0f};
    p.acceleration = {0.0f, -10.0f, 0.0f};
    p.lifetime = 2.0f;
    p.maxLifetime = 2.0f;

    pm.addParticle(p);
    REQUIRE(pm.getActiveCount() == 1);

    // Update 1 second
    pm.update(1.0f);
    REQUIRE(pm.getActiveCount() == 1);

    // Update another 1.5 seconds — lifetime exceeded
    pm.update(1.5f);
    REQUIRE(pm.getActiveCount() == 0);
}

TEST_CASE("ParticleManager: budget eviction at MAX_PARTICLES", "[renderer][particle]")
{
    voxel::renderer::ParticleManager pm;

    // Fill to capacity
    for (uint32_t i = 0; i < voxel::renderer::ParticleManager::MAX_PARTICLES; ++i)
    {
        voxel::renderer::Particle p;
        p.lifetime = 10.0f;
        p.maxLifetime = 10.0f;
        pm.addParticle(p);
    }
    REQUIRE(pm.getActiveCount() == voxel::renderer::ParticleManager::MAX_PARTICLES);

    // Adding one more should evict oldest
    voxel::renderer::Particle extra;
    extra.lifetime = 10.0f;
    extra.maxLifetime = 10.0f;
    pm.addParticle(extra);
    REQUIRE(pm.getActiveCount() == voxel::renderer::ParticleManager::MAX_PARTICLES);
}

TEST_CASE("ParticleManager: addParticleSpawner creates multiple particles", "[renderer][particle]")
{
    voxel::renderer::ParticleManager pm;

    pm.addParticleSpawner(
        50,
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 0.0f, -1.0f},
        {1.0f, 2.0f, 1.0f},
        0, 0.1f, 2.0f);

    REQUIRE(pm.getActiveCount() == 50);
}
