#include "voxel/scripting/LuaBindings.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/ABMRegistry.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/BlockTimerManager.h"
#include "voxel/scripting/EntityHandle.h"
#include "voxel/scripting/LBMRegistry.h"
#include "voxel/world/BlockInventory.h"
#include "voxel/world/BlockMetadata.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ItemStack.h"

#include <sol/sol.hpp>

namespace voxel::scripting
{

std::unordered_map<std::string, ItemDefinition> LuaBindings::s_itemRegistry;

void LuaBindings::registerBlockAPI(sol::state& lua, world::BlockRegistry& registry)
{
    sol::table voxelTable = lua["voxel"];

    voxelTable.set_function("register_block", [&lua, &registry](sol::table table) {
        auto result = LuaBindings::parseBlockDefinition(table);
        if (!result.has_value())
        {
            VX_LOG_ERROR("voxel.register_block failed: {}", result.error().message);
            return;
        }

        std::string blockName = result->stringId;
        auto regResult = registry.registerBlock(std::move(*result));
        if (!regResult.has_value())
        {
            VX_LOG_ERROR("voxel.register_block: registration failed for '{}': {}", blockName, regResult.error().message);
            return;
        }

        VX_LOG_DEBUG("Registered block '{}' (base state ID: {})", blockName, *regResult);
    });

    voxelTable.set_function("register_item", [](sol::table table) {
        auto id = table.get<std::optional<std::string>>("id");
        if (!id.has_value() || id->empty())
        {
            VX_LOG_ERROR("voxel.register_item: 'id' field is required");
            return;
        }

        // Validate namespace
        auto colon = id->find(':');
        if (colon == std::string::npos || colon == 0 || colon == id->size() - 1)
        {
            VX_LOG_ERROR("voxel.register_item: invalid namespace format '{}'", *id);
            return;
        }

        if (s_itemRegistry.contains(*id))
        {
            VX_LOG_ERROR("voxel.register_item: duplicate item ID '{}'", *id);
            return;
        }

        ItemDefinition item;
        item.id = std::move(*id);
        item.stackSize = table.get_or("stack_size", 64);
        item.block = table.get_or<std::string>("block", "");

        VX_LOG_DEBUG("Registered item '{}'", item.id);
        s_itemRegistry[item.id] = std::move(item);
    });
}

core::Result<world::BlockDefinition> LuaBindings::parseBlockDefinition(const sol::table& table)
{
    world::BlockDefinition def;

    // --- Required: id ---
    auto id = table.get<std::optional<std::string>>("id");
    if (!id.has_value() || id->empty())
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::ScriptError, "voxel.register_block: 'id' field is required"});
    }

    // Validate namespace format
    auto colon = id->find(':');
    if (colon == std::string::npos || colon == 0 || colon == id->size() - 1)
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::ScriptError, "voxel.register_block: invalid namespace format '" + *id + "'"});
    }
    // Ensure only one colon
    if (id->find(':', colon + 1) != std::string::npos)
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::ScriptError, "voxel.register_block: multiple colons in '" + *id + "'"});
    }

    def.stringId = std::move(*id);

    // --- Core properties ---
    def.isSolid = table.get_or("solid", true);
    def.isTransparent = table.get_or("transparent", false);
    def.hasCollision = table.get_or("has_collision", true);
    def.lightEmission = static_cast<uint8_t>(table.get_or("light_emission", 0));
    def.lightFilter = static_cast<uint8_t>(table.get_or("light_filter", 0));
    def.hardness = table.get_or("hardness", 1.0f);

    // --- Rendering ---
    auto renderTypeStr = table.get_or<std::string>("render_type", "opaque");
    def.renderType = parseRenderType(renderTypeStr);

    auto modelTypeStr = table.get_or<std::string>("model_type", "full_cube");
    def.modelType = parseModelType(modelTypeStr);

    auto tintStr = table.get_or<std::string>("tint", "none");
    def.tintIndex = parseTintIndex(tintStr);
    // Also accept numeric tint_index directly
    if (tintStr == "none")
    {
        def.tintIndex = static_cast<uint8_t>(table.get_or("tint_index", 0));
    }

    def.waving = static_cast<uint8_t>(table.get_or("waving", 0));

    // --- Physics / interaction ---
    def.isClimbable = table.get_or("climbable", false);
    def.moveResistance = static_cast<uint8_t>(table.get_or("move_resistance", 0));
    def.damagePerSecond = static_cast<uint32_t>(table.get_or("damage_per_second", 0));
    def.drowning = static_cast<uint8_t>(table.get_or("drowning", 0));
    def.isBuildableTo = table.get_or("buildable_to", false);
    def.isFloodable = table.get_or("floodable", false);
    def.isReplaceable = table.get_or("replaceable", false);

    // --- Drop ---
    def.dropItem = table.get_or<std::string>("drop", "");

    // --- Visual effects ---
    def.postEffectColor = static_cast<uint32_t>(table.get_or("post_effect_color", 0));

    // --- Mechanical ---
    auto pushStr = table.get_or<std::string>("push_reaction", "normal");
    def.pushReaction = parsePushReaction(pushStr);
    def.isFallingBlock = table.get_or("falling", false);

    // --- Signal stubs ---
    def.powerOutput = static_cast<uint8_t>(table.get_or("power_output", 0));
    def.isPowerSource = table.get_or("is_power_source", false);
    def.isPowerConductor = table.get_or("is_power_conductor", true);

    // --- Textures ---
    // Support direct numeric indices via texture_indices = {1,1,1,1,1,1}
    auto texIdxTable = table.get<std::optional<sol::table>>("texture_indices");
    if (texIdxTable.has_value())
    {
        for (int i = 0; i < 6; ++i)
        {
            auto val = (*texIdxTable).get<std::optional<int>>(i + 1); // Lua 1-indexed
            if (val.has_value())
            {
                def.textureIndices[i] = static_cast<uint16_t>(*val);
            }
        }
    }
    else
    {
        // Name-based textures via textures = { all = "stone.png" } etc.
        auto textures = table.get<std::optional<sol::table>>("textures");
        if (textures.has_value())
        {
            parseTextures(*textures, def);
        }
    }

    // --- Groups ---
    auto groups = table.get<std::optional<sol::table>>("groups");
    if (groups.has_value())
    {
        parseGroups(*groups, def);
    }

    // --- Block state properties ---
    auto properties = table.get<std::optional<sol::table>>("properties");
    if (properties.has_value())
    {
        parseProperties(*properties, def);
    }

    // --- Sounds ---
    auto sounds = table.get<std::optional<sol::table>>("sounds");
    if (sounds.has_value())
    {
        parseSounds(*sounds, def);
    }

    // --- Liquid ---
    auto liquid = table.get<std::optional<sol::table>>("liquid");
    if (liquid.has_value())
    {
        parseLiquid(*liquid, def);
    }

    // --- Callbacks (Task 3) ---
    // Extract each callback as optional<sol::protected_function>
    auto extractCallback = [&](const char* name) -> std::optional<sol::protected_function> {
        return table.get<std::optional<sol::protected_function>>(name);
    };

    // Check if any callback is present
    bool hasAnyCallback = false;
    auto checkAndStore =
        [&](const char* name, std::optional<sol::protected_function>& target) {
            target = extractCallback(name);
            if (target.has_value())
            {
                hasAnyCallback = true;
            }
        };

    // Temporary storage for callbacks
    std::optional<sol::protected_function> cbCanPlace;
    std::optional<sol::protected_function> cbGetStateForPlacement;
    std::optional<sol::protected_function> cbOnPlace;
    std::optional<sol::protected_function> cbOnConstruct;
    std::optional<sol::protected_function> cbAfterPlace;
    std::optional<sol::protected_function> cbCanBeReplaced;
    std::optional<sol::protected_function> cbCanDig;
    std::optional<sol::protected_function> cbOnDestruct;
    std::optional<sol::protected_function> cbOnDig;
    std::optional<sol::protected_function> cbAfterDestruct;
    std::optional<sol::protected_function> cbAfterDig;
    std::optional<sol::protected_function> cbOnBlast;
    std::optional<sol::protected_function> cbOnFlood;
    std::optional<sol::protected_function> cbPreserveMetadata;
    std::optional<sol::protected_function> cbGetDrops;
    std::optional<sol::protected_function> cbGetExperience;
    std::optional<sol::protected_function> cbOnDigProgress;

    // Timer callbacks
    std::optional<sol::protected_function> cbOnTimer;

    // Interaction callbacks
    std::optional<sol::protected_function> cbOnRightclick;
    std::optional<sol::protected_function> cbOnPunch;
    std::optional<sol::protected_function> cbOnSecondaryUse;
    std::optional<sol::protected_function> cbOnInteractStart;
    std::optional<sol::protected_function> cbOnInteractStep;
    std::optional<sol::protected_function> cbOnInteractStop;
    std::optional<sol::protected_function> cbOnInteractCancel;

    // Neighbor change callbacks
    std::optional<sol::protected_function> cbOnNeighborChanged;
    std::optional<sol::protected_function> cbUpdateShape;
    std::optional<sol::protected_function> cbCanSurvive;

    // Physics/collision shape callbacks
    std::optional<sol::protected_function> cbGetCollisionShape;
    std::optional<sol::protected_function> cbGetSelectionShape;
    std::optional<sol::protected_function> cbCanAttachAt;
    std::optional<sol::protected_function> cbIsPathfindable;

    // Entity-block interaction callbacks
    std::optional<sol::protected_function> cbOnEntityInside;
    std::optional<sol::protected_function> cbOnEntityStepOn;
    std::optional<sol::protected_function> cbOnEntityFallOn;
    std::optional<sol::protected_function> cbOnEntityCollide;
    std::optional<sol::protected_function> cbOnProjectileHit;

    // Inventory callbacks
    std::optional<sol::protected_function> cbAllowInventoryPut;
    std::optional<sol::protected_function> cbAllowInventoryTake;
    std::optional<sol::protected_function> cbAllowInventoryMove;
    std::optional<sol::protected_function> cbOnInventoryPut;
    std::optional<sol::protected_function> cbOnInventoryTake;
    std::optional<sol::protected_function> cbOnInventoryMove;

    // Signal/power stubs
    std::optional<sol::protected_function> cbOnPowered;
    std::optional<sol::protected_function> cbGetComparatorOutput;
    std::optional<sol::protected_function> cbGetPushReaction;

    checkAndStore("can_place", cbCanPlace);
    checkAndStore("get_state_for_placement", cbGetStateForPlacement);
    checkAndStore("on_place", cbOnPlace);
    checkAndStore("on_construct", cbOnConstruct);
    checkAndStore("after_place", cbAfterPlace);
    checkAndStore("can_be_replaced", cbCanBeReplaced);
    checkAndStore("can_dig", cbCanDig);
    checkAndStore("on_destruct", cbOnDestruct);
    checkAndStore("on_dig", cbOnDig);
    checkAndStore("after_destruct", cbAfterDestruct);
    checkAndStore("after_dig", cbAfterDig);
    checkAndStore("on_blast", cbOnBlast);
    checkAndStore("on_flood", cbOnFlood);
    checkAndStore("preserve_metadata", cbPreserveMetadata);
    checkAndStore("get_drops", cbGetDrops);
    checkAndStore("get_experience", cbGetExperience);
    checkAndStore("on_dig_progress", cbOnDigProgress);
    checkAndStore("on_timer", cbOnTimer);
    checkAndStore("on_rightclick", cbOnRightclick);
    checkAndStore("on_punch", cbOnPunch);
    checkAndStore("on_secondary_use", cbOnSecondaryUse);
    checkAndStore("on_interact_start", cbOnInteractStart);
    checkAndStore("on_interact_step", cbOnInteractStep);
    checkAndStore("on_interact_stop", cbOnInteractStop);
    checkAndStore("on_interact_cancel", cbOnInteractCancel);
    checkAndStore("on_neighbor_changed", cbOnNeighborChanged);
    checkAndStore("update_shape", cbUpdateShape);
    checkAndStore("can_survive", cbCanSurvive);
    checkAndStore("get_collision_shape", cbGetCollisionShape);
    checkAndStore("get_selection_shape", cbGetSelectionShape);
    checkAndStore("can_attach_at", cbCanAttachAt);
    checkAndStore("is_pathfindable", cbIsPathfindable);
    checkAndStore("on_entity_inside", cbOnEntityInside);
    checkAndStore("on_entity_step_on", cbOnEntityStepOn);
    checkAndStore("on_entity_fall_on", cbOnEntityFallOn);
    checkAndStore("on_entity_collide", cbOnEntityCollide);
    checkAndStore("on_projectile_hit", cbOnProjectileHit);
    checkAndStore("allow_inventory_put", cbAllowInventoryPut);
    checkAndStore("allow_inventory_take", cbAllowInventoryTake);
    checkAndStore("allow_inventory_move", cbAllowInventoryMove);
    checkAndStore("on_inventory_put", cbOnInventoryPut);
    checkAndStore("on_inventory_take", cbOnInventoryTake);
    checkAndStore("on_inventory_move", cbOnInventoryMove);
    checkAndStore("on_powered", cbOnPowered);
    checkAndStore("get_comparator_output", cbGetComparatorOutput);
    checkAndStore("get_push_reaction", cbGetPushReaction);

    if (hasAnyCallback)
    {
        world::BlockCallbacksPtr cbs(new BlockCallbacks(), world::BlockCallbacksDeleter{});
        cbs->canPlace = std::move(cbCanPlace);
        cbs->getStateForPlacement = std::move(cbGetStateForPlacement);
        cbs->onPlace = std::move(cbOnPlace);
        cbs->onConstruct = std::move(cbOnConstruct);
        cbs->afterPlace = std::move(cbAfterPlace);
        cbs->canBeReplaced = std::move(cbCanBeReplaced);
        cbs->canDig = std::move(cbCanDig);
        cbs->onDestruct = std::move(cbOnDestruct);
        cbs->onDig = std::move(cbOnDig);
        cbs->afterDestruct = std::move(cbAfterDestruct);
        cbs->afterDig = std::move(cbAfterDig);
        cbs->onBlast = std::move(cbOnBlast);
        cbs->onFlood = std::move(cbOnFlood);
        cbs->preserveMetadata = std::move(cbPreserveMetadata);
        cbs->getDrops = std::move(cbGetDrops);
        cbs->getExperience = std::move(cbGetExperience);
        cbs->onDigProgress = std::move(cbOnDigProgress);
        cbs->onTimer = std::move(cbOnTimer);
        cbs->onRightclick = std::move(cbOnRightclick);
        cbs->onPunch = std::move(cbOnPunch);
        cbs->onSecondaryUse = std::move(cbOnSecondaryUse);
        cbs->onInteractStart = std::move(cbOnInteractStart);
        cbs->onInteractStep = std::move(cbOnInteractStep);
        cbs->onInteractStop = std::move(cbOnInteractStop);
        cbs->onInteractCancel = std::move(cbOnInteractCancel);
        cbs->onNeighborChanged = std::move(cbOnNeighborChanged);
        cbs->updateShape = std::move(cbUpdateShape);
        cbs->canSurvive = std::move(cbCanSurvive);
        cbs->getCollisionShape = std::move(cbGetCollisionShape);
        cbs->getSelectionShape = std::move(cbGetSelectionShape);
        cbs->canAttachAt = std::move(cbCanAttachAt);
        cbs->isPathfindable = std::move(cbIsPathfindable);
        cbs->onEntityInside = std::move(cbOnEntityInside);
        cbs->onEntityStepOn = std::move(cbOnEntityStepOn);
        cbs->onEntityFallOn = std::move(cbOnEntityFallOn);
        cbs->onEntityCollide = std::move(cbOnEntityCollide);
        cbs->onProjectileHit = std::move(cbOnProjectileHit);
        cbs->allowInventoryPut = std::move(cbAllowInventoryPut);
        cbs->allowInventoryTake = std::move(cbAllowInventoryTake);
        cbs->allowInventoryMove = std::move(cbAllowInventoryMove);
        cbs->onInventoryPut = std::move(cbOnInventoryPut);
        cbs->onInventoryTake = std::move(cbOnInventoryTake);
        cbs->onInventoryMove = std::move(cbOnInventoryMove);
        cbs->onPowered = std::move(cbOnPowered);
        cbs->getComparatorOutput = std::move(cbGetComparatorOutput);
        cbs->getPushReaction = std::move(cbGetPushReaction);
        def.callbacks = std::move(cbs);
    }

    return def;
}

