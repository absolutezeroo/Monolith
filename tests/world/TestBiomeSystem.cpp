#include "voxel/world/BiomeSystem.h"
#include "voxel/world/BiomeTypes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <set>

using namespace voxel::world;

// ── Whittaker lookup coverage ────────────────────────────────────────────────

TEST_CASE("BiomeSystem: classifyBiome covers all 8 biome types", "[world][biome]")
{
    std::set<BiomeType> found;

    // Sample the 4x4 grid centers
    for (int t = 0; t < 4; ++t)
    {
        for (int h = 0; h < 4; ++h)
        {
            float temp = (static_cast<float>(t) + 0.5f) / 4.0f;
            float hum = (static_cast<float>(h) + 0.5f) / 4.0f;
            found.insert(BiomeSystem::classifyBiome(temp, hum));
        }
    }

    REQUIRE(found.size() == static_cast<size_t>(BiomeType::Count));
}

// ── classifyBiome specific values ────────────────────────────────────────────

TEST_CASE("BiomeSystem: classifyBiome returns expected biomes for known inputs", "[world][biome]")
{
    // Hot + dry = Desert
    REQUIRE(BiomeSystem::classifyBiome(0.9f, 0.1f) == BiomeType::Desert);

    // Cold + dry = IcePlains
    REQUIRE(BiomeSystem::classifyBiome(0.1f, 0.1f) == BiomeType::IcePlains);

    // Hot + wet = Jungle
    REQUIRE(BiomeSystem::classifyBiome(0.9f, 0.9f) == BiomeType::Jungle);

    // Cold + wet = Taiga
    REQUIRE(BiomeSystem::classifyBiome(0.1f, 0.9f) == BiomeType::Taiga);

    // Warm + medium-high humidity = Plains
    REQUIRE(BiomeSystem::classifyBiome(0.6f, 0.6f) == BiomeType::Plains);
}

// ── classifyBiome boundary clamping ──────────────────────────────────────────

TEST_CASE("BiomeSystem: classifyBiome clamps out-of-range inputs", "[world][biome]")
{
    // Should not crash or return invalid biome
    BiomeType b1 = BiomeSystem::classifyBiome(-0.5f, -0.5f);
    REQUIRE(static_cast<uint8_t>(b1) < static_cast<uint8_t>(BiomeType::Count));

    BiomeType b2 = BiomeSystem::classifyBiome(1.5f, 1.5f);
    REQUIRE(static_cast<uint8_t>(b2) < static_cast<uint8_t>(BiomeType::Count));

    BiomeType b3 = BiomeSystem::classifyBiome(1.0f, 1.0f);
    REQUIRE(static_cast<uint8_t>(b3) < static_cast<uint8_t>(BiomeType::Count));
}

// ── getBiomeAt determinism ───────────────────────────────────────────────────

TEST_CASE("BiomeSystem: getBiomeAt is deterministic", "[world][biome]")
{
    constexpr uint64_t SEED = 42;
    BiomeSystem sys1(SEED);
    BiomeSystem sys2(SEED);

    // Same seed + same coords = same biome
    for (float x = -100.0f; x <= 100.0f; x += 37.0f)
    {
        for (float z = -100.0f; z <= 100.0f; z += 41.0f)
        {
            REQUIRE(sys1.getBiomeAt(x, z) == sys2.getBiomeAt(x, z));
        }
    }
}

// ── Different seeds produce different biome maps ─────────────────────────────

TEST_CASE("BiomeSystem: different seeds produce different biome maps", "[world][biome]")
{
    BiomeSystem sys1(111);
    BiomeSystem sys2(999);

    int differences = 0;
    for (float x = -200.0f; x <= 200.0f; x += 13.0f)
    {
        for (float z = -200.0f; z <= 200.0f; z += 17.0f)
        {
            if (sys1.getBiomeAt(x, z) != sys2.getBiomeAt(x, z))
            {
                ++differences;
            }
        }
    }

    REQUIRE(differences > 0);
}

// ── Blending weight normalization ────────────────────────────────────────────

TEST_CASE("BiomeSystem: getBlendedBiomeAt produces valid blended values", "[world][biome]")
{
    BiomeSystem sys(42);

    // Test several positions
    float positions[] = {0.0f, 50.0f, -100.0f, 300.0f, -500.0f};

    for (float x : positions)
    {
        for (float z : positions)
        {
            BlendedBiome blended = sys.getBlendedBiomeAt(x, z);

            // Primary biome must be valid
            REQUIRE(static_cast<uint8_t>(blended.primaryBiome) < static_cast<uint8_t>(BiomeType::Count));

            // Blended height modifier should be within the range of defined biome modifiers [-5, 5]
            REQUIRE(blended.blendedHeightModifier >= -5.5f);
            REQUIRE(blended.blendedHeightModifier <= 5.5f);

            // Blended height scale should be within defined range [0.4, 1.5]
            REQUIRE(blended.blendedHeightScale >= 0.35f);
            REQUIRE(blended.blendedHeightScale <= 1.55f);

            // Blended surface depth should be positive and within defined range [2, 4]
            REQUIRE(blended.blendedSurfaceDepth >= 1.5f);
            REQUIRE(blended.blendedSurfaceDepth <= 4.5f);
        }
    }
}

// ── BiomeDefinition lookup ───────────────────────────────────────────────────

TEST_CASE("BiomeSystem: getBiomeDefinition returns correct data for all types", "[world][biome]")
{
    for (size_t i = 0; i < static_cast<size_t>(BiomeType::Count); ++i)
    {
        auto type = static_cast<BiomeType>(i);
        const BiomeDefinition& def = getBiomeDefinition(type);

        REQUIRE(def.type == type);
        REQUIRE(!def.surfaceBlock.empty());
        REQUIRE(!def.subSurfaceBlock.empty());
        REQUIRE(!def.fillerBlock.empty());
        REQUIRE(def.surfaceDepth >= 1);
        REQUIRE(def.surfaceDepth <= 10);
    }
}

// ── Blending determinism ─────────────────────────────────────────────────────

TEST_CASE("BiomeSystem: getBlendedBiomeAt is deterministic", "[world][biome]")
{
    constexpr uint64_t SEED = 7777;
    BiomeSystem sys1(SEED);
    BiomeSystem sys2(SEED);

    BlendedBiome b1 = sys1.getBlendedBiomeAt(42.0f, -17.0f);
    BlendedBiome b2 = sys2.getBlendedBiomeAt(42.0f, -17.0f);

    REQUIRE(b1.primaryBiome == b2.primaryBiome);
    REQUIRE(b1.blendedHeightModifier == b2.blendedHeightModifier);
    REQUIRE(b1.blendedHeightScale == b2.blendedHeightScale);
    REQUIRE(b1.blendedSurfaceDepth == b2.blendedSurfaceDepth);
}
