#pragma once

#include "voxel/world/BiomeSystem.h"
#include "voxel/world/BiomeTypes.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/SplineCurve.h"

#pragma warning(push, 0)
#include "voxel/world/FastNoiseLite.h"
#pragma warning(pop)

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
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
    /// Compute base terrain height at a world (x, z) position via spline-remapped continent noise.
    [[nodiscard]] float computeBaseHeight(int worldX, int worldZ) const;

    /// Get detail noise value at a world (x, z) position.
    [[nodiscard]] float getDetailNoise(int worldX, int worldZ) const;

    static constexpr float DETAIL_AMPLITUDE = 7.0f;

    uint64_t m_seed;

    // Cached block IDs resolved at construction time
    uint16_t m_bedrockId;
    uint16_t m_stoneId;

    // Per-biome cached block IDs
    struct BiomeBlockIds
    {
        uint16_t surface;
        uint16_t subSurface;
        uint16_t filler;
    };
    std::array<BiomeBlockIds, static_cast<size_t>(BiomeType::Count)> m_biomeBlockIds;

    // Biome system for climate-based biome selection and blending
    BiomeSystem m_biomeSystem;

    // Spline curve mapping continent noise to terrain height
    SplineCurve m_spline;

    // FastNoiseLite instances configured once at construction. GetNoise() is const,
    // so all post-construction usage is read-only.
    FastNoiseLite m_continentNoise;
    FastNoiseLite m_detailNoise;
};

} // namespace voxel::world
