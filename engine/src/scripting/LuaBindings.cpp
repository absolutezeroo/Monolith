#include "voxel/scripting/LuaBindings.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/world/BlockRegistry.h"

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
        def.callbacks = std::move(cbs);
    }

    return def;
}

const std::unordered_map<std::string, ItemDefinition>& LuaBindings::getItemRegistry()
{
    return s_itemRegistry;
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

} // namespace voxel::scripting
