#pragma once

#include "voxel/world/BiomeTypes.h"

#pragma warning(push, 0)
#include "voxel/world/FastNoiseLite.h"
#pragma warning(pop)

#include <cstdint>

namespace voxel::world
{

/// Result of blending biomes across a neighborhood.
struct BlendedBiome
{
    BiomeType primaryBiome;
    float blendedHeightModifier;
    float blendedHeightScale;
    float blendedSurfaceDepth;
};

/**
 * @brief Biome selection system using Whittaker-style temperature/humidity classification.
 *
 * Generates two independent noise maps (temperature, humidity) and uses a discretized
 * Whittaker diagram to select biome types. Supports distance-weighted blending for
 * smooth transitions at biome boundaries.
 */
class BiomeSystem
{
  public:
    explicit BiomeSystem(uint64_t seed);

    /// Get the raw biome type at a world (x, z) position (no blending).
    [[nodiscard]] BiomeType getBiomeAt(float worldX, float worldZ) const;

    /// Classify a biome from normalized [0, 1] temperature and humidity values.
    [[nodiscard]] static BiomeType classifyBiome(float temperature, float humidity);

    /// Get blended biome data at a world (x, z) position using 5x5 neighborhood sampling.
    [[nodiscard]] BlendedBiome getBlendedBiomeAt(float worldX, float worldZ) const;

  private:
    FastNoiseLite m_temperatureNoise;
    FastNoiseLite m_humidityNoise;
};

} // namespace voxel::world
