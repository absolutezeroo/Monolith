#pragma once

#include "voxel/world/ChunkColumn.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace voxel::world
{

/// Floor division — correct for negative dividends (C++ truncates toward zero).
inline int floorDiv(int a, int b)
{
    return a / b - (a % b != 0 && (a ^ b) < 0);
}

/// Euclidean modulo — always returns a non-negative result.
inline int euclideanMod(int a, int b)
{
    int r = a % b;
    return r + (r < 0) * b;
}

/// Translate world position to chunk column coordinate (X/Z only).
inline glm::ivec2 worldToChunkCoord(const glm::ivec3& worldPos)
{
    return {floorDiv(worldPos.x, ChunkSection::SIZE), floorDiv(worldPos.z, ChunkSection::SIZE)};
}

/// Translate world position to chunk-local position (Y passed through).
inline glm::ivec3 worldToLocalPos(const glm::ivec3& worldPos)
{
    return {euclideanMod(worldPos.x, ChunkSection::SIZE), worldPos.y, euclideanMod(worldPos.z, ChunkSection::SIZE)};
}

/// Spatial hash for glm::ivec2 chunk coordinates using XOR-shift combination.
struct ChunkCoordHash
{
    size_t operator()(const glm::ivec2& v) const noexcept
    {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class ChunkManager
{
  public:
    ChunkManager() = default;

    /// Returns the ChunkColumn at the given coordinate, or nullptr if not loaded.
    [[nodiscard]] ChunkColumn* getChunk(glm::ivec2 coord);
    [[nodiscard]] const ChunkColumn* getChunk(glm::ivec2 coord) const;

    /// Get block at world position. Returns BLOCK_AIR if chunk not loaded.
    [[nodiscard]] uint16_t getBlock(const glm::ivec3& worldPos) const;

    /// Set block at world position. No-op if chunk not loaded (debug assert).
    void setBlock(const glm::ivec3& worldPos, uint16_t id);

    /// Insert a new empty ChunkColumn. No-op if already loaded.
    void loadChunk(glm::ivec2 coord);

    /// Remove a ChunkColumn. No-op if not loaded.
    void unloadChunk(glm::ivec2 coord);

    /// Number of currently loaded chunk columns.
    [[nodiscard]] size_t loadedChunkCount() const;

    /// Number of chunk columns with at least one dirty section.
    [[nodiscard]] size_t dirtyChunkCount() const;

  private:
    std::unordered_map<glm::ivec2, std::unique_ptr<ChunkColumn>, ChunkCoordHash> m_chunks;
};

} // namespace voxel::world
