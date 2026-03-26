#include "voxel/world/CaveCarver.h"

#include "voxel/world/Block.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSection.h"

#include <algorithm>
#include <cmath>

namespace voxel::world
{

// Cheese noise: large open cavities (Swiss cheese holes)
static constexpr float CHEESE_FREQUENCY = 0.02f;
static constexpr int CHEESE_OCTAVES = 3;
static constexpr int CHEESE_SEED_OFFSET = 4;

// Spaghetti noise: narrow winding tunnels
static constexpr float SPAGHETTI_FREQUENCY = 0.03f;
static constexpr int SPAGHETTI_OCTAVES = 2;
static constexpr int SPAGHETTI_SEED_OFFSET = 5;

// Y-axis stretch factor for spaghetti noise (makes tunnels horizontal)
static constexpr float SPAGHETTI_Y_SCALE = 0.33f;

// Spaghetti isosurface width control (larger = thinner tunnels)
static constexpr float SPAGHETTI_WIDTH_FACTOR = 4.0f;

// Spaghetti isosurface target value
static constexpr float SPAGHETTI_TARGET = 0.5f;

// Max Y offset above surface to consider for carving
static constexpr int SURFACE_OVERSHOOT = 5;

CaveCarver::CaveCarver(uint64_t seed)
{
    int cheeseSeed = static_cast<int>(seed + CHEESE_SEED_OFFSET);
    m_cheeseNoise.SetSeed(cheeseSeed);
    m_cheeseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_cheeseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_cheeseNoise.SetFractalOctaves(CHEESE_OCTAVES);
    m_cheeseNoise.SetFrequency(CHEESE_FREQUENCY);

    int spaghettiSeed = static_cast<int>(seed + SPAGHETTI_SEED_OFFSET);
    m_spaghettiNoise.SetSeed(spaghettiSeed);
    m_spaghettiNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_spaghettiNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_spaghettiNoise.SetFractalOctaves(SPAGHETTI_OCTAVES);
    m_spaghettiNoise.SetFrequency(SPAGHETTI_FREQUENCY);
}

float CaveCarver::getThreshold(int y, int surfaceHeight)
{
    // Bedrock protection zone: never carve
    if (y <= 5)
    {
        return 1.0f;
    }

    // Above surface + overshoot: don't carve
    if (y > surfaceHeight + SURFACE_OVERSHOOT)
    {
        return 1.0f;
    }

    // --- Component 1: depth-based threshold (absolute Y) ---
    float depthThreshold;
    if (y <= 20)
    {
        float t = static_cast<float>(y - 5) / 15.0f;
        depthThreshold = std::lerp(0.95f, 0.82f, t);
    }
    else if (y <= 50)
    {
        float t = static_cast<float>(y - 20) / 30.0f;
        depthThreshold = std::lerp(0.82f, 0.78f, t);
    }
    else
    {
        depthThreshold = 0.78f; // peak cave density (~3-5% carving)
    }

    // --- Component 2: surface-relative protection ---
    // Always raises threshold within 15 blocks of the surface,
    // regardless of absolute Y (protects low terrain too).
    float surfaceThreshold = 0.0f;
    int distBelow = surfaceHeight - y;
    if (distBelow < 15)
    {
        // t goes from 0.0 at 15 blocks below surface to 1.0 at 5 blocks above
        float t = 1.0f - static_cast<float>(distBelow + SURFACE_OVERSHOOT) /
                             static_cast<float>(15 + SURFACE_OVERSHOOT);
        t = std::clamp(t, 0.0f, 1.0f);
        surfaceThreshold = std::lerp(0.78f, 0.92f, t);
    }

    // Take the more restrictive of the two
    return std::max(depthThreshold, surfaceThreshold);
}

bool CaveCarver::shouldCarve(float worldX, float worldY, float worldZ, int surfaceHeight) const
{
    int y = static_cast<int>(worldY);

    // Skip bedrock protection zone entirely (avoid noise evaluation)
    if (y <= 5)
    {
        return false;
    }

    float threshold = getThreshold(y, surfaceHeight);

    // Skip if threshold is at maximum (would never carve)
    if (threshold >= 1.0f)
    {
        return false;
    }

    // Cheese noise: large cavities
    float cheese = m_cheeseNoise.GetNoise(worldX, worldY, worldZ);
    cheese = (cheese + 1.0f) * 0.5f; // remap [-1,1] to [0,1]

    // Spaghetti noise: narrow tunnels via isosurface intersection
    float spaghetti = m_spaghettiNoise.GetNoise(worldX, worldY * SPAGHETTI_Y_SCALE, worldZ);
    spaghetti = (spaghetti + 1.0f) * 0.5f; // remap [-1,1] to [0,1]

    // Spaghetti carve peaks at target value, falls off at distance
    float spaghettiCarve = 1.0f - std::abs(spaghetti - SPAGHETTI_TARGET) * SPAGHETTI_WIDTH_FACTOR;
    spaghettiCarve = std::clamp(spaghettiCarve, 0.0f, 1.0f);

    // Combined density: carve if either system wants to carve
    float density = std::max(cheese, spaghettiCarve);

    return density > threshold;
}

void CaveCarver::carveColumn(ChunkColumn& column, glm::ivec2 chunkCoord, const int surfaceHeights[16][16]) const
{
    for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
    {
        for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
        {
            float worldX = static_cast<float>(chunkCoord.x * ChunkSection::SIZE + lx);
            float worldZ = static_cast<float>(chunkCoord.y * ChunkSection::SIZE + lz);
            int surfaceH = surfaceHeights[lx][lz];

            int maxY = std::min(surfaceH + SURFACE_OVERSHOOT, ChunkColumn::COLUMN_HEIGHT - 1);

            // Start at y=1 to always protect bedrock at y=0
            for (int y = 1; y <= maxY; ++y)
            {
                if (column.getBlock(lx, y, lz) != BLOCK_AIR)
                {
                    float worldY = static_cast<float>(y);
                    if (shouldCarve(worldX, worldY, worldZ, surfaceH))
                    {
                        column.setBlock(lx, y, lz, BLOCK_AIR);
                    }
                }
            }
        }
    }
}

} // namespace voxel::world
