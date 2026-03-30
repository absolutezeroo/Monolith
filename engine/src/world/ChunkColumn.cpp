#include "voxel/world/ChunkColumn.h"

#include "voxel/core/Assert.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"

namespace voxel::world
{

ChunkColumn::ChunkColumn(glm::ivec2 chunkCoord) : m_chunkCoord(chunkCoord), m_sections{}, m_dirty{}, m_heightMap{} {}

glm::ivec2 ChunkColumn::getChunkCoord() const
{
    return m_chunkCoord;
}

ChunkSection* ChunkColumn::getSection(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_sections[sectionY].get();
}

const ChunkSection* ChunkColumn::getSection(int sectionY) const
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_sections[sectionY].get();
}

ChunkSection& ChunkColumn::getOrCreateSection(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    if (!m_sections[sectionY])
    {
        m_sections[sectionY] = std::make_unique<ChunkSection>();
    }
    return *m_sections[sectionY];
}

uint16_t ChunkColumn::getBlock(int x, int y, int z) const
{
    VX_ASSERT(x >= 0 && x < ChunkSection::SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < COLUMN_HEIGHT, "y out of bounds");
    VX_ASSERT(z >= 0 && z < ChunkSection::SIZE, "z out of bounds");

    int sectionIndex = y / ChunkSection::SIZE;
    int localY = y % ChunkSection::SIZE;

    const ChunkSection* section = m_sections[sectionIndex].get();
    if (!section)
    {
        return BLOCK_AIR;
    }
    return section->getBlock(x, localY, z);
}

void ChunkColumn::setBlock(int x, int y, int z, uint16_t id)
{
    VX_ASSERT(x >= 0 && x < ChunkSection::SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < COLUMN_HEIGHT, "y out of bounds");
    VX_ASSERT(z >= 0 && z < ChunkSection::SIZE, "z out of bounds");

    int sectionIndex = y / ChunkSection::SIZE;
    int localY = y % ChunkSection::SIZE;

    // Remove stale metadata/inventory when block type changes
    uint16_t oldId = getBlock(x, y, z);
    if (oldId != id)
    {
        uint16_t packed = packLocalIndex(x, y, z);
        m_metadata.erase(packed);
        m_inventories.erase(packed);
    }

    ChunkSection& section = getOrCreateSection(sectionIndex);
    section.setBlock(x, localY, z, id);
    m_dirty[sectionIndex] = true;
}

bool ChunkColumn::isSectionDirty(int sectionY) const
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_dirty[sectionY];
}

void ChunkColumn::markDirty(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    m_dirty[sectionY] = true;
}

void ChunkColumn::clearDirty(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    m_dirty[sectionY] = false;
}

LightMap& ChunkColumn::getLightMap(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_lightMaps[sectionY];
}

const LightMap& ChunkColumn::getLightMap(int sectionY) const
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_lightMaps[sectionY];
}

void ChunkColumn::clearAllLight()
{
    for (auto& lightMap : m_lightMaps)
    {
        lightMap.clear();
    }
}

uint8_t ChunkColumn::getHeight(int x, int z) const
{
    VX_ASSERT(x >= 0 && x < ChunkSection::SIZE, "x out of bounds");
    VX_ASSERT(z >= 0 && z < ChunkSection::SIZE, "z out of bounds");
    return m_heightMap[z * ChunkSection::SIZE + x];
}

void ChunkColumn::setHeight(int x, int z, uint8_t y)
{
    VX_ASSERT(x >= 0 && x < ChunkSection::SIZE, "x out of bounds");
    VX_ASSERT(z >= 0 && z < ChunkSection::SIZE, "z out of bounds");
    m_heightMap[z * ChunkSection::SIZE + x] = y;
}

void ChunkColumn::buildHeightMap(const BlockRegistry& registry)
{
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            uint8_t height = 0;
            for (int y = COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                uint16_t blockId = getBlock(x, y, z);
                const BlockDefinition& def = registry.getBlockType(blockId);
                if (def.lightFilter == 15)
                {
                    height = static_cast<uint8_t>(y);
                    break;
                }
            }
            m_heightMap[z * ChunkSection::SIZE + x] = height;
        }
    }
}

bool ChunkColumn::isAllEmpty() const
{
    for (const auto& section : m_sections)
    {
        if (section && !section->isEmpty())
        {
            return false;
        }
    }
    return true;
}

int ChunkColumn::getHighestNonEmptySection() const
{
    for (int i = SECTIONS_PER_COLUMN - 1; i >= 0; --i)
    {
        if (m_sections[i] && !m_sections[i]->isEmpty())
        {
            return i;
        }
    }
    return -1;
}

// --- Metadata accessors ---

uint16_t ChunkColumn::packLocalIndex(int x, int y, int z)
{
    return static_cast<uint16_t>(x + z * ChunkSection::SIZE + y * ChunkSection::SIZE * ChunkSection::SIZE);
}

BlockMetadata* ChunkColumn::getMetadata(int x, int y, int z)
{
    auto it = m_metadata.find(packLocalIndex(x, y, z));
    return it != m_metadata.end() ? &it->second : nullptr;
}

const BlockMetadata* ChunkColumn::getMetadata(int x, int y, int z) const
{
    auto it = m_metadata.find(packLocalIndex(x, y, z));
    return it != m_metadata.end() ? &it->second : nullptr;
}

BlockMetadata& ChunkColumn::getOrCreateMetadata(int x, int y, int z)
{
    return m_metadata[packLocalIndex(x, y, z)];
}

void ChunkColumn::removeMetadata(int x, int y, int z)
{
    m_metadata.erase(packLocalIndex(x, y, z));
}

// --- Inventory accessors ---

BlockInventory* ChunkColumn::getInventory(int x, int y, int z)
{
    auto it = m_inventories.find(packLocalIndex(x, y, z));
    return it != m_inventories.end() ? &it->second : nullptr;
}

const BlockInventory* ChunkColumn::getInventory(int x, int y, int z) const
{
    auto it = m_inventories.find(packLocalIndex(x, y, z));
    return it != m_inventories.end() ? &it->second : nullptr;
}

BlockInventory& ChunkColumn::getOrCreateInventory(int x, int y, int z)
{
    return m_inventories[packLocalIndex(x, y, z)];
}

void ChunkColumn::removeInventory(int x, int y, int z)
{
    m_inventories.erase(packLocalIndex(x, y, z));
}

bool ChunkColumn::hasBlockData(int x, int y, int z) const
{
    uint16_t packed = packLocalIndex(x, y, z);
    return m_metadata.contains(packed) || m_inventories.contains(packed);
}

} // namespace voxel::world
