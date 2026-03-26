#pragma once

#pragma warning(push, 0)
#include "voxel/world/FastNoiseLite.h"
#pragma warning(pop)

#include <cstdint>

#include <glm/vec2.hpp>

namespace voxel::world
{

class ChunkColumn;

/// Carves 3D caves and overhangs into terrain using cheese + spaghetti noise layers.
class CaveCarver
{
  public:
    explicit CaveCarver(uint64_t seed);

    /// Carve caves in a fully populated chunk column.
    /// @param column The column to carve (blocks will be set to BLOCK_AIR).
    /// @param chunkCoord The chunk coordinate (for world-space noise sampling).
    /// @param surfaceHeights Per-column surface heights [x][z], each the Y of the topmost solid block.
    void carveColumn(ChunkColumn& column, glm::ivec2 chunkCoord, const int surfaceHeights[16][16]) const;

    /// Evaluate whether a block at (worldX, worldY, worldZ) should be carved.
    /// @param surfaceHeight The surface height for this (x, z) column.
    [[nodiscard]] bool shouldCarve(float worldX, float worldY, float worldZ, int surfaceHeight) const;

    /// Compute the depth-dependent carving threshold for a given Y and surface height.
    /// Higher threshold = fewer caves. Returns 1.0 in the bedrock protection zone.
    [[nodiscard]] static float getThreshold(int y, int surfaceHeight);

  private:
    FastNoiseLite m_cheeseNoise;
    FastNoiseLite m_spaghettiNoise;
};

} // namespace voxel::world
