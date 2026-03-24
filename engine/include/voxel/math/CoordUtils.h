#pragma once

#include "voxel/math/MathTypes.h"

#include <cmath>
#include <cstdint>

namespace voxel::math
{

/// Number of blocks per section axis (16x16x16).
inline constexpr int32_t SECTION_SIZE = 16;

/// Total blocks per section (16^3 = 4096).
inline constexpr int32_t SECTION_VOLUME = SECTION_SIZE * SECTION_SIZE * SECTION_SIZE;

/**
 * @brief Converts a world position to chunk coordinates (XZ only).
 *
 * Uses std::floor for correct negative coordinate handling.
 * Y is dropped because ChunkColumns span all vertical sections.
 *
 * @param pos World position (double precision).
 * @return Chunk coordinate as (chunkX, chunkZ).
 */
[[nodiscard]] inline IVec2 worldToChunk(DVec3 pos)
{
    return IVec2{
        static_cast<int32_t>(std::floor(pos.x / static_cast<double>(SECTION_SIZE))),
        static_cast<int32_t>(std::floor(pos.z / static_cast<double>(SECTION_SIZE)))};
}

/**
 * @brief Converts a world position to local block coordinates.
 *
 * X and Z use bitmask (& 0xF) for wrapping into [0, 15].
 * Y is floored to integer (not wrapped — sections stack vertically).
 *
 * @param pos World position (double precision).
 * @return Local block coordinate. X,Z in [0,15]; Y is absolute integer Y.
 */
[[nodiscard]] inline IVec3 worldToLocal(DVec3 pos)
{
    return IVec3{
        static_cast<int32_t>(std::floor(pos.x)) & 0xF,
        static_cast<int32_t>(std::floor(pos.y)),
        static_cast<int32_t>(std::floor(pos.z)) & 0xF};
}

/**
 * @brief Converts chunk + local coordinates back to world position.
 *
 * @param chunk Chunk coordinate (XZ).
 * @param local Local block coordinate within the chunk.
 * @return World position (double precision).
 */
[[nodiscard]] inline DVec3 localToWorld(IVec2 chunk, IVec3 local)
{
    return DVec3{
        static_cast<double>(chunk.x) * static_cast<double>(SECTION_SIZE) + static_cast<double>(local.x),
        static_cast<double>(local.y),
        static_cast<double>(chunk.y) * static_cast<double>(SECTION_SIZE) + static_cast<double>(local.z)};
}

/**
 * @brief Converts 3D block coordinates to a flat array index (Y-major layout).
 *
 * Layout: y * 256 + z * 16 + x
 * Valid input: x,y,z in [0, SECTION_SIZE-1]. No bounds checking for performance.
 *
 * @return Flat index in [0, 4095].
 */
[[nodiscard]] inline int32_t blockToIndex(int32_t x, int32_t y, int32_t z)
{
    return y * (SECTION_SIZE * SECTION_SIZE) + z * SECTION_SIZE + x;
}

/**
 * @brief Converts a flat array index back to 3D block coordinates.
 *
 * Inverse of blockToIndex. Valid input: index in [0, 4095].
 *
 * @return Block coordinates (x, y, z) each in [0, 15].
 */
[[nodiscard]] inline IVec3 indexToBlock(int32_t index)
{
    int32_t y = index >> 8;          // index / 256
    int32_t z = (index >> 4) & 0xF;  // (index % 256) / 16
    int32_t x = index & 0xF;         // index % 16
    return IVec3{x, y, z};
}

} // namespace voxel::math
