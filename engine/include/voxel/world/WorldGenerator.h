#pragma once

#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace voxel::world
{

class WorldGenerator
{
  public:
    explicit WorldGenerator(uint64_t seed, const BlockRegistry& registry);

    /// Generate a fully populated ChunkColumn for the given chunk coordinate.
    [[nodiscard]] ChunkColumn generateChunkColumn(glm::ivec2 chunkCoord) const;

    /// Find a suitable spawn point above solid ground near world origin.
    [[nodiscard]] glm::dvec3 findSpawnPoint() const;

    [[nodiscard]] uint64_t getSeed() const { return m_seed; }

  private:
    /// Compute terrain height at a world (x, z) position. Returns value in [40, 120].
    [[nodiscard]] int computeHeight(int worldX, int worldZ) const;

    uint64_t m_seed;

    // Cached block IDs resolved at construction time
    uint16_t m_bedrockId;
    uint16_t m_stoneId;
    uint16_t m_dirtId;
    uint16_t m_grassId;

    // FastNoiseLite is configured per-call since GetNoise is const-qualified
    // but the object itself is mutable (SetSeed etc.). We store config params
    // and create a thread-local instance as needed, or just use mutable.
    // For simplicity in synchronous single-threaded gen (Story 4.1), use mutable.
    struct NoiseConfig
    {
        int seed;
        float frequency;
        int octaves;
    };
    NoiseConfig m_noiseConfig;
};

} // namespace voxel::world
