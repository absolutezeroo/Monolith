#pragma once

#include "voxel/core/Result.h"
#include "voxel/world/Block.h"

#include <sol/forward.hpp>

#include <string>
#include <unordered_map>

namespace voxel::renderer
{
class ParticleManager;
class TextureArray;
} // namespace voxel::renderer

namespace voxel::world
{
class BlockRegistry;
class ChunkManager;
} // namespace voxel::world

namespace voxel::scripting
{
class BlockTimerManager;
class ABMRegistry;
class LBMRegistry;
}

namespace voxel::scripting
{

/// Minimal V1 item definition — just stores the Lua table fields.
struct ItemDefinition
{
    std::string id;
    int stackSize = 64;
    std::string block; // Associated block ID, if any.
};

/// Binds Lua API functions (voxel.register_block, voxel.register_item) to the engine.
/// Parses Lua tables into BlockDefinition and registers them via BlockRegistry.
class LuaBindings
{
public:
    /// Bind `voxel.register_block` and `voxel.register_item` onto the existing `voxel` table.
    /// @param lua The Lua state (must already have a `voxel` table from ScriptEngine::init).
    /// @param registry The block registry to register blocks into.
    static void registerBlockAPI(sol::state& lua, world::BlockRegistry& registry);

    /// Parse a Lua table into a BlockDefinition. Public for testability.
    /// @param table The Lua table with block properties.
    /// @return The parsed BlockDefinition, or an error if validation fails.
    [[nodiscard]] static core::Result<world::BlockDefinition> parseBlockDefinition(const sol::table& table);

    /// Bind `voxel.set_timer` and `voxel.get_timer` onto the existing `voxel` table.
    static void registerTimerAPI(sol::state& lua, BlockTimerManager& timerMgr);

    /// Bind `voxel.register_abm` onto the existing `voxel` table.
    static void registerABMAPI(sol::state& lua, ABMRegistry& abmRegistry, world::BlockRegistry& blockRegistry);

    /// Bind `voxel.register_lbm` onto the existing `voxel` table.
    static void registerLBMAPI(sol::state& lua, LBMRegistry& lbmRegistry, world::BlockRegistry& blockRegistry);

    /// Bind `voxel.get_neighbor_at` and `voxel.face_to_direction` onto the existing `voxel` table.
    static void registerNeighborAPI(sol::state& lua, world::ChunkManager& chunkMgr, world::BlockRegistry& registry);

    /// Register the EntityHandle usertype so Lua callbacks can interact with entities.
    static void registerEntityAPI(sol::state& lua);

    /// Register ItemStack, MetaDataRef, InvRef usertypes and voxel.get_meta / voxel.get_inventory.
    static void registerMetadataAPI(sol::state& lua, world::ChunkManager& chunkManager);

    /// Bind `voxel.add_particle` and `voxel.add_particle_spawner` onto the existing `voxel` table.
    static void registerParticleAPI(
        sol::state& lua, renderer::ParticleManager& pm, renderer::TextureArray& texArray);

    /// Access the item registry (populated by voxel.register_item calls).
    [[nodiscard]] static const std::unordered_map<std::string, ItemDefinition>& getItemRegistry();

private:
    static void parseTextures(const sol::table& texTable, world::BlockDefinition& def);
    static void parseGroups(const sol::table& groupsTable, world::BlockDefinition& def);
    static void parseProperties(const sol::table& propsTable, world::BlockDefinition& def);
    static void parseSounds(const sol::table& soundsTable, world::BlockDefinition& def);
    static void parseLiquid(const sol::table& liquidTable, world::BlockDefinition& def);

    static world::RenderType parseRenderType(std::string_view str);
    static world::ModelType parseModelType(std::string_view str);
    static world::LiquidType parseLiquidType(std::string_view str);
    static world::PushReaction parsePushReaction(std::string_view str);
    static uint8_t parseTintIndex(std::string_view str);

    static std::unordered_map<std::string, ItemDefinition> s_itemRegistry;
};

} // namespace voxel::scripting