const std::unordered_map<std::string, ItemDefinition>& LuaBindings::getItemRegistry()
{
    return s_itemRegistry;
}

static glm::ivec3 tableToPos(const sol::table& t)
{
    return {t.get_or("x", 0), t.get_or("y", 0), t.get_or("z", 0)};
}

static std::vector<std::string> parseStringTable(const sol::table& table)
{
    std::vector<std::string> result;
    table.for_each([&](sol::object, sol::object val) {
        if (val.is<std::string>())
        {
            result.push_back(val.as<std::string>());
        }
    });
    return result;
}

static void resolveNodenames(
    const std::vector<std::string>& names,
    std::unordered_set<uint16_t>& resolved,
    world::BlockRegistry& registry)
{
    for (const auto& name : names)
    {
        if (name.starts_with("group:"))
        {
            std::string groupName = name.substr(6);
            for (uint16_t id = 1; id < registry.blockCount(); ++id)
            {
                const auto& def = registry.getBlockByTypeIndex(id);
                if (def.groups.contains(groupName))
                {
                    resolved.insert(def.baseStateId);
                }
            }
        }
        else
        {
            uint16_t id = registry.getIdByName(name);
            if (id != 0)
            {
                resolved.insert(id);
            }
            else
            {
                VX_LOG_WARN("ABM/LBM nodename '{}' not found in registry", name);
            }
        }
    }
}

