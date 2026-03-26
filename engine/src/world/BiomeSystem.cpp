#include "voxel/world/BiomeSystem.h"

#include <algorithm>
#include <cmath>

namespace voxel::world
{

// 4x4 Whittaker-style lookup table.
// Rows = temperature (cold → hot), Columns = humidity (low → high).
static constexpr BiomeType WHITTAKER_TABLE[4][4] = {
    // Cold (0–0.25)
    {BiomeType::IcePlains, BiomeType::Tundra, BiomeType::Tundra, BiomeType::Taiga},
    // Cool (0.25–0.5)
    {BiomeType::Tundra, BiomeType::Plains, BiomeType::Taiga, BiomeType::Taiga},
    // Warm (0.5–0.75)
    {BiomeType::Desert, BiomeType::Savanna, BiomeType::Plains, BiomeType::Forest},
    // Hot (0.75–1.0)
    {BiomeType::Desert, BiomeType::Savanna, BiomeType::Jungle, BiomeType::Jungle},
};

static constexpr float CLIMATE_FREQUENCY = 0.005f;
static constexpr int CLIMATE_OCTAVES = 4;

// Blending parameters
static constexpr int BLEND_RADIUS = 2;                   // -2..+2 = 5x5 grid
static constexpr float BLEND_STEP = 4.0f;                // Block spacing between samples
static constexpr int BLEND_SAMPLES = (2 * BLEND_RADIUS + 1) * (2 * BLEND_RADIUS + 1); // 25

BiomeSystem::BiomeSystem(uint64_t seed)
{
    int baseSeed = static_cast<int>(seed & 0x7FFFFFFF);

    // Temperature noise: deterministic offset +2 from world seed
    m_temperatureNoise.SetSeed(baseSeed + 2);
    m_temperatureNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise.SetFractalOctaves(CLIMATE_OCTAVES);
    m_temperatureNoise.SetFrequency(CLIMATE_FREQUENCY);

    // Humidity noise: deterministic offset +3 from world seed
    m_humidityNoise.SetSeed(baseSeed + 3);
    m_humidityNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_humidityNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_humidityNoise.SetFractalOctaves(CLIMATE_OCTAVES);
    m_humidityNoise.SetFrequency(CLIMATE_FREQUENCY);
}

BiomeType BiomeSystem::classifyBiome(float temperature, float humidity)
{
    // Clamp to [0, 1] for safety
    temperature = std::clamp(temperature, 0.0f, 1.0f);
    humidity = std::clamp(humidity, 0.0f, 1.0f);

    // Map [0, 1] to table index [0, 3]. The 3.99f avoids out-of-bounds when value == 1.0.
    int tempIdx = static_cast<int>(temperature * 3.99f);
    int humIdx = static_cast<int>(humidity * 3.99f);

    return WHITTAKER_TABLE[tempIdx][humIdx];
}

BiomeType BiomeSystem::getBiomeAt(float worldX, float worldZ) const
{
    // Sample noise in [-1, 1], remap to [0, 1]
    float rawTemp = m_temperatureNoise.GetNoise(worldX, worldZ);
    float rawHum = m_humidityNoise.GetNoise(worldX, worldZ);

    float temperature = (rawTemp + 1.0f) * 0.5f;
    float humidity = (rawHum + 1.0f) * 0.5f;

    return classifyBiome(temperature, humidity);
}

BlendedBiome BiomeSystem::getBlendedBiomeAt(float worldX, float worldZ) const
{
    // Per-biome accumulated weights
    static constexpr size_t BIOME_COUNT = static_cast<size_t>(BiomeType::Count);
    float biomeWeights[BIOME_COUNT] = {};
    float totalWeight = 0.0f;

    // Accumulators for weighted averages
    float weightedHeightMod = 0.0f;
    float weightedHeightScale = 0.0f;
    float weightedSurfaceDepth = 0.0f;

    // 5x5 sampling grid centered on (worldX, worldZ)
    for (int dx = -BLEND_RADIUS; dx <= BLEND_RADIUS; ++dx)
    {
        for (int dz = -BLEND_RADIUS; dz <= BLEND_RADIUS; ++dz)
        {
            float sampleX = worldX + static_cast<float>(dx) * BLEND_STEP;
            float sampleZ = worldZ + static_cast<float>(dz) * BLEND_STEP;

            BiomeType biome = getBiomeAt(sampleX, sampleZ);
            const BiomeDefinition& def = getBiomeDefinition(biome);

            // Distance-weighted: w = 1 / (d² + 1)
            float distSq = static_cast<float>(dx * dx + dz * dz) * (BLEND_STEP * BLEND_STEP);
            float weight = 1.0f / (distSq + 1.0f);

            biomeWeights[static_cast<size_t>(biome)] += weight;
            totalWeight += weight;

            weightedHeightMod += def.heightModifier * weight;
            weightedHeightScale += def.heightScale * weight;
            weightedSurfaceDepth += static_cast<float>(def.surfaceDepth) * weight;
        }
    }

    // Normalize
    float invTotal = 1.0f / totalWeight;

    // Find primary biome (highest accumulated weight)
    BiomeType primaryBiome = BiomeType::Plains;
    float maxWeight = 0.0f;
    for (size_t i = 0; i < BIOME_COUNT; ++i)
    {
        if (biomeWeights[i] > maxWeight)
        {
            maxWeight = biomeWeights[i];
            primaryBiome = static_cast<BiomeType>(i);
        }
    }

    return BlendedBiome{
        primaryBiome,
        weightedHeightMod * invTotal,
        weightedHeightScale * invTotal,
        weightedSurfaceDepth * invTotal,
    };
}

} // namespace voxel::world
