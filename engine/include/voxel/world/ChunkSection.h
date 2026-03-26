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

    uint16_t blocks[VOLUME];

    ChunkSection();

    [[nodiscard]] uint16_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint16_t id);
    void fill(uint16_t id);
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool isFull() const;
    [[nodiscard]] int32_t countNonAir() const;

  private:
    int32_t m_nonAirCount = 0;

    [[nodiscard]] static constexpr int toIndex(int x, int y, int z) { return y * 256 + z * 16 + x; }
};

} // namespace voxel::world