void LuaBindings::registerTimerAPI(sol::state& lua, BlockTimerManager& timerMgr)
{
    sol::table voxelTable = lua["voxel"];

    voxelTable.set_function("set_timer", [&timerMgr](const sol::table& posTable, float seconds) {
        glm::ivec3 pos = tableToPos(posTable);
        timerMgr.setTimer(pos, seconds);
    });

    voxelTable.set_function("get_timer", [&lua, &timerMgr](const sol::table& posTable) -> sol::object {
        glm::ivec3 pos = tableToPos(posTable);
        auto remaining = timerMgr.getTimer(pos);
        if (remaining.has_value())
        {
            return sol::make_object(lua, *remaining);
        }
        return sol::make_object(lua, sol::lua_nil);
    });
}

void LuaBindings::registerABMAPI(sol::state& lua, ABMRegistry& abmRegistry, world::BlockRegistry& blockRegistry)
{
    sol::table voxelTable = lua["voxel"];

    voxelTable.set_function("register_abm", [&abmRegistry, &blockRegistry](const sol::table& table) {
        ABMDefinition def;
        def.label = table.get_or<std::string>("label", "unnamed_abm");
        def.interval = table.get_or("interval", 1.0f);
        def.chance = table.get_or("chance", 1);

        // Parse nodenames
        auto nodenames = table.get<std::optional<sol::table>>("nodenames");
        if (!nodenames.has_value())
        {
            VX_LOG_WARN("register_abm '{}': missing 'nodenames'", def.label);
            return;
        }
        def.nodenames = parseStringTable(*nodenames);

        // Parse optional neighbors
        auto neighbors = table.get<std::optional<sol::table>>("neighbors");
        if (neighbors.has_value())
        {
            def.neighbors = parseStringTable(*neighbors);
            def.hasNeighborRequirement = !def.neighbors.empty();
        }

        // Extract action
        auto action = table.get<std::optional<sol::protected_function>>("action");
        if (!action.has_value())
        {
            VX_LOG_WARN("register_abm '{}': missing 'action'", def.label);
            return;
        }
        def.action = std::move(*action);

        // Validate
        if (def.interval <= 0.0f)
        {
            VX_LOG_WARN("register_abm '{}': interval must be > 0", def.label);
            return;
        }
        if (def.chance < 1)
        {
            VX_LOG_WARN("register_abm '{}': chance must be >= 1", def.label);
            return;
        }

        // Resolve nodenames to numeric IDs
        resolveNodenames(def.nodenames, def.resolvedNodenames, blockRegistry);
        if (def.hasNeighborRequirement)
        {
            resolveNodenames(def.neighbors, def.resolvedNeighbors, blockRegistry);
        }

        std::string label = def.label;
        abmRegistry.registerABM(std::move(def));
        VX_LOG_INFO("Registered ABM: '{}'", label);
    });
}

