#pragma once

#include "voxel/core/Result.h"
#include "voxel/world/ItemStack.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

class BinaryWriter;
class BinaryReader;

/// Per-block named inventory lists. Each list is a vector of ItemStacks.
/// Used by mods for chests, furnaces, hoppers, etc.
class BlockInventory
{
public:
    void setSize(const std::string& listname, size_t size);
    [[nodiscard]] size_t getSize(const std::string& listname) const;

    [[nodiscard]] ItemStack getStack(const std::string& listname, size_t index) const;
    void setStack(const std::string& listname, size_t index, const ItemStack& stack);

    [[nodiscard]] bool isEmpty(const std::string& listname) const;
    [[nodiscard]] bool isEmpty() const; // All lists empty

    [[nodiscard]] std::vector<std::string> getListNames() const;

    // Serialization
    void serialize(BinaryWriter& writer) const;
    [[nodiscard]] static core::Result<BlockInventory> deserialize(BinaryReader& reader);

private:
    std::unordered_map<std::string, std::vector<ItemStack>> m_lists;
};

} // namespace voxel::world
