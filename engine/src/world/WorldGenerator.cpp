#include "voxel/world/WorldGenerator.h"

#include "voxel/core/Log.h"
#include "voxel/world/Block.h"

#include <algorithm>
#include <cmath>

namespace voxel::world
{

static constexpr float CONTINENT_FREQUENCY = 0.001f;
static constexpr int CONTINENT_OCTAVES = 4;
static constexpr float DETAIL_FREQUENCY = 0.02f;
static constexpr int DETAIL_OCTAVES = 4;
static constexpr int DETAIL_SEED_OFFSET = 0x12345;
static constexpr float MIN_TERRAIN_HEIGHT = 1.0f;
static constexpr float MAX_TERRAIN_HEIGHT = 254.0f;

static uint16_t resolveBlockId(const BlockRegistry& registry, std::string_view name, uint16_t fallback)
{
    uint16_t id = registry.getIdByName(name);
    if (id == BLOCK_AIR)
    {
        VX_LOG_WARN("WorldGenerator: block '{}' not registered, falling back to ID {}", name, fallback);
        return fallback;
    }
    return id;
}

WorldGenerator::WorldGenerator(uint64_t seed, const BlockRegistry& registry)
    : m_seed(seed)
    , m_bedrockId(resolveBlockId(registry, "base:bedrock", 1))
    , m_stoneId(resolveBlockId(registry, "base:stone", 1))
    , m_biomeSystem(seed)
    , m_caveCarver(seed)
    , m_structureGen(seed, registry)
    , m_spline(SplineCurve::createDefault())
{
    // FNL accepts int (32-bit) seeds; mask to 31 bits for positive range.
    int baseSeed = static_cast<int>(seed & 0x7FFFFFFF);

    // Continent noise: large-scale continental shapes
    m_continentNoise.SetSeed(baseSeed);
    m_continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_continentNoise.SetFractalOctaves(CONTINENT_OCTAVES);
    m_continentNoise.SetFrequency(CONTINENT_FREQUENCY);

    // Detail noise: local terrain variation (deterministic offset from continent seed)
    m_detailNoise.SetSeed(baseSeed ^ DETAIL_SEED_OFFSET);
    m_detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_detailNoise.SetFractalOctaves(DETAIL_OCTAVES);
    m_detailNoise.SetFrequency(DETAIL_FREQUENCY);

    // Resolve type-appropriate fallback IDs for biome block caching
    uint16_t grassFallback = resolveBlockId(registry, "base:grass_block", m_stoneId);
    uint16_t dirtFallback = resolveBlockId(registry, "base:dirt", m_stoneId);

    // Cache per-biome block IDs, resolving string names to numeric IDs
    for (size_t i = 0; i < static_cast<size_t>(BiomeType::Count); ++i)
    {
        const BiomeDefinition& def = getBiomeDefinition(static_cast<BiomeType>(i));
        m_biomeBlockIds[i].surface = resolveBlockId(registry, def.surfaceBlock, grassFallback);
        m_biomeBlockIds[i].subSurface = resolveBlockId(registry, def.subSurfaceBlock, dirtFallback);
        m_biomeBlockIds[i].filler = resolveBlockId(registry, def.fillerBlock, m_stoneId);
    }
}

float WorldGenerator::computeBaseHeight(int worldX, int worldZ) const
{
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);

    // Continent noise → spline-remapped base height
    float continentNoise = m_continentNoise.GetNoise(wx, wz);
    return m_spline.evaluate(continentNoise);
}

float WorldGenerator::getDetailNoise(int worldX, int worldZ) const
{
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);
    return m_detailNoise.GetNoise(wx, wz);
}