void LuaBindings::registerLBMAPI(sol::state& lua, LBMRegistry& lbmRegistry, world::BlockRegistry& blockRegistry)
{
    sol::table voxelTable = lua["voxel"];

    voxelTable.set_function("register_lbm", [&lbmRegistry, &blockRegistry](const sol::table& table) {
        LBMDefinition def;
        def.label = table.get_or<std::string>("label", "unnamed_lbm");
        def.runAtEveryLoad = table.get_or("run_at_every_load", false);

        // Parse nodenames
        auto nodenames = table.get<std::optional<sol::table>>("nodenames");
        if (!nodenames.has_value())
        {
            VX_LOG_WARN("register_lbm '{}': missing 'nodenames'", def.label);
            return;
        }
        def.nodenames = parseStringTable(*nodenames);

        // Extract action
        auto action = table.get<std::optional<sol::protected_function>>("action");
        if (!action.has_value())
        {
            VX_LOG_WARN("register_lbm '{}': missing 'action'", def.label);
            return;
        }
        def.action = std::move(*action);

        // Resolve nodenames to numeric IDs
        resolveNodenames(def.nodenames, def.resolvedNodenames, blockRegistry);

        std::string label = def.label;
        lbmRegistry.registerLBM(std::move(def));
        VX_LOG_INFO("Registered LBM: '{}'", label);
    });
}

