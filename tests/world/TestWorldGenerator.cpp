#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSection.h"
#include "voxel/world/WorldGenerator.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace voxel::world;

namespace
{

/// Helper: register the four terrain blocks needed by WorldGenerator and return the registry.
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

    return registry;
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

    uint16_t grassId = registry.getIdByName("base:grass_block");
    REQUIRE(grassId != BLOCK_AIR);

    // Check several chunks at different positions
    glm::ivec2 coords[] = {{0, 0}, {5, 5}, {-3, 7}, {10, -10}};

    for (auto coord : coords)
    {
        ChunkColumn col = gen.generateChunkColumn(coord);

        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                // Find grass block (surface)
                for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
                {
                    if (col.getBlock(x, y, z) == grassId)
                    {
                        REQUIRE(y >= 1);
                        REQUIRE(y <= 254);
                        break;
                    }
                }
            }
        }
    }
}

// ── Surface composition ───────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: surface composition is correct", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(99999, registry);

    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t dirtId = registry.getIdByName("base:dirt");
    uint16_t grassId = registry.getIdByName("base:grass_block");
    uint16_t bedrockId = registry.getIdByName("base:bedrock");

    ChunkColumn col = gen.generateChunkColumn({0, 0});

    for (int x = 0; x < ChunkSection::SIZE; ++x)
    {
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            // Bedrock at y=0
            REQUIRE(col.getBlock(x, 0, z) == bedrockId);

            // Find surface (grass block)
            int surfaceY = -1;
            for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
            {
                if (col.getBlock(x, y, z) == grassId)
                {
                    surfaceY = y;
                    break;
                }
            }
            REQUIRE(surfaceY > 0);

            // Dirt below grass (up to 3 layers)
            int dirtCount = 0;
            for (int y = surfaceY - 1; y >= 1; --y)
            {
                if (col.getBlock(x, y, z) == dirtId)
                {
                    ++dirtCount;
                }
                else
                {
                    break;
                }
            }
            REQUIRE(dirtCount <= 3);
            // There should be dirt layers unless surface is very low
            if (surfaceY >= 4)
            {
                REQUIRE(dirtCount == 3);
            }

            // Stone below dirt
            int stoneTop = surfaceY - 1 - dirtCount;
            if (stoneTop >= 1)
            {
                REQUIRE(col.getBlock(x, stoneTop, z) == stoneId);
            }

            // Air above surface
            if (surfaceY + 1 < ChunkColumn::COLUMN_HEIGHT)
            {
                REQUIRE(col.getBlock(x, surfaceY + 1, z) == BLOCK_AIR);
            }
        }
    }
}

// ── Spawn point ───────────────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: spawn point is above ground", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(7777, registry);

    glm::dvec3 spawn = gen.findSpawnPoint();

    // Spawn Y should be height + 2; with spline range [1, 254], spawn in [3, 256]
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

// ── Height distribution ──────────────────────────────────────────────────────

TEST_CASE("WorldGenerator: terrain has plains, hills, and mountains", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(54321, registry);

    uint16_t grassId = registry.getIdByName("base:grass_block");
    REQUIRE(grassId != BLOCK_AIR);

    int plainsCount = 0;   // heights in [55, 70]
    int hillsCount = 0;    // heights in [80, 100]
    int mountainCount = 0; // heights in [120, 254]
    int totalColumns = 0;

    // Generate many chunks across a wide area to sample the full noise range
    for (int cx = -20; cx <= 20; cx += 5)
    {
        for (int cz = -20; cz <= 20; cz += 5)
        {
            ChunkColumn col = gen.generateChunkColumn({cx, cz});

            for (int x = 0; x < ChunkSection::SIZE; x += 4)
            {
                for (int z = 0; z < ChunkSection::SIZE; z += 4)
                {
                    // Find grass block (surface)
                    for (int y = ChunkColumn::COLUMN_HEIGHT - 1; y >= 0; --y)
                    {
                        if (col.getBlock(x, y, z) == grassId)
                        {
                            ++totalColumns;
                            if (y >= 55 && y <= 70)
                            {
                                ++plainsCount;
                            }
                            if (y >= 80 && y <= 100)
                            {
                                ++hillsCount;
                            }
                            if (y >= 120)
                            {
                                ++mountainCount;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // Verify we sampled enough columns
    REQUIRE(totalColumns > 100);

    // Verify terrain diversity: each category should have at least some representation
    INFO("plains=" << plainsCount << " hills=" << hillsCount << " mountains=" << mountainCount
                   << " total=" << totalColumns);
    REQUIRE(plainsCount > 0);
    REQUIRE(hillsCount > 0);
    REQUIRE(mountainCount > 0);
}
