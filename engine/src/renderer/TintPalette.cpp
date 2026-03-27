#include "voxel/renderer/TintPalette.h"

#include <algorithm>

namespace voxel::renderer
{

TintPalette::TintPalette()
{
    m_colors.fill(glm::vec3(1.0f, 1.0f, 1.0f));
}

glm::vec3 TintPalette::getColor(uint8_t index) const
{
    return m_colors[std::min(index, static_cast<uint8_t>(MAX_ENTRIES - 1))];
}

void TintPalette::setColor(uint8_t index, glm::vec3 color)
{
    if (index < MAX_ENTRIES)
    {
        m_colors[index] = color;
    }
}

TintPalette TintPalette::buildForBiome(world::BiomeType biome)
{
    TintPalette palette;

    // Index 0 = white (no tint) — set by constructor.
    // Indices 4-7 = reserved for mods, default white.

    switch (biome)
    {
    case world::BiomeType::Plains:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.55f, 0.76f, 0.38f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.47f, 0.65f, 0.33f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.24f, 0.45f, 0.75f);
        break;

    case world::BiomeType::Forest:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.45f, 0.68f, 0.30f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.40f, 0.60f, 0.28f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.22f, 0.42f, 0.72f);
        break;

    case world::BiomeType::Desert:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.75f, 0.72f, 0.42f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.68f, 0.65f, 0.38f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.28f, 0.50f, 0.70f);
        break;

    case world::BiomeType::Taiga:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.50f, 0.68f, 0.45f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.42f, 0.58f, 0.40f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.20f, 0.40f, 0.70f);
        break;

    case world::BiomeType::Jungle:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.35f, 0.80f, 0.25f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.30f, 0.70f, 0.20f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.18f, 0.38f, 0.68f);
        break;

    case world::BiomeType::Tundra:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.60f, 0.65f, 0.50f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.55f, 0.58f, 0.45f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.25f, 0.48f, 0.78f);
        break;

    case world::BiomeType::Savanna:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.65f, 0.72f, 0.35f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.58f, 0.65f, 0.30f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.26f, 0.47f, 0.73f);
        break;

    case world::BiomeType::IcePlains:
        palette.m_colors[TINT_GRASS] = glm::vec3(0.62f, 0.68f, 0.55f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.55f, 0.60f, 0.50f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.22f, 0.45f, 0.80f);
        break;

    default:
        // Fallback: use Plains colors.
        palette.m_colors[TINT_GRASS] = glm::vec3(0.55f, 0.76f, 0.38f);
        palette.m_colors[TINT_FOLIAGE] = glm::vec3(0.47f, 0.65f, 0.33f);
        palette.m_colors[TINT_WATER] = glm::vec3(0.24f, 0.45f, 0.75f);
        break;
    }

    return palette;
}

} // namespace voxel::renderer