void LuaBindings::registerNeighborAPI(
    sol::state& lua, world::ChunkManager& chunkMgr, world::BlockRegistry& registry)
{
    sol::table voxelTable = lua["voxel"];

    // voxel.get_neighbor_at(pos, face_string) -> {id=string, pos={x,y,z}}
    voxelTable.set_function(
        "get_neighbor_at",
        [&lua, &chunkMgr, &registry](const sol::table& posTable, const std::string& face) -> sol::table {
            glm::ivec3 pos = tableToPos(posTable);

            static const std::unordered_map<std::string, glm::ivec3> FACE_OFFSETS = {
                {"east", {1, 0, 0}},
                {"west", {-1, 0, 0}},
                {"up", {0, 1, 0}},
                {"down", {0, -1, 0}},
                {"south", {0, 0, 1}},
                {"north", {0, 0, -1}},
            };

            auto it = FACE_OFFSETS.find(face);
            if (it == FACE_OFFSETS.end())
            {
                VX_LOG_WARN("get_neighbor_at: unknown face '{}'", face);
                return lua.create_table();
            }

            glm::ivec3 neighborPos = pos + it->second;
            uint16_t blockId = chunkMgr.getBlock(neighborPos);
            const auto& def = registry.getBlockType(blockId);

            sol::table result = lua.create_table();
            result["id"] = def.stringId;
            result["pos"] = lua.create_table_with("x", neighborPos.x, "y", neighborPos.y, "z", neighborPos.z);
            return result;
        });

    // voxel.face_to_direction(face_string) -> {x, y, z}
    voxelTable.set_function("face_to_direction", [&lua](const std::string& face) -> sol::table {
        static const std::unordered_map<std::string, glm::ivec3> FACE_DIRS = {
            {"east", {1, 0, 0}},
            {"west", {-1, 0, 0}},
            {"up", {0, 1, 0}},
            {"down", {0, -1, 0}},
            {"south", {0, 0, 1}},
            {"north", {0, 0, -1}},
        };

        auto it = FACE_DIRS.find(face);
        if (it == FACE_DIRS.end())
        {
            return lua.create_table_with("x", 0, "y", 0, "z", 0);
        }
        return lua.create_table_with("x", it->second.x, "y", it->second.y, "z", it->second.z);
    });
}

