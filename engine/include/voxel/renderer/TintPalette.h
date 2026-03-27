#pragma once

#include "voxel/world/BiomeTypes.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace voxel::renderer
{

/// Small color lookup table for biome-dependent block tinting.
/// Index 0 = no tint (white). Indices 1-7 = tint colors.
/// Uploaded to GPU as UBO/SSBO in Story 6.8.
class TintPalette
{
public:
    static constexpr uint8_t MAX_ENTRIES = 8;
    static constexpr uint8_t TINT_NONE = 0;
    static constexpr uint8_t TINT_GRASS = 1;
    static constexpr uint8_t TINT_FOLIAGE = 2;
    static constexpr uint8_t TINT_WATER = 3;

    TintPalette();

    /// Get tint color by index. Clamps to valid range [0, MAX_ENTRIES).
    [[nodiscard]] glm::vec3 getColor(uint8_t index) const;

    /// Set a tint color at the given index. Clamps to valid range.
    void setColor(uint8_t index, glm::vec3 color);

    /// Build a palette with biome-appropriate grass/foliage/water colors.
    /// V1: hardcoded LUT. Future: gradient texture sampling.
    [[nodiscard]] static TintPalette buildForBiome(world::BiomeType biome);

private:
    std::array<glm::vec3, MAX_ENTRIES> m_colors;
};

} // namespace voxel::renderer
