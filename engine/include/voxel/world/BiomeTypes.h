#pragma once

#include "voxel/core/Assert.h"

#include <cstdint>
#include <string_view>

namespace voxel::world
{

enum class BiomeType : uint8_t
{
    Desert,
    Savanna,
    Plains,
    Forest,
    Jungle,
    Taiga,
    Tundra,
    IcePlains,
    Count
};

struct BiomeDefinition
{
    BiomeType type;
    std::string_view surfaceBlock;
    std::string_view subSurfaceBlock;
    std::string_view fillerBlock;
    int surfaceDepth;
    float heightModifier;
    float heightScale;
};

/// Returns the static BiomeDefinition for the given biome type.
/// Asserts that type < BiomeType::Count.
inline const BiomeDefinition& getBiomeDefinition(BiomeType type)
{
    static constexpr BiomeDefinition BIOME_DEFINITIONS[] = {
        {BiomeType::Desert, "base:sand", "base:sand", "base:sandstone", 4, -5.0f, 0.5f},
        {BiomeType::Savanna, "base:grass_block", "base:dirt", "base:stone", 3, 0.0f, 0.8f},
        {BiomeType::Plains, "base:grass_block", "base:dirt", "base:stone", 3, 0.0f, 1.0f},
        {BiomeType::Forest, "base:grass_block", "base:dirt", "base:stone", 3, 3.0f, 1.2f},
        {BiomeType::Jungle, "base:grass_block", "base:dirt", "base:stone", 4, 5.0f, 1.5f},
        {BiomeType::Taiga, "base:grass_block", "base:dirt", "base:stone", 3, 2.0f, 1.0f},
        {BiomeType::Tundra, "base:snow_block", "base:dirt", "base:stone", 2, -2.0f, 0.6f},
        {BiomeType::IcePlains, "base:snow_block", "base:snow_block", "base:stone", 3, -3.0f, 0.4f},
    };

    auto index = static_cast<size_t>(type);
    VX_ASSERT(index < static_cast<size_t>(BiomeType::Count), "Invalid BiomeType passed to getBiomeDefinition");
    return BIOME_DEFINITIONS[index];
}

} // namespace voxel::world
