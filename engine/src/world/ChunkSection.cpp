#include "voxel/world/ChunkSection.h"

#include <algorithm>

namespace voxel::world
{

ChunkSection::ChunkSection()
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
    blocks[toIndex(x, y, z)] = id;
}

void ChunkSection::fill(uint16_t id)
{
    std::fill(std::begin(blocks), std::end(blocks), id);
}

bool ChunkSection::isEmpty() const
{
    return std::all_of(std::begin(blocks), std::end(blocks), [](uint16_t b) { return b == BLOCK_AIR; });
}

int32_t ChunkSection::countNonAir() const
{
    return static_cast<int32_t>(
        std::count_if(std::begin(blocks), std::end(blocks), [](uint16_t b) { return b != BLOCK_AIR; }));
}

} // namespace voxel::world