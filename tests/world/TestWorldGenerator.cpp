#include "voxel/world/BiomeSystem.h"
#include "voxel/world/BiomeTypes.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/CaveCarver.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSection.h"
#include "voxel/world/WorldGenerator.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <set>

using namespace voxel::world;

namespace
{

/// Helper: register all terrain blocks needed by WorldGenerator (including biome blocks) and return the registry.
BlockRegistry makeTerrainRegistry()
{
    BlockRegistry registry;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    (void)registry.registerBlock(std::move(stone));

    BlockDefinition dirt;
    dirt.stringId = "base:dirt";
    (void)registry.registerBlock(std::move(dirt));

    BlockDefinition grass;
    grass.stringId = "base:grass_block";
    (void)registry.registerBlock(std::move(grass));

    BlockDefinition bedrock;
    bedrock.stringId = "base:bedrock";
    (void)registry.registerBlock(std::move(bedrock));

    BlockDefinition sand;
    sand.stringId = "base:sand";
    (void)registry.registerBlock(std::move(sand));

    BlockDefinition sandstone;
    sandstone.stringId = "base:sandstone";
    (void)registry.registerBlock(std::move(sandstone));

    BlockDefinition snow;
    snow.stringId = "base:snow_block";
    (void)registry.registerBlock(std::move(snow));

    return registry;
}

/// Get the set of all surface block IDs for all biomes.
std::set<uint16_t> getAllSurfaceBlockIds(const BlockRegistry& registry)
{
    std::set<uint16_t> ids;
    for (size_t i = 0; i < static_cast<size_t>(BiomeType::Count); ++i)
    {
        const BiomeDefinition& def = getBiomeDefinition(static_cast<BiomeType>(i));
        ids.insert(registry.getIdByName(def.surfaceBlock));
    }
    return ids;
}

/// Find the surface Y (highest non-air block) at a column.
int findSurfaceY(const ChunkColumn& col, int x, int z)
{
    for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
    {
        if (col.getBlock(x, y, z) != BLOCK_AIR)
        {
            return y;
        }
    }
    return -1;
}

} // namespace

// ── Determinism ───────────────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: determinism — same seed and coord produce identical columns", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    constexpr uint64_t SEED = 42;

    WorldGenerator gen1(SEED, registry);
    WorldGenerator gen2(SEED, registry);

    glm::ivec2 coord{3, -7};
    ChunkColumn col1 = gen1.generateChunkColumn(coord);
    ChunkColumn col2 = gen2.generateChunkColumn(coord);

    // Compare every block in every section
    for (int y = 0; y < ChunkColumn::COLUMN_HEIGHT; ++y)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                REQUIRE(col1.getBlock(x, y, z) == col2.getBlock(x, y, z));
            }
        }
    }
}

// ── Height bounds ─────────────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: surface blocks are within [1, 254]", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(12345, registry);

    // Check several chunks at different positions
    glm::ivec2 coords[] = {{0, 0}, {5, 5}, {-3, 7}, {10, -10}};

    for (auto coord : coords)
    {
        ChunkColumn col = gen.generateChunkColumn(coord);

        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                int surfaceY = findSurfaceY(col, x, z);
                REQUIRE(surfaceY >= 1);
                REQUIRE(surfaceY <= 254);
            }
        }
    }
}

// ── Surface composition ───────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: surface composition follows biome definitions", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(99999, registry);

    uint16_t bedrockId = registry.getIdByName("base:bedrock");
    std::set<uint16_t> validSurface = getAllSurfaceBlockIds(registry);

    ChunkColumn col = gen.generateChunkColumn({0, 0});

    int matchCount = 0;
    int totalChecked = 0;

    for (int x = 0; x < ChunkSection::SIZE; ++x)
    {
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            // Bedrock at y=0
            REQUIRE(col.getBlock(x, 0, z) == bedrockId);

            // Find surface (highest non-air)
            int surfaceY = -1;
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                if (col.getBlock(x, y, z) != BLOCK_AIR)
                {
                    surfaceY = y;
                    break;
                }
            }
            REQUIRE(surfaceY > 0);

            // Air above surface
            if (surfaceY + 1 < ChunkColumn::COLUMN_HEIGHT)
            {
                REQUIRE(col.getBlock(x, surfaceY + 1, z) == BLOCK_AIR);
            }

            // Surface block: most should be biome surface blocks,
            // but cave openings can expose sub-surface/filler blocks
            uint16_t surfaceBlock = col.getBlock(x, surfaceY, z);
            ++totalChecked;
            if (validSurface.count(surfaceBlock) > 0)
            {
                ++matchCount;
            }
        }
    }

    // Most columns should have intact biome surface blocks (caves breach only a minority)
    float matchRate = static_cast<float>(matchCount) / static_cast<float>(totalChecked);
    INFO("Surface biome match rate: " << matchRate << " (" << matchCount << "/" << totalChecked << ")");
    REQUIRE(matchRate > 0.7f);
}

