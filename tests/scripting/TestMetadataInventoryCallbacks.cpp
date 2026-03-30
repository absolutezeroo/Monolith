#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockInventory.h"
#include "voxel/world/BlockMetadata.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ItemStack.h"

#include <sol/sol.hpp>

#include <catch2/catch_approx.hpp>
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

static void setupEngine(ScriptEngine& engine, BlockRegistry& registry, ChunkManager& chunkMgr)
{
    voxel::core::Log::init();
    auto initResult = engine.init();
    REQUIRE(initResult.has_value());
    engine.addAllowedPath(getTestScriptsDir());
    LuaBindings::registerBlockAPI(engine.getLuaState(), registry);
    LuaBindings::registerEntityAPI(engine.getLuaState());
    LuaBindings::registerMetadataAPI(engine.getLuaState(), chunkMgr);
}

// ============================================================================
// Metadata callbacks via on_construct
// ============================================================================

TEST_CASE("Metadata: on_construct sets metadata via voxel.get_meta", "[scripting][metadata]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "metadata_basic.lua");
    REQUIRE(loadResult.has_value());

    uint16_t signId = registry.getIdByName("test:sign");
    REQUIRE(signId != 0);
    const auto& def = registry.getBlockType(signId);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onConstruct.has_value());

    // Place the block at a position within a loaded chunk
    glm::ivec3 pos{0, 64, 0};
    glm::ivec2 chunkCoord = worldToChunkCoord(pos);
    chunkMgr.loadChunk(chunkCoord);

    // Invoke on_construct which calls voxel.get_meta(pos)
    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);
    invoker.invokeOnConstruct(def, pos);

    // Verify Lua flag was set
    REQUIRE(lua["test_meta_constructed"].get<bool>() == true);

    // Verify metadata was set on the chunk column
    auto* column = chunkMgr.getChunk(chunkCoord);
    REQUIRE(column != nullptr);

    glm::ivec3 local = worldToLocalPos(pos);
    auto* meta = column->getMetadata(local.x, local.y, local.z);
    REQUIRE(meta != nullptr);
    REQUIRE(meta->getString("text") == "Hello");
    REQUIRE(meta->getInt("line_count") == 4);
    float scaleVal = meta->getFloat("scale");
    REQUIRE(scaleVal == Catch::Approx(1.5f));
}

// ============================================================================
// Inventory: allow_inventory_put returns 0 (deny)
// ============================================================================

TEST_CASE("Inventory: allow_inventory_put returns 0 to deny", "[scripting][inventory]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "inventory_deny.lua");
    REQUIRE(loadResult.has_value());

    uint16_t lockedId = registry.getIdByName("test:locked_chest");
    REQUIRE(lockedId != 0);
    const auto& def = registry.getBlockType(lockedId);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->allowInventoryPut.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    ItemStack stone("base:stone", 64);
    int allowed = invoker.invokeAllowInventoryPut(def, {0, 64, 0}, "main", 0, stone, 1);
    REQUIRE(allowed == 0);

    // Also test take is denied
    int takeAllowed = invoker.invokeAllowInventoryTake(def, {0, 64, 0}, "main", 0, stone, 1);
    REQUIRE(takeAllowed == 0);
}

// ============================================================================
// Inventory: allow_inventory_put returns requested count (allow)
// ============================================================================

TEST_CASE("Inventory: allow_inventory_put returns stack count to allow", "[scripting][inventory]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "inventory_chest.lua");
    REQUIRE(loadResult.has_value());

    uint16_t chestId = registry.getIdByName("test:chest");
    REQUIRE(chestId != 0);
    const auto& def = registry.getBlockType(chestId);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->allowInventoryPut.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    ItemStack stone("base:stone", 64);
    int allowed = invoker.invokeAllowInventoryPut(def, {0, 64, 0}, "main", 0, stone, 1);
    REQUIRE(allowed == 64);
}

// ============================================================================
// Inventory: on_inventory_put notification fires
// ============================================================================

TEST_CASE("Inventory: on_inventory_put notification fires", "[scripting][inventory]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "inventory_chest.lua");
    REQUIRE(loadResult.has_value());

    uint16_t chestId = registry.getIdByName("test:chest");
    const auto& def = registry.getBlockType(chestId);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onInventoryPut.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    ItemStack stone("base:stone", 32);
    invoker.invokeOnInventoryPut(def, {0, 64, 0}, "main", 0, stone, 1);

    REQUIRE(lua["test_put_fired"].get<bool>() == true);
    REQUIRE(lua["test_put_item"].get<std::string>() == "base:stone");
}

// ============================================================================
// Inventory: on_inventory_move notification fires
// ============================================================================

TEST_CASE("Inventory: on_inventory_move notification fires", "[scripting][inventory]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    auto loadResult = engine.loadScript(getTestScriptsDir() / "inventory_notify.lua");
    REQUIRE(loadResult.has_value());

    uint16_t hopperId = registry.getIdByName("test:hopper");
    const auto& def = registry.getBlockType(hopperId);
    REQUIRE(def.callbacks != nullptr);
    REQUIRE(def.callbacks->onInventoryMove.has_value());

    auto& lua = engine.getLuaState();
    BlockCallbackInvoker invoker(lua, registry);

    invoker.invokeOnInventoryMove(def, {0, 64, 0}, "input", 0, "output", 0, 5, 1);

    REQUIRE(lua["test_move_from"].get<std::string>() == "input");
    REQUIRE(lua["test_move_to"].get<std::string>() == "output");
    REQUIRE(lua["test_move_count"].get<int>() == 5);
}

// ============================================================================
// Inventory: default behavior when no callback set
// ============================================================================

TEST_CASE("Inventory: no callback returns default (allow all)", "[scripting][inventory]")
{
    ScriptEngine engine;
    BlockRegistry registry;
    ChunkManager chunkMgr;
    setupEngine(engine, registry, chunkMgr);

    // Register a block with NO inventory callbacks
    auto& lua = engine.getLuaState();
    lua.safe_script(R"(
        voxel.register_block({
            id = "test:plain_block",
        })
    )");

    uint16_t blockId = registry.getIdByName("test:plain_block");
    REQUIRE(blockId != 0);
    const auto& def = registry.getBlockType(blockId);

    BlockCallbackInvoker invoker(lua, registry);

    ItemStack stack("base:stone", 16);
    int allowed = invoker.invokeAllowInventoryPut(def, {0, 0, 0}, "main", 0, stack, 1);
    REQUIRE(allowed == 16); // Default: allow all

    int takeAllowed = invoker.invokeAllowInventoryTake(def, {0, 0, 0}, "main", 0, stack, 1);
    REQUIRE(takeAllowed == 16);

    int moveAllowed = invoker.invokeAllowInventoryMove(def, {0, 0, 0}, "a", 0, "b", 0, 8, 1);
    REQUIRE(moveAllowed == 8);
}
