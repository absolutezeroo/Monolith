#pragma once

#include "voxel/core/Result.h"
#include "voxel/world/Block.h"

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

    /// Register a new block definition. Returns assigned base state ID.
    /// Rejects duplicate stringId or invalid namespace format.
    [[nodiscard]] core::Result<uint16_t> registerBlock(BlockDefinition def);

    /// Get block definition by type index (position in m_blocks).
    [[nodiscard]] const BlockDefinition& getBlockByTypeIndex(uint16_t typeIndex) const;

    /// Get block definition by state ID (resolves any state variant to its parent block).
    [[nodiscard]] const BlockDefinition& getBlockType(uint16_t stateId) const;

    /// Decompose a state ID into its property name→value pairs.
    [[nodiscard]] StateMap getStateValues(uint16_t stateId) const;

    /// Combine a base state ID + property values into the specific state ID.
    [[nodiscard]] uint16_t getStateId(uint16_t baseStateId, const StateMap& stateMap) const;

    /// Return the state ID with one property changed, others preserved.
    [[nodiscard]] uint16_t withProperty(uint16_t stateId, std::string_view propName, std::string_view value) const;

    /// Total allocated state IDs (for capacity validation).
    [[nodiscard]] uint16_t totalStateCount() const;

    /// Get numeric ID by string name. Returns BLOCK_AIR (0) if not found.
    [[nodiscard]] uint16_t getIdByName(std::string_view name) const;

    /// Number of registered block types (including air).
    [[nodiscard]] uint16_t blockCount() const;

    /// Load block definitions from a JSON file. Returns count of blocks loaded.
    [[nodiscard]] core::Result<uint16_t> loadFromJson(const std::filesystem::path& filePath);

  private:
    std::vector<BlockDefinition> m_blocks;
    std::unordered_map<std::string, uint16_t> m_nameToId;
    std::vector<uint16_t> m_stateToBlockIndex;
    uint16_t m_nextStateId = 0;

    [[nodiscard]] static bool isValidNamespace(std::string_view name);
};

} // namespace voxel::world