// ── Spawn point ───────────────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: spawn point is above ground", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(7777, registry);

    glm::dvec3 spawn = gen.findSpawnPoint();

    // Spawn Y should be height + 2; with terrain range [1, 254], spawn in [3, 256]
    REQUIRE(spawn.y >= 3.0);
    REQUIRE(spawn.y <= 256.0);

    // Spawn should be at block center (x.5, z.5)
    double fracX = spawn.x - std::floor(spawn.x);
    double fracZ = spawn.z - std::floor(spawn.z);
    REQUIRE(fracX == 0.5);
    REQUIRE(fracZ == 0.5);
}

// ── Different seeds ───────────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: different seeds produce different terrain", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen1(111, registry);
    WorldGenerator gen2(222, registry);

    ChunkColumn col1 = gen1.generateChunkColumn({0, 0});
    ChunkColumn col2 = gen2.generateChunkColumn({0, 0});

    // Count differences — different seeds should produce at least some different blocks
    int differences = 0;
    for (int y = 0; y < ChunkColumn::COLUMN_HEIGHT; ++y)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                if (col1.getBlock(x, y, z) != col2.getBlock(x, y, z))
                {
                    ++differences;
                }
            }
        }
    }

    REQUIRE(differences > 0);
}

// ── Biome integration: surface blocks match biome ────────────────────────────

TEST_CASE("WorldGenerator: surface blocks match biome definitions", "[world][worldgenerator][biome]")
{
    BlockRegistry registry = makeTerrainRegistry();
    constexpr uint64_t SEED = 54321;
    WorldGenerator gen(SEED, registry);
    BiomeSystem biomes(SEED);

    // Generate several chunks and verify surface block matches expected biome
    glm::ivec2 coords[] = {{0, 0}, {10, 10}, {-15, 5}, {20, -20}};

    int matchCount = 0;
    int totalChecked = 0;

    for (auto coord : coords)
    {
        ChunkColumn col = gen.generateChunkColumn(coord);

        for (int lx = 0; lx < ChunkSection::SIZE; lx += 4)
        {
            for (int lz = 0; lz < ChunkSection::SIZE; lz += 4)
            {
                int worldX = coord.x * ChunkSection::SIZE + lx;
                int worldZ = coord.y * ChunkSection::SIZE + lz;

                // Find surface
                int surfaceY = -1;
                for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
                {
                    if (col.getBlock(lx, y, lz) != BLOCK_AIR)
                    {
                        surfaceY = y;
                        break;
                    }
                }
                if (surfaceY <= 0)
                {
                    continue;
                }

                // Get expected biome surface block
                BlendedBiome blended =
                    biomes.getBlendedBiomeAt(static_cast<float>(worldX), static_cast<float>(worldZ));
                const BiomeDefinition& def = getBiomeDefinition(blended.primaryBiome);
                uint16_t expectedSurface = registry.getIdByName(def.surfaceBlock);

                uint16_t actualSurface = col.getBlock(lx, surfaceY, lz);

                ++totalChecked;
                if (actualSurface == expectedSurface)
                {
                    ++matchCount;
                }
            }
        }
    }

    REQUIRE(totalChecked > 0);
    // Most surface blocks should match their biome (some differ due to blending + cave openings)
    float matchRate = static_cast<float>(matchCount) / static_cast<float>(totalChecked);
    INFO("Biome surface match rate: " << matchRate << " (" << matchCount << "/" << totalChecked << ")");
    REQUIRE(matchRate > 0.7f);
}

// ── Biome integration: terrain determinism preserved ─────────────────────────

TEST_CASE("WorldGenerator: terrain determinism preserved with biome system", "[world][worldgenerator][biome]")
{
    BlockRegistry registry = makeTerrainRegistry();
    constexpr uint64_t SEED = 12345;

    WorldGenerator gen1(SEED, registry);
    WorldGenerator gen2(SEED, registry);

    // Generate same chunk twice with same seed — must be identical
    glm::ivec2 coord{-5, 8};
    ChunkColumn col1 = gen1.generateChunkColumn(coord);
    ChunkColumn col2 = gen2.generateChunkColumn(coord);

    for (int y = 0; y < ChunkColumn::COLUMN_HEIGHT; ++y)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                REQUIRE(col1.getBlock(x, y, z) == col2.getBlock(x, y, z));
            }
        }
    }
}

// ── Biome integration: smooth height transitions at boundaries ───────────────