ChunkColumn WorldGenerator::generateChunkColumn(glm::ivec2 chunkCoord) const
{
    ChunkColumn column(chunkCoord);

    // Track surface heights per (x, z) for the cave carving post-pass
    int surfaceHeights[ChunkSection::SIZE][ChunkSection::SIZE] = {};

    for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
    {
        for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
        {
            int worldX = chunkCoord.x * ChunkSection::SIZE + lx;
            int worldZ = chunkCoord.y * ChunkSection::SIZE + lz;

            // Get blended biome data for this column
            BlendedBiome blended =
                m_biomeSystem.getBlendedBiomeAt(static_cast<float>(worldX), static_cast<float>(worldZ));

            // Height computation: base + biome modifier + detail * amplitude * biome scale
            float baseHeight = computeBaseHeight(worldX, worldZ);
            float detailNoise = getDetailNoise(worldX, worldZ);
            float finalHeight =
                baseHeight + blended.blendedHeightModifier + detailNoise * DETAIL_AMPLITUDE * blended.blendedHeightScale;

            finalHeight = std::clamp(finalHeight, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT);
            int height = static_cast<int>(finalHeight);

            // Record surface height for cave carver
            surfaceHeights[lx][lz] = height;

            // Surface depth from blended biome, clamped to valid range
            int surfaceDepth = static_cast<int>(std::round(blended.blendedSurfaceDepth));
            if (height < surfaceDepth + 1)
            {
                surfaceDepth = std::max(0, height - 1);
            }
            surfaceDepth = std::max(1, surfaceDepth);

            // Resolve biome block IDs
            size_t biomeIdx = static_cast<size_t>(blended.primaryBiome);
            uint16_t surfaceId = m_biomeBlockIds[biomeIdx].surface;
            uint16_t subSurfaceId = m_biomeBlockIds[biomeIdx].subSurface;
            uint16_t fillerId = m_biomeBlockIds[biomeIdx].filler;

            // Bedrock at y=0
            column.setBlock(lx, 0, lz, m_bedrockId);

            // Filler block from y=1 up to (height - surfaceDepth - 1)
            int fillerTop = std::max(0, height - surfaceDepth - 1);
            for (int y = 1; y <= fillerTop; ++y)
            {
                column.setBlock(lx, y, lz, fillerId);
            }

            // Sub-surface layers
            int subSurfaceStart = fillerTop + 1;
            for (int y = subSurfaceStart; y < height; ++y)
            {
                column.setBlock(lx, y, lz, subSurfaceId);
            }

            // Surface block on top
            column.setBlock(lx, height, lz, surfaceId);
        }
    }

    // Ore veins placed before cave carving (so caves can cut through ores naturally)
    m_structureGen.populateOres(column, chunkCoord);

    // Cave carving post-pass
    m_caveCarver.carveColumn(column, chunkCoord, surfaceHeights);

    // Trees and decorations placed after cave carving
    m_structureGen.populateStructures(
        column, chunkCoord, m_biomeSystem, surfaceHeights, &surfaceHeightCallback, this);

    return column;
}

int WorldGenerator::computeSurfaceHeight(float worldX, float worldZ) const
{
    float continentNoise = m_continentNoise.GetNoise(worldX, worldZ);
    float baseHeight = m_spline.evaluate(continentNoise);
    BlendedBiome blended = m_biomeSystem.getBlendedBiomeAt(worldX, worldZ);
    float detailNoise = m_detailNoise.GetNoise(worldX, worldZ);
    float finalHeight = baseHeight + blended.blendedHeightModifier +
                        detailNoise * DETAIL_AMPLITUDE * blended.blendedHeightScale;
    return static_cast<int>(std::clamp(finalHeight, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT));
}

int WorldGenerator::surfaceHeightCallback(float worldX, float worldZ, const void* userData)
{
    const auto* self = static_cast<const WorldGenerator*>(userData);
    return self->computeSurfaceHeight(worldX, worldZ);
}

glm::dvec3 WorldGenerator::findSpawnPoint() const
{
    // Spiral walk from origin to find suitable spawn
    int x = 0;
    int z = 0;
    int dx = 1;
    int dz = 0;
    int segmentLength = 1;
    int segmentPassed = 0;
    int turnCount = 0;

    static constexpr int MAX_ATTEMPTS = 256;
    static constexpr int MIN_SPAWN_HEIGHT = 1;
    static constexpr int MAX_SPAWN_HEIGHT = 200;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
    {
        // Compute height with biome influence
        float baseHeight = computeBaseHeight(x, z);
        BlendedBiome blended = m_biomeSystem.getBlendedBiomeAt(static_cast<float>(x), static_cast<float>(z));
        float detailNoise = getDetailNoise(x, z);
        float finalHeight =
            baseHeight + blended.blendedHeightModifier + detailNoise * DETAIL_AMPLITUDE * blended.blendedHeightScale;
        finalHeight = std::clamp(finalHeight, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT);
        int height = static_cast<int>(finalHeight);

        if (height >= MIN_SPAWN_HEIGHT && height <= MAX_SPAWN_HEIGHT)
        {
            double spawnY = static_cast<double>(height) + 2.0;
            return {static_cast<double>(x) + 0.5, spawnY, static_cast<double>(z) + 0.5};
        }

        // Spiral walk step
        x += dx;
        z += dz;
        ++segmentPassed;

        if (segmentPassed == segmentLength)
        {
            segmentPassed = 0;
            // Turn 90 degrees
            int temp = dx;
            dx = -dz;
            dz = temp;
            ++turnCount;
            if (turnCount % 2 == 0)
            {
                ++segmentLength;
            }
        }
    }

    // Fallback: just use origin with computed height
    float baseHeight = computeBaseHeight(0, 0);
    BlendedBiome blended = m_biomeSystem.getBlendedBiomeAt(0.0f, 0.0f);
    float detailNoise = getDetailNoise(0, 0);
    float finalHeight =
        baseHeight + blended.blendedHeightModifier + detailNoise * DETAIL_AMPLITUDE * blended.blendedHeightScale;
    finalHeight = std::clamp(finalHeight, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT);
    return {0.5, static_cast<double>(static_cast<int>(finalHeight)) + 2.0, 0.5};
}

} // namespace voxel::world
