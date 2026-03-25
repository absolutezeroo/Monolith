#pragma once

#include "voxel/world/Block.h"

#include "voxel/core/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

class BlockRegistry
{
public:
    BlockRegistry();

    /// Register a new block definition. Returns assigned numeric ID.
    /// Rejects duplicate stringId or invalid namespace format.
    [[nodiscard]] core::Result<uint16_t> registerBlock(BlockDefinition def);

    /// Get block definition by numeric ID.
    [[nodiscard]] const BlockDefinition& getBlock(uint16_t id) const;

    /// Get numeric ID by string name. Returns BLOCK_AIR (0) if not found.
    [[nodiscard]] uint16_t getIdByName(std::string_view name) const;

    /// Number of registered blocks (including air).
    [[nodiscard]] uint16_t blockCount() const;

    /// Load block definitions from a JSON file. Returns count of blocks loaded.
    [[nodiscard]] core::Result<uint16_t> loadFromJson(const std::filesystem::path& filePath);

private:
    std::vector<BlockDefinition> m_blocks;
    std::unordered_map<std::string, uint16_t> m_nameToId;

    [[nodiscard]] static bool isValidNamespace(std::string_view name);
};

} // namespace voxel::world
