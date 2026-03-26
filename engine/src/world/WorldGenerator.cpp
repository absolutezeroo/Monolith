#include "voxel/world/WorldGenerator.h"

#include "voxel/core/Log.h"
#include "voxel/world/Block.h"

#include <algorithm>
#include <cmath>

namespace voxel::world
{

static constexpr int DIRT_LAYERS = 3;
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
    , m_dirtId(resolveBlockId(registry, "base:dirt", 1))
    , m_grassId(resolveBlockId(registry, "base:grass_block", 1))
    , m_spline(SplineCurve::createDefault())
{
    // FNL accepts int (32-bit) seeds; mask to 31 bits for positive range.
    // Upper 33 bits of the uint64_t seed are unused — this is an FNL API limitation.
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
}

int WorldGenerator::computeHeight(int worldX, int worldZ) const
{
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);

    // Continent noise → spline-remapped base height
    float continentNoise = m_continentNoise.GetNoise(wx, wz);
    float baseHeight = m_spline.evaluate(continentNoise);

    // Detail noise → local terrain variation
    float detailNoise = m_detailNoise.GetNoise(wx, wz);
    float finalHeight = baseHeight + detailNoise * DETAIL_AMPLITUDE;

    // Clamp to valid column bounds
    finalHeight = std::clamp(finalHeight, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT);

    return static_cast<int>(finalHeight);
}

ChunkColumn WorldGenerator::generateChunkColumn(glm::ivec2 chunkCoord) const
{
    ChunkColumn column(chunkCoord);

    for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
    {
        for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
        {
            int worldX = chunkCoord.x * ChunkSection::SIZE + lx;
            int worldZ = chunkCoord.y * ChunkSection::SIZE + lz;
            int height = computeHeight(worldX, worldZ);

            // Bedrock at y=0
            column.setBlock(lx, 0, lz, m_bedrockId);

            // Stone from y=1 up to (height - DIRT_LAYERS - 1)
            int stoneTop = std::max(0, height - DIRT_LAYERS - 1);
            for (int y = 1; y <= stoneTop; ++y)
            {
                column.setBlock(lx, y, lz, m_stoneId);
            }

            // Dirt layers
            int dirtStart = stoneTop + 1;
            for (int y = dirtStart; y < height; ++y)
            {
                column.setBlock(lx, y, lz, m_dirtId);
            }

            // Grass on top
            column.setBlock(lx, height, lz, m_grassId);
        }
    }

    return column;
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
        int height = computeHeight(x, z);

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
    int fallbackHeight = computeHeight(0, 0);
    return {0.5, static_cast<double>(fallbackHeight) + 2.0, 0.5};
}

} // namespace voxel::world
