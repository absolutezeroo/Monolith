#include "voxel/world/BlockRegistry.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>

namespace voxel::world
{

static RenderType parseRenderType(const std::string& s)
{
    if (s == "cutout")
        return RenderType::Cutout;
    if (s == "translucent")
        return RenderType::Translucent;
    return RenderType::Opaque;
}

static ModelType parseModelType(const std::string& s)
{
    if (s == "slab")
        return ModelType::Slab;
    if (s == "stair")
        return ModelType::Stair;
    if (s == "cross")
        return ModelType::Cross;
    if (s == "torch")
        return ModelType::Torch;
    if (s == "connected")
        return ModelType::Connected;
    if (s == "json_model")
        return ModelType::JsonModel;
    if (s == "mesh_model")
        return ModelType::MeshModel;
    if (s == "custom")
        return ModelType::Custom;
    return ModelType::FullCube;
}

static LiquidType parseLiquidType(const std::string& s)
{
    if (s == "source")
        return LiquidType::Source;
    if (s == "flowing")
        return LiquidType::Flowing;
    return LiquidType::None;
}

static PushReaction parsePushReaction(const std::string& s)
{
    if (s == "destroy")
        return PushReaction::Destroy;
    if (s == "block")
        return PushReaction::Block;
    return PushReaction::Normal;
}

BlockRegistry::BlockRegistry()
{
    BlockDefinition air;
    air.stringId = "base:air";
    air.numericId = BLOCK_AIR;
    air.isSolid = false;
    air.isTransparent = true;
    air.hasCollision = false;
    air.lightEmission = 0;
    air.lightFilter = 0;
    air.hardness = 0.0f;

    m_blocks.push_back(std::move(air));
    m_nameToId["base:air"] = BLOCK_AIR;
}

core::Result<uint16_t> BlockRegistry::registerBlock(BlockDefinition def)
{
    if (!isValidNamespace(def.stringId))
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidArgument, "Invalid block namespace: " + def.stringId});
    }

    if (m_nameToId.contains(def.stringId))
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidArgument, "Duplicate block ID: " + def.stringId});
    }

    VX_ASSERT(m_blocks.size() < UINT16_MAX, "Block registry capacity exceeded (max 65535)");
    auto id = static_cast<uint16_t>(m_blocks.size());
    def.numericId = id;

    m_nameToId[def.stringId] = id;
    m_blocks.push_back(std::move(def));

    return id;
}

const BlockDefinition& BlockRegistry::getBlock(uint16_t id) const
{
    VX_ASSERT(id < m_blocks.size(), "Block ID out of range");
    return m_blocks[id];
}

uint16_t BlockRegistry::getIdByName(std::string_view name) const
{
    auto it = m_nameToId.find(std::string(name));
    if (it != m_nameToId.end())
    {
        return it->second;
    }
    return BLOCK_AIR;
}

uint16_t BlockRegistry::blockCount() const
{
    return static_cast<uint16_t>(m_blocks.size());
}

core::Result<uint16_t> BlockRegistry::loadFromJson(const std::filesystem::path& filePath)
{
    if (!std::filesystem::exists(filePath))
    {
        return std::unexpected(core::EngineError::file(filePath.string()));
    }

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return std::unexpected(core::EngineError::file(filePath.string()));
    }

    auto json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_array())
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "Invalid JSON format: " + filePath.string()});
    }

    uint16_t count = 0;
    for (const auto& entry : json)
    {
        if (!entry.is_object())
        {
            continue;
        }

        BlockDefinition def;

        // --- Identity & core ---
        def.stringId = entry.value("stringId", "");
        def.isSolid = entry.value("isSolid", true);
        def.isTransparent = entry.value("isTransparent", false);
        def.hasCollision = entry.value("hasCollision", true);
        def.lightEmission = entry.value("lightEmission", static_cast<uint8_t>(0));
        def.lightFilter = entry.value("lightFilter", static_cast<uint8_t>(0));
        def.hardness = entry.value("hardness", 1.0f);
        def.dropItem = entry.value("dropItem", std::string(""));

        if (entry.contains("textureIndices") && entry["textureIndices"].is_array())
        {
            const auto& texArr = entry["textureIndices"];
            for (size_t i = 0; i < 6 && i < texArr.size(); ++i)
            {
                if (texArr[i].is_number_unsigned())
                {
                    def.textureIndices[i] = texArr[i].get<uint16_t>();
                }
            }
        }

        // --- Rendering ---
        if (entry.contains("renderType") && entry["renderType"].is_string())
            def.renderType = parseRenderType(entry["renderType"].get<std::string>());

        if (entry.contains("modelType") && entry["modelType"].is_string())
            def.modelType = parseModelType(entry["modelType"].get<std::string>());

        def.tintIndex = entry.value("tintIndex", static_cast<uint8_t>(0));
        def.waving = entry.value("waving", static_cast<uint8_t>(0));

        // --- Physics / interaction ---
        def.isClimbable = entry.value("isClimbable", false);
        def.moveResistance = entry.value("moveResistance", static_cast<uint8_t>(0));
        def.damagePerSecond = entry.value("damagePerSecond", static_cast<uint32_t>(0));
        def.drowning = entry.value("drowning", static_cast<uint8_t>(0));
        def.isBuildableTo = entry.value("isBuildableTo", false);
        def.isFloodable = entry.value("isFloodable", false);
        def.isReplaceable = entry.value("isReplaceable", false);

        // --- Groups ---
        if (entry.contains("groups") && entry["groups"].is_object())
        {
            for (auto& [key, val] : entry["groups"].items())
            {
                if (val.is_number_integer())
                    def.groups[key] = val.get<int>();
            }
        }

        // --- Sounds ---
        if (entry.contains("sounds") && entry["sounds"].is_object())
        {
            const auto& s = entry["sounds"];
            if (s.contains("footstep") && s["footstep"].is_string())
                def.soundFootstep = s["footstep"].get<std::string>();
            if (s.contains("dig") && s["dig"].is_string())
                def.soundDig = s["dig"].get<std::string>();
            if (s.contains("place") && s["place"].is_string())
                def.soundPlace = s["place"].get<std::string>();
        }

        // --- Liquid ---
        if (entry.contains("liquid") && entry["liquid"].is_object())
        {
            const auto& l = entry["liquid"];
            if (l.contains("type") && l["type"].is_string())
                def.liquidType = parseLiquidType(l["type"].get<std::string>());
            def.liquidViscosity = l.value("viscosity", static_cast<uint8_t>(0));
            def.liquidRange = l.value("range", static_cast<uint8_t>(8));
            def.liquidRenewable = l.value("renewable", true);
            if (l.contains("alternativeFlowing") && l["alternativeFlowing"].is_string())
                def.liquidAlternativeFlowing = l["alternativeFlowing"].get<std::string>();
            if (l.contains("alternativeSource") && l["alternativeSource"].is_string())
                def.liquidAlternativeSource = l["alternativeSource"].get<std::string>();
        }

        // --- Visual effects ---
        if (entry.contains("postEffectColor"))
        {
            if (entry["postEffectColor"].is_string())
            {
                const auto& hexStr = entry["postEffectColor"].get<std::string>();
                std::string_view sv = hexStr;
                if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'))
                    sv.remove_prefix(2);
                uint32_t val = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 16);
                if (ec == std::errc{})
                    def.postEffectColor = val;
            }
            else if (entry["postEffectColor"].is_number_unsigned())
            {
                def.postEffectColor = entry["postEffectColor"].get<uint32_t>();
            }
        }

        // --- Mechanical ---
        if (entry.contains("pushReaction") && entry["pushReaction"].is_string())
            def.pushReaction = parsePushReaction(entry["pushReaction"].get<std::string>());

        def.isFallingBlock = entry.value("isFallingBlock", false);

        // --- Signal stubs ---
        def.powerOutput = entry.value("powerOutput", static_cast<uint8_t>(0));
        def.isPowerSource = entry.value("isPowerSource", false);
        def.isPowerConductor = entry.value("isPowerConductor", true);

        std::string blockName = def.stringId;
        auto result = registerBlock(std::move(def));
        if (result.has_value())
        {
            ++count;
        }
        else
        {
            VX_LOG_WARN("Failed to register block '{}': skipping", blockName);
        }
    }

    return count;
}

bool BlockRegistry::isValidNamespace(std::string_view name)
{
    auto colon = name.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon == name.size() - 1)
    {
        return false;
    }
    // Ensure only one colon
    if (name.find(':', colon + 1) != std::string_view::npos)
    {
        return false;
    }
    return true;
}

} // namespace voxel::world
