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

static std::filesystem::path getBaseScriptsDir()
{
    return std::filesystem::path(VOXELFORGE_ASSETS_DIR) / "scripts";
}

/// Helper: init engine + registry. Call at start of each test.
static void setupEngine(ScriptEngine& engine, BlockRegistry& registry)
{
    voxel::core::Log::init();
    auto initResult = engine.init();
    REQUIRE(initResult.has_value());
    engine.addAllowedPath(getTestScriptsDir());
    engine.addAllowedPath(getBaseScriptsDir());
    LuaBindings::registerBlockAPI(engine.getLuaState(), registry);
}

static void loadTestScript(ScriptEngine& engine, const std::string& filename)
{
    auto result = engine.loadScript(getTestScriptsDir() / filename);
    REQUIRE(result.has_value());
}

// ============================================================================
// Task 1: voxel.register_block basic registration
// ============================================================================

TEST_CASE("LuaBindings: register_block creates block in registry", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    uint16_t id = registry.getIdByName("test:simple_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    CHECK(def.stringId == "test:simple_block");
    CHECK(def.isSolid == true);
    CHECK(def.hasCollision == true);
    CHECK(def.hardness == 2.0f);
    CHECK(def.dropItem == "test:simple_block");
    CHECK(def.lightFilter == 15);
}

TEST_CASE("LuaBindings: register_block sets texture indices", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    uint16_t id = registry.getIdByName("test:simple_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    for (int i = 0; i < 6; ++i)
    {
        CHECK(def.textureIndices[i] == 1);
    }
}

TEST_CASE("LuaBindings: register_block sets groups", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    uint16_t id = registry.getIdByName("test:simple_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.groups.contains("cracky"));
    CHECK(def.groups.at("cracky") == 2);
    REQUIRE(def.groups.contains("stone"));
    CHECK(def.groups.at("stone") == 1);
}

TEST_CASE("LuaBindings: parseBlockDefinition rejects missing id", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    auto& lua = engine.getLuaState();
    sol::table noId = lua.create_table_with("solid", true);

    auto result = LuaBindings::parseBlockDefinition(noId);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == voxel::core::ErrorCode::ScriptError);
}

TEST_CASE("LuaBindings: parseBlockDefinition rejects invalid namespace", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    auto& lua = engine.getLuaState();

    SECTION("no colon")
    {
        sol::table t = lua.create_table_with("id", "nocolon");
        auto result = LuaBindings::parseBlockDefinition(t);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("empty namespace")
    {
        sol::table t = lua.create_table_with("id", ":badname");
        auto result = LuaBindings::parseBlockDefinition(t);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("multiple colons")
    {
        sol::table t = lua.create_table_with("id", "a:b:c");
        auto result = LuaBindings::parseBlockDefinition(t);
        REQUIRE_FALSE(result.has_value());
    }
}

// ============================================================================
// Task 2: Block definition defaults
// ============================================================================

TEST_CASE("LuaBindings: parseBlockDefinition applies correct defaults", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    auto& lua = engine.getLuaState();
    sol::table t = lua.create_table_with("id", "test:defaults");

    auto result = LuaBindings::parseBlockDefinition(t);
    REQUIRE(result.has_value());

    const auto& def = *result;
    CHECK(def.isSolid == true);
    CHECK(def.isTransparent == false);
    CHECK(def.hasCollision == true);
    CHECK(def.lightEmission == 0);
    CHECK(def.lightFilter == 0);
    CHECK(def.hardness == 1.0f);
    CHECK(def.renderType == RenderType::Opaque);
    CHECK(def.modelType == ModelType::FullCube);
    CHECK(def.callbacks == nullptr);
}

// ============================================================================
// Task 3: Callback extraction
// ============================================================================

TEST_CASE("LuaBindings: register_block extracts callbacks", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block_with_callbacks.lua");

    uint16_t id = registry.getIdByName("test:callback_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    REQUIRE(def.callbacks != nullptr);

    CHECK(def.callbacks->onConstruct.has_value());
    CHECK(def.callbacks->onDestruct.has_value());
    CHECK(def.callbacks->afterPlace.has_value());
    CHECK(def.callbacks->afterDestruct.has_value());
    CHECK(def.callbacks->afterDig.has_value());

    CHECK_FALSE(def.callbacks->canPlace.has_value());
    CHECK_FALSE(def.callbacks->canDig.has_value());
    CHECK_FALSE(def.callbacks->onPlace.has_value());
}

TEST_CASE("LuaBindings: block without callbacks has nullptr", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    uint16_t id = registry.getIdByName("test:simple_block");
    REQUIRE(id != BLOCK_AIR);

    const auto& def = registry.getBlockType(id);
    CHECK(def.callbacks == nullptr);
}

// ============================================================================
// Task 4: BlockCallbackInvoker
// ============================================================================

TEST_CASE("BlockCallbackInvoker: invokeOnConstruct calls Lua function", "[scripting][callbacks]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block_with_callbacks.lua");

    BlockCallbackInvoker invoker(engine.getLuaState(), registry);

    uint16_t id = registry.getIdByName("test:callback_block");
    const auto& def = registry.getBlockType(id);

    invoker.invokeOnConstruct(def, glm::ivec3{10, 20, 30});

    auto& lua = engine.getLuaState();
    sol::table log = lua["callback_log"];
    REQUIRE(log.valid());

    std::string entry = log[1];
    CHECK(entry == "on_construct:10,20,30");
}

TEST_CASE("BlockCallbackInvoker: invokeOnDestruct and afterDestruct chain", "[scripting][callbacks]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block_with_callbacks.lua");

    BlockCallbackInvoker invoker(engine.getLuaState(), registry);

    uint16_t id = registry.getIdByName("test:callback_block");
    const auto& def = registry.getBlockType(id);

    invoker.invokeOnDestruct(def, glm::ivec3{5, 6, 7});
    invoker.invokeAfterDestruct(def, glm::ivec3{5, 6, 7}, id);

    auto& lua = engine.getLuaState();
    sol::table log = lua["callback_log"];
    REQUIRE(log.valid());
    REQUIRE(log.size() >= 2);

    std::string entry1 = log[1];
    std::string entry2 = log[2];
    CHECK(entry1 == "on_destruct:5,6,7");
    CHECK(entry2 == "after_destruct:5,6,7");
}

TEST_CASE("BlockCallbackInvoker: invokeCanDig returns false for unbreakable block", "[scripting][callbacks]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block_can_dig_false.lua");

    BlockCallbackInvoker invoker(engine.getLuaState(), registry);

    uint16_t id = registry.getIdByName("test:unbreakable");
    const auto& def = registry.getBlockType(id);

    bool result = invoker.invokeCanDig(def, glm::ivec3{0, 0, 0}, 1);
    CHECK_FALSE(result);
}

TEST_CASE("BlockCallbackInvoker: invokeCanPlace returns false for restricted block", "[scripting][callbacks]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block_can_place_false.lua");

    BlockCallbackInvoker invoker(engine.getLuaState(), registry);

    uint16_t id = registry.getIdByName("test:no_place");
    const auto& def = registry.getBlockType(id);

    bool result = invoker.invokeCanPlace(def, glm::ivec3{0, 0, 0}, 1);
    CHECK_FALSE(result);
}

TEST_CASE("BlockCallbackInvoker: no-op when callbacks are nullptr", "[scripting][callbacks]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    loadTestScript(engine, "register_block.lua");

    BlockCallbackInvoker invoker(engine.getLuaState(), registry);

    uint16_t id = registry.getIdByName("test:simple_block");
    const auto& def = registry.getBlockType(id);

    CHECK(invoker.invokeCanDig(def, glm::ivec3{0, 0, 0}, 1) == true);
    CHECK(invoker.invokeCanPlace(def, glm::ivec3{0, 0, 0}, 1) == true);
    invoker.invokeOnConstruct(def, glm::ivec3{0, 0, 0});
    invoker.invokeOnDestruct(def, glm::ivec3{0, 0, 0});
    invoker.invokeAfterPlace(def, glm::ivec3{0, 0, 0}, 1);
    invoker.invokeAfterDestruct(def, glm::ivec3{0, 0, 0}, 0);
    invoker.invokeAfterDig(def, glm::ivec3{0, 0, 0}, 0, 1);
    invoker.invokeOnPlace(def, glm::ivec3{0, 0, 0}, 1);
}

// ============================================================================
// Task 6: init.lua integration — all 29 base blocks register correctly
// ============================================================================

TEST_CASE("LuaBindings: init.lua registers all 29 base blocks", "[scripting][integration]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto result = engine.loadScript(getBaseScriptsDir() / "base" / "init.lua");
    REQUIRE(result.has_value());

    static const char* EXPECTED_BLOCKS[] = {
        "base:stone",     "base:dirt",          "base:grass_block",  "base:sand",
        "base:water",     "base:oak_log",       "base:oak_leaves",   "base:glass",
        "base:glowstone", "base:bedrock",       "base:sandstone",    "base:snow_block",
        "base:torch",     "base:birch_log",     "base:birch_leaves", "base:spruce_log",
        "base:spruce_leaves", "base:jungle_log", "base:jungle_leaves", "base:cactus",
        "base:tall_grass", "base:flower_red",   "base:flower_yellow", "base:dead_bush",
        "base:snow_layer", "base:coal_ore",     "base:iron_ore",     "base:gold_ore",
        "base:diamond_ore",
    };

    for (const char* name : EXPECTED_BLOCKS)
    {
        uint16_t id = registry.getIdByName(name);
        INFO("Block: " << name);
        CHECK(id != BLOCK_AIR);
    }
}

TEST_CASE("LuaBindings: init.lua stone has correct properties", "[scripting][integration]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto result = engine.loadScript(getBaseScriptsDir() / "base" / "init.lua");
    REQUIRE(result.has_value());

    uint16_t stoneId = registry.getIdByName("base:stone");
    REQUIRE(stoneId != BLOCK_AIR);

    const auto& stone = registry.getBlockType(stoneId);
    CHECK(stone.isSolid == true);
    CHECK(stone.hasCollision == true);
    CHECK(stone.hardness == 1.5f);
    CHECK(stone.lightFilter == 15);
    CHECK(stone.dropItem == "base:cobblestone");
    CHECK(stone.textureIndices[0] == 1);
    REQUIRE(stone.groups.contains("cracky"));
    CHECK(stone.groups.at("cracky") == 3);
}

TEST_CASE("LuaBindings: init.lua water has liquid properties", "[scripting][integration]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto result = engine.loadScript(getBaseScriptsDir() / "base" / "init.lua");
    REQUIRE(result.has_value());

    uint16_t waterId = registry.getIdByName("base:water");
    REQUIRE(waterId != BLOCK_AIR);

    const auto& water = registry.getBlockType(waterId);
    CHECK(water.isSolid == false);
    CHECK(water.isTransparent == true);
    CHECK(water.hasCollision == false);
    CHECK(water.renderType == RenderType::Translucent);
    CHECK(water.isReplaceable == true);
    CHECK(water.moveResistance == 3);
    CHECK(water.drowning == 1);
    CHECK(water.tintIndex == 3);
    CHECK(water.liquidType == LiquidType::Source);
    CHECK(water.liquidRange == 8);
    CHECK(water.liquidRenewable == true);
}

TEST_CASE("LuaBindings: init.lua grass_block has per-face textures", "[scripting][integration]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);

    auto result = engine.loadScript(getBaseScriptsDir() / "base" / "init.lua");
    REQUIRE(result.has_value());

    uint16_t grassId = registry.getIdByName("base:grass_block");
    REQUIRE(grassId != BLOCK_AIR);

    const auto& grass = registry.getBlockType(grassId);
    CHECK(grass.textureIndices[0] == 4);
    CHECK(grass.textureIndices[1] == 4);
    CHECK(grass.textureIndices[2] == 3);
    CHECK(grass.textureIndices[3] == 2);
    CHECK(grass.textureIndices[4] == 4);
    CHECK(grass.textureIndices[5] == 4);
    CHECK(grass.tintIndex == 1);
}

// ============================================================================
// Task 8: register_item stub
// ============================================================================

TEST_CASE("LuaBindings: register_item stores item in registry", "[scripting][lua_bindings]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    setupEngine(engine, registry);
    auto& lua = engine.getLuaState();

    auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
        return pfr;
    };
    auto res = lua.safe_script(
        R"(
        voxel.register_item({
            id = "test:pickaxe",
            stack_size = 1,
        })
    )",
        errorHandler);
    REQUIRE(res.valid());

    const auto& items = LuaBindings::getItemRegistry();
    REQUIRE(items.contains("test:pickaxe"));
    CHECK(items.at("test:pickaxe").stackSize == 1);
}
