#include "voxel/world/ChunkSection.h"

#include <algorithm>

namespace voxel::world
{

ChunkSection::ChunkSection() : m_nonAirCount(0)
{
    std::fill(std::begin(blocks), std::end(blocks), BLOCK_AIR);
}

uint16_t ChunkSection::getBlock(int x, int y, int z) const
{
    VX_ASSERT(x >= 0 && x < SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < SIZE, "y out of bounds");
    VX_ASSERT(z >= 0 && z < SIZE, "z out of bounds");
    return blocks[toIndex(x, y, z)];
}

void ChunkSection::setBlock(int x, int y, int z, uint16_t id)
{
    VX_ASSERT(x >= 0 && x < SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < SIZE, "y out of bounds");
    VX_ASSERT(z >= 0 && z < SIZE, "z out of bounds");
    int idx = toIndex(x, y, z);
    uint16_t old = blocks[idx];
    if (old == id)
    {
        return;
    }
    if (old == BLOCK_AIR && id != BLOCK_AIR)
    {
        ++m_nonAirCount;
    }
    if (old != BLOCK_AIR && id == BLOCK_AIR)
    {
        --m_nonAirCount;
    }
    blocks[idx] = id;
}

void ChunkSection::fill(uint16_t id)
{
    std::fill(std::begin(blocks), std::end(blocks), id);
    m_nonAirCount = (id == BLOCK_AIR) ? 0 : VOLUME;
}

bool ChunkSection::isEmpty() const
{
    return m_nonAirCount == 0;
}

bool ChunkSection::isFull() const
{
    return m_nonAirCount == VOLUME;
}

int32_t ChunkSection::countNonAir() const
{
    return m_nonAirCount;
}

} // namespace voxel::world
