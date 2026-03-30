#include "voxel/world/BlockInventory.h"

#include "BinaryIO.h"

namespace voxel::world
{

void BlockInventory::setSize(const std::string& listname, size_t size)
{
    m_lists[listname].resize(size);
}

size_t BlockInventory::getSize(const std::string& listname) const
{
    auto it = m_lists.find(listname);
    if (it == m_lists.end())
    {
        return 0;
    }
    return it->second.size();
}

ItemStack BlockInventory::getStack(const std::string& listname, size_t index) const
{
    auto it = m_lists.find(listname);
    if (it == m_lists.end() || index >= it->second.size())
    {
        return {};
    }
    return it->second[index];
}

void BlockInventory::setStack(const std::string& listname, size_t index, const ItemStack& stack)
{
    auto it = m_lists.find(listname);
    if (it == m_lists.end() || index >= it->second.size())
    {
        return;
    }
    it->second[index] = stack;
}

bool BlockInventory::isEmpty(const std::string& listname) const
{
    auto it = m_lists.find(listname);
    if (it == m_lists.end())
    {
        return true;
    }
    for (const auto& stack : it->second)
    {
        if (!stack.isEmpty())
        {
            return false;
        }
    }
    return true;
}

bool BlockInventory::isEmpty() const
{
    for (const auto& [name, list] : m_lists)
    {
        for (const auto& stack : list)
        {
            if (!stack.isEmpty())
            {
                return false;
            }
        }
    }
    return true;
}

std::vector<std::string> BlockInventory::getListNames() const
{
    std::vector<std::string> names;
    names.reserve(m_lists.size());
    for (const auto& [name, list] : m_lists)
    {
        names.push_back(name);
    }
    return names;
}

// Serialization format:
// [u16 list_count] [for each list: string name, u16 slot_count, [for each slot: string item_name, u16 count]]

void BlockInventory::serialize(BinaryWriter& writer) const
{
    writer.writeU16(static_cast<uint16_t>(m_lists.size()));
    for (const auto& [name, list] : m_lists)
    {
        writer.writeString(name);
        writer.writeU16(static_cast<uint16_t>(list.size()));
        for (const auto& stack : list)
        {
            writer.writeString(stack.name);
            writer.writeU16(stack.count);
        }
    }
}

core::Result<BlockInventory> BlockInventory::deserialize(BinaryReader& reader)
{
    auto listCountResult = reader.readU16();
    if (!listCountResult.has_value())
    {
        return std::unexpected(listCountResult.error());
    }
    uint16_t listCount = listCountResult.value();

    BlockInventory inv;
    for (uint16_t i = 0; i < listCount; ++i)
    {
        auto nameResult = reader.readString();
        if (!nameResult.has_value())
        {
            return std::unexpected(nameResult.error());
        }

        auto slotCountResult = reader.readU16();
        if (!slotCountResult.has_value())
        {
            return std::unexpected(slotCountResult.error());
        }
        uint16_t slotCount = slotCountResult.value();

        std::vector<ItemStack> slots(slotCount);
        for (uint16_t s = 0; s < slotCount; ++s)
        {
            auto itemNameResult = reader.readString();
            if (!itemNameResult.has_value())
            {
                return std::unexpected(itemNameResult.error());
            }
            auto itemCountResult = reader.readU16();
            if (!itemCountResult.has_value())
            {
                return std::unexpected(itemCountResult.error());
            }
            slots[s] = ItemStack{std::move(itemNameResult.value()), itemCountResult.value()};
        }

        inv.m_lists[std::move(nameResult.value())] = std::move(slots);
    }

    return inv;
}

} // namespace voxel::world