void LuaBindings::registerMetadataAPI(sol::state& lua, world::ChunkManager& chunkManager)
{
    sol::table voxelTable = lua["voxel"];

    // Register ItemStack usertype
    lua.new_usertype<world::ItemStack>(
        "ItemStack",
        sol::constructors<world::ItemStack(), world::ItemStack(std::string, uint16_t)>(),
        "get_name", &world::ItemStack::getName,
        "get_count", &world::ItemStack::getCount,
        "set_count", &world::ItemStack::setCount,
        "is_empty", &world::ItemStack::isEmpty);

    // Register BlockMetadata as MetaDataRef usertype
    lua.new_usertype<world::BlockMetadata>(
        "MetaDataRef",
        "set_string", &world::BlockMetadata::setString,
        "get_string", &world::BlockMetadata::getString,
        "set_int", &world::BlockMetadata::setInt,
        "get_int", &world::BlockMetadata::getInt,
        "set_float", &world::BlockMetadata::setFloat,
        "get_float", &world::BlockMetadata::getFloat,
        "contains", &world::BlockMetadata::contains,
        "erase", &world::BlockMetadata::erase);

    // Register BlockInventory as InvRef usertype
    lua.new_usertype<world::BlockInventory>(
        "InvRef",
        "set_size", &world::BlockInventory::setSize,
        "get_size", &world::BlockInventory::getSize,
        "get_stack", &world::BlockInventory::getStack,
        "set_stack", &world::BlockInventory::setStack,
        "is_empty", sol::overload(
            static_cast<bool (world::BlockInventory::*)(const std::string&) const>(&world::BlockInventory::isEmpty),
            static_cast<bool (world::BlockInventory::*)() const>(&world::BlockInventory::isEmpty)));

    // voxel.get_meta(pos) -> MetaDataRef
    voxelTable.set_function("get_meta", [&chunkManager](const sol::table& posTable) -> world::BlockMetadata& {
        int x = posTable.get<int>("x");
        int y = posTable.get<int>("y");
        int z = posTable.get<int>("z");

        glm::ivec2 chunkCoord = world::worldToChunkCoord({x, y, z});
        auto* column = chunkManager.getChunk(chunkCoord);
        if (!column)
        {
            VX_LOG_WARN("voxel.get_meta: chunk not loaded at ({}, {})", chunkCoord.x, chunkCoord.y);
            static world::BlockMetadata s_dummyMeta;
            s_dummyMeta.clear();
            return s_dummyMeta;
        }

        glm::ivec3 local = world::worldToLocalPos({x, y, z});
        return column->getOrCreateMetadata(local.x, local.y, local.z);
    });

    // voxel.get_inventory(pos) -> InvRef
    voxelTable.set_function("get_inventory", [&chunkManager](const sol::table& posTable) -> world::BlockInventory& {
        int x = posTable.get<int>("x");
        int y = posTable.get<int>("y");
        int z = posTable.get<int>("z");

        glm::ivec2 chunkCoord = world::worldToChunkCoord({x, y, z});
        auto* column = chunkManager.getChunk(chunkCoord);
        if (!column)
        {
            VX_LOG_WARN("voxel.get_inventory: chunk not loaded at ({}, {})", chunkCoord.x, chunkCoord.y);
            static world::BlockInventory s_dummyInv;
            return s_dummyInv;
        }

        glm::ivec3 local = world::worldToLocalPos({x, y, z});
        return column->getOrCreateInventory(local.x, local.y, local.z);
    });
}