TEST_CASE("WorldGenerator: biome boundaries produce smooth height transitions", "[world][worldgenerator][biome]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(42, registry);

    // Generate a strip of chunks and verify no extreme height jumps between adjacent columns
    int maxJump = 0;

    for (int cx = -10; cx <= 10; ++cx)
    {
        ChunkColumn col = gen.generateChunkColumn({cx, 0});

        int prevHeight = -1;
        for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
        {
            // Find surface Y at (lx, 0)
            int surfaceY = -1;
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                if (col.getBlock(lx, y, 0) != BLOCK_AIR)
                {
                    surfaceY = y;
                    break;
                }
            }

            if (prevHeight >= 0 && surfaceY >= 0)
            {
                int jump = std::abs(surfaceY - prevHeight);
                if (jump > maxJump)
                {
                    maxJump = jump;
                }
            }
            prevHeight = surfaceY;
        }
    }

    // Adjacent columns should not jump more than ~45 blocks
    // (biome blending smooths terrain, but cave surface openings can expose deep chambers)
    INFO("Max height jump between adjacent columns: " << maxJump);
    REQUIRE(maxJump <= 45);
}

// ── Height distribution (adapted for biome height modifiers) ─────────────────

TEST_CASE("WorldGenerator: terrain has varied heights with biomes", "[world][worldgenerator][biome]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(54321, registry);

    int lowCount = 0;     // heights in [1, 60]
    int midCount = 0;     // heights in [61, 100]
    int highCount = 0;    // heights in [101, 254]
    int totalColumns = 0;

    // Generate many chunks across a wide area
    for (int cx = -20; cx <= 20; cx += 5)
    {
        for (int cz = -20; cz <= 20; cz += 5)
        {
            ChunkColumn col = gen.generateChunkColumn({cx, cz});

            for (int x = 0; x < ChunkSection::SIZE; x += 4)
            {
                for (int z = 0; z < ChunkSection::SIZE; z += 4)
                {
                    // Find surface
                    for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
                    {
                        if (col.getBlock(x, y, z) != BLOCK_AIR)
                        {
                            ++totalColumns;
                            if (y <= 60)
                            {
                                ++lowCount;
                            }
                            else if (y <= 100)
                            {
                                ++midCount;
                            }
                            else
                            {
                                ++highCount;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    REQUIRE(totalColumns > 100);

    INFO("low=" << lowCount << " mid=" << midCount << " high=" << highCount << " total=" << totalColumns);
    REQUIRE(lowCount > 0);
    REQUIRE(midCount > 0);
    REQUIRE(highCount > 0);
}

// ── Cave integration: determinism preserved with cave carver ─────────────────

TEST_CASE("WorldGenerator: terrain determinism preserved with cave carver", "[world][worldgenerator][cave]")
{
    BlockRegistry registry = makeTerrainRegistry();
    constexpr uint64_t SEED = 31415;

    WorldGenerator gen1(SEED, registry);
    WorldGenerator gen2(SEED, registry);

    glm::ivec2 coord{-2, 4};
    ChunkColumn col1 = gen1.generateChunkColumn(coord);
    ChunkColumn col2 = gen2.generateChunkColumn(coord);

    for (int y = 0; y < ChunkColumn::COLUMN_HEIGHT; ++y)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                REQUIRE(col1.getBlock(x, y, z) == col2.getBlock(x, y, z));
            }
        }
    }
}

// ── Cave integration: generated chunk has caves at mid-depth ─────────────────

TEST_CASE("WorldGenerator: generated chunk has caves at mid-depth", "[world][worldgenerator][cave]")
{
    BlockRegistry registry = makeTerrainRegistry();
    constexpr uint64_t SEED = 77777;

    WorldGenerator gen(SEED, registry);

    int totalMidAir = 0;
    int totalMidBlocks = 0;

    // Check many chunks to find caves
    for (int cx = -5; cx <= 5; ++cx)
    {
        for (int cz = -5; cz <= 5; ++cz)
        {
            ChunkColumn col = gen.generateChunkColumn({cx, cz});

            for (int y = 30; y <= 80; ++y)
            {
                for (int x = 0; x < ChunkSection::SIZE; ++x)
                {
                    for (int z = 0; z < ChunkSection::SIZE; ++z)
                    {
                        ++totalMidBlocks;
                        if (col.getBlock(x, y, z) == BLOCK_AIR)
                        {
                            ++totalMidAir;
                        }
                    }
                }
            }
        }
    }

    INFO("Air blocks at mid-depth [30-80] across 121 chunks: " << totalMidAir << " / " << totalMidBlocks);
    // Some chunks may have terrain below y=30-80, so air is expected from terrain gaps AND caves.
    // With caves enabled, there should be carved blocks within solid terrain.
    REQUIRE(totalMidAir > 0);
}
