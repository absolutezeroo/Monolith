#pragma once

#include "voxel/core/Assert.h"
#include "voxel/world/Block.h"

#include <cstdint>

namespace voxel::world
{

struct ChunkSection
{
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE; // 4096

    ChunkSection();

    [[nodiscard]] uint16_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint16_t id);
    void fill(uint16_t id);
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool isFull() const;
    [[nodiscard]] int32_t countNonAir() const;

    /// Read-only pointer to the raw block array for bulk access (compression, tests).
    [[nodiscard]] const uint16_t* data() const { return m_blocks; }

  private:
    uint16_t m_blocks[VOLUME];
    int32_t m_nonAirCount = 0;

    [[nodiscard]] static constexpr int toIndex(int x, int y, int z) { return y * 256 + z * 16 + x; }
};

} // namespace voxel::world
