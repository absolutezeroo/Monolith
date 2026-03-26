#include "voxel/world/BlockRegistry.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace voxel::world
{

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
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidArgument, "Invalid block namespace: " + def.stringId});
    }

    if (m_nameToId.contains(def.stringId))
    {
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidArgument, "Duplicate block ID: " + def.stringId});
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
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidFormat, "Invalid JSON format: " + filePath.string()});
    }

    uint16_t count = 0;
    for (const auto& entry : json)
    {
        if (!entry.is_object())
        {
            continue;
        }

        BlockDefinition def;
        def.stringId = entry.value("stringId", "");
        def.isSolid = entry.value("isSolid", true);
        def.isTransparent = entry.value("isTransparent", false);
        def.hasCollision = entry.value("hasCollision", true);
        def.lightEmission = entry.value("lightEmission", static_cast<uint8_t>(0));
        def.lightFilter = entry.value("lightFilter", static_cast<uint8_t>(15));
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
