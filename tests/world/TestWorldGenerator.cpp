#include "voxel/world/WorldGenerator.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSection.h"

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
    stone.stringId = "voxelforge:stone";
    (void)registry.registerBlock(std::move(stone));

    BlockDefinition dirt;
    dirt.stringId = "voxelforge:dirt";
    (void)registry.registerBlock(std::move(dirt));

    BlockDefinition grass;
    grass.stringId = "voxelforge:grass_block";
    (void)registry.registerBlock(std::move(grass));

    BlockDefinition bedrock;
    bedrock.stringId = "voxelforge:bedrock";
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

TEST_CASE("WorldGenerator: surface blocks are within [40, 120]", "[world][worldgenerator]")
{
    BlockRegistry registry = makeTerrainRegistry();
    WorldGenerator gen(12345, registry);

    uint16_t grassId = registry.getIdByName("voxelforge:grass_block");
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
                        REQUIRE(y >= 40);
                        REQUIRE(y <= 120);
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

    uint16_t stoneId = registry.getIdByName("voxelforge:stone");
    uint16_t dirtId = registry.getIdByName("voxelforge:dirt");
    uint16_t grassId = registry.getIdByName("voxelforge:grass_block");
    uint16_t bedrockId = registry.getIdByName("voxelforge:bedrock");

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

    // Spawn Y should be at least 42 (min height 40 + 2)
    REQUIRE(spawn.y >= 42.0);
    REQUIRE(spawn.y <= 122.0); // max height 120 + 2

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
