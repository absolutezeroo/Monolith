#include "voxel/world/ChunkColumn.h"

#include "voxel/core/Assert.h"

namespace voxel::world
{

ChunkColumn::ChunkColumn(glm::ivec2 chunkCoord) : m_chunkCoord(chunkCoord), m_sections{}, m_dirty{} {}

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

    ChunkSection& section = getOrCreateSection(sectionIndex);
    section.setBlock(x, localY, z, id);
    m_dirty[sectionIndex] = true;
}

bool ChunkColumn::isSectionDirty(int sectionY) const
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    return m_dirty[sectionY];
}

void ChunkColumn::clearDirty(int sectionY)
{
    VX_ASSERT(sectionY >= 0 && sectionY < SECTIONS_PER_COLUMN, "sectionY out of bounds");
    m_dirty[sectionY] = false;
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

} // namespace voxel::world
