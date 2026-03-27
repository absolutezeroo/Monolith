#pragma once

#include "voxel/world/BiomeSystem.h"
#include "voxel/world/BiomeTypes.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace voxel::world
{

/// A tree schematic: block offsets relative to the root position (0,0,0).
struct TreeSchematic
{
    std::vector<glm::ivec3> trunkOffsets;
    std::vector<glm::ivec3> leafOffsets;
    uint16_t trunkBlock = BLOCK_AIR;
    uint16_t leafBlock = BLOCK_AIR;
};

/// Ore generation parameters for a single ore type.
struct OreConfig
{
    uint16_t blockId = BLOCK_AIR;
    int veinsPerChunk = 0;
    int minY = 0;
    int maxY = 0;
    int minVeinSize = 0;
    int maxVeinSize = 0;
};

/**
 * @brief Places multi-block structures (trees), surface decorations, and ore veins
 *        during terrain generation.
 *
 * Designed to be called after terrain fill and cave carving. Tree placement uses
 * a 3x3 neighbor-overlap algorithm for cross-chunk determinism.
 */
class StructureGenerator
{
  public:
    StructureGenerator(uint64_t seed, const BlockRegistry& registry);

    /// Place ore veins in stone. Call BEFORE cave carving so caves cut through ores.
    void populateOres(ChunkColumn& column, glm::ivec2 chunkCoord) const;

    /// Place trees and surface decorations. Call AFTER cave carving.
    /// @param computeSurfaceHeightFn Callback for cross-chunk surface height lookups.
    void populateStructures(
        ChunkColumn& column,
        glm::ivec2 chunkCoord,
        const BiomeSystem& biomeSystem,
        const int surfaceHeights[16][16],
        int (*computeSurfaceHeightFn)(float worldX, float worldZ, const void* userData),
        const void* userData) const;

  private:
    void placeOres(ChunkColumn& column, glm::ivec2 chunkCoord) const;

    void placeTrees(
        ChunkColumn& column,
        glm::ivec2 chunkCoord,
        const BiomeSystem& biomeSystem,
        const int surfaceHeights[16][16],
        int (*computeSurfaceHeightFn)(float worldX, float worldZ, const void* userData),
        const void* userData) const;

    void placeDecorations(
        ChunkColumn& column,
        glm::ivec2 chunkCoord,
        const BiomeSystem& biomeSystem,
        const int surfaceHeights[16][16]) const;

    [[nodiscard]] TreeSchematic buildOak(std::mt19937& rng) const;
    [[nodiscard]] TreeSchematic buildBirch(std::mt19937& rng) const;
    [[nodiscard]] TreeSchematic buildSpruce(std::mt19937& rng) const;
    [[nodiscard]] TreeSchematic buildJungle(std::mt19937& rng) const;
    [[nodiscard]] TreeSchematic buildCactus(std::mt19937& rng) const;

    [[nodiscard]] float getTreeDensity(BiomeType biome) const;
    [[nodiscard]] TreeSchematic selectTree(BiomeType biome, std::mt19937& rng) const;

    static constexpr int64_t TREE_SEED_OFFSET = 6;
    static constexpr int64_t ORE_SEED_OFFSET = 7;
    static constexpr int TREE_SPACING = 4;

    uint64_t m_seed;

    // Cached block IDs
    uint16_t m_oakLogId;
    uint16_t m_oakLeavesId;
    uint16_t m_birchLogId;
    uint16_t m_birchLeavesId;
    uint16_t m_spruceLogId;
    uint16_t m_spruceLeavesId;
    uint16_t m_jungleLogId;
    uint16_t m_jungleLeavesId;
    uint16_t m_cactusId;
    uint16_t m_tallGrassId;
    uint16_t m_flowerRedId;
    uint16_t m_flowerYellowId;
    uint16_t m_deadBushId;
    uint16_t m_snowLayerId;
    uint16_t m_stoneId;
    uint16_t m_sandId;
    uint16_t m_snowBlockId;
    uint16_t m_grassBlockId;
    uint16_t m_dirtId;

    // Ore configs
    OreConfig m_coalOre;
    OreConfig m_ironOre;
    OreConfig m_goldOre;
    OreConfig m_diamondOre;

    // True if any tree/decoration blocks are registered (skip placement if false)
    bool m_hasTreeBlocks = false;
    bool m_hasDecorationBlocks = false;
};

} // namespace voxel::world