void LuaBindings::parseTextures(const sol::table& texTable, world::BlockDefinition& def)
{
    // Shorthand: textures = { all = "stone.png" } -> use same index for all faces
    auto allTex = texTable.get<std::optional<std::string>>("all");
    if (allTex.has_value())
    {
        // For now, store texture names as hash-based placeholder indices.
        // Actual texture name→index resolution happens at the TextureArray level
        // when the renderer is available. During registration, we store 0 (fallback).
        // The init.lua migration uses numeric textureIndices directly for base blocks.
        for (int i = 0; i < 6; ++i)
        {
            def.textureIndices[i] = 0;
        }
        return;
    }

    // Per-face: textures = { top = "grass_top.png", bottom = "dirt.png", ... }
    // Face order: [+X, -X, +Y, -Y, +Z, -Z] = [east, west, top, bottom, south, north]
    static constexpr const char* FACE_NAMES[6] = {"east", "west", "top", "bottom", "south", "north"};
    for (int i = 0; i < 6; ++i)
    {
        auto tex = texTable.get<std::optional<std::string>>(FACE_NAMES[i]);
        def.textureIndices[i] = 0; // Fallback to missing texture
    }
}

void LuaBindings::parseGroups(const sol::table& groupsTable, world::BlockDefinition& def)
{
    for (auto& [key, val] : groupsTable)
    {
        if (key.is<std::string>() && val.is<int>())
        {
            def.groups[key.as<std::string>()] = val.as<int>();
        }
    }
}

void LuaBindings::parseProperties(const sol::table& propsTable, world::BlockDefinition& def)
{
    for (auto& [key, val] : propsTable)
    {
        if (!key.is<std::string>() || !val.is<sol::table>())
        {
            continue;
        }

        world::BlockStateProperty prop;
        prop.name = key.as<std::string>();

        sol::table valuesTable = val.as<sol::table>();
        for (auto& [idx, v] : valuesTable)
        {
            if (v.is<std::string>())
            {
                prop.values.push_back(v.as<std::string>());
            }
        }

        if (!prop.values.empty())
        {
            def.properties.push_back(std::move(prop));
        }
    }
}

void LuaBindings::parseSounds(const sol::table& soundsTable, world::BlockDefinition& def)
{
    def.soundFootstep = soundsTable.get_or<std::string>("footstep", "");
    def.soundDig = soundsTable.get_or<std::string>("dig", "");
    def.soundPlace = soundsTable.get_or<std::string>("place", "");
}

void LuaBindings::parseLiquid(const sol::table& liquidTable, world::BlockDefinition& def)
{
    auto typeStr = liquidTable.get_or<std::string>("type", "none");
    def.liquidType = parseLiquidType(typeStr);
    def.liquidViscosity = static_cast<uint8_t>(liquidTable.get_or("viscosity", 0));
    def.liquidRange = static_cast<uint8_t>(liquidTable.get_or("range", 8));
    def.liquidRenewable = liquidTable.get_or("renewable", true);
    def.liquidAlternativeFlowing = liquidTable.get_or<std::string>("alternative_flowing", "");
    def.liquidAlternativeSource = liquidTable.get_or<std::string>("alternative_source", "");
}

world::RenderType LuaBindings::parseRenderType(std::string_view str)
{
    if (str == "cutout")
        return world::RenderType::Cutout;
    if (str == "translucent")
        return world::RenderType::Translucent;
    return world::RenderType::Opaque;
}

world::ModelType LuaBindings::parseModelType(std::string_view str)
{
    if (str == "slab")
        return world::ModelType::Slab;
    if (str == "stair")
        return world::ModelType::Stair;
    if (str == "cross")
        return world::ModelType::Cross;
    if (str == "torch")
        return world::ModelType::Torch;
    if (str == "connected")
        return world::ModelType::Connected;
    if (str == "json_model")
        return world::ModelType::JsonModel;
    if (str == "mesh_model")
        return world::ModelType::MeshModel;
    if (str == "custom")
        return world::ModelType::Custom;
    return world::ModelType::FullCube;
}

world::LiquidType LuaBindings::parseLiquidType(std::string_view str)
{
    if (str == "source")
        return world::LiquidType::Source;
    if (str == "flowing")
        return world::LiquidType::Flowing;
    return world::LiquidType::None;
}

world::PushReaction LuaBindings::parsePushReaction(std::string_view str)
{
    if (str == "destroy")
        return world::PushReaction::Destroy;
    if (str == "block")
        return world::PushReaction::Block;
    return world::PushReaction::Normal;
}

uint8_t LuaBindings::parseTintIndex(std::string_view str)
{
    if (str == "grass")
        return 1;
    if (str == "foliage")
        return 2;
    if (str == "water")
        return 3;
    return 0; // "none"
}

void LuaBindings::registerEntityAPI(sol::state& lua)
{
    lua.new_usertype<EntityHandle>(
        "EntityHandle",
        "damage",
        &EntityHandle::damage,
        "get_velocity",
        [](EntityHandle& e, sol::this_state s) {
            sol::state_view luaView(s);
            auto v = e.getVelocity();
            sol::table t = luaView.create_table();
            t["x"] = v.x;
            t["y"] = v.y;
            t["z"] = v.z;
            return t;
        },
        "get_position",
        [](EntityHandle& e, sol::this_state s) {
            sol::state_view luaView(s);
            auto p = e.getPosition();
            sol::table t = luaView.create_table();
            t["x"] = p.x;
            t["y"] = p.y;
            t["z"] = p.z;
            return t;
        },
        "set_velocity",
        [](EntityHandle& e, const sol::table& t) {
            float x = t.get_or("x", 0.0f);
            float y = t.get_or("y", 0.0f);
            float z = t.get_or("z", 0.0f);
            e.setVelocity({x, y, z});
        });
}

} // namespace voxel::scripting
