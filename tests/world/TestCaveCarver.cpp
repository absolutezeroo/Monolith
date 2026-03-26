#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/CaveCarver.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

using namespace voxel::world;

namespace
{

/// Fill a column with stone (ID 1) from y=0 to surfaceHeight, air above.
/// Returns the stone block ID used.
void fillTestColumn(ChunkColumn& column, int uniformSurfaceHeight, uint16_t stoneId, uint16_t bedrockId)
{
    for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
    {
        for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
        {
            column.setBlock(lx, 0, lz, bedrockId);
            for (int y = 1; y <= uniformSurfaceHeight; ++y)
            {
                column.setBlock(lx, y, lz, stoneId);
            }
        }
    }
}

/// Build surface heights array with a uniform value.
void fillSurfaceHeights(int surfaceHeights[16][16], int height)
{
    for (int x = 0; x < 16; ++x)
    {
        for (int z = 0; z < 16; ++z)
        {
            surfaceHeights[x][z] = height;
        }
    }
}

/// Count air blocks in a Y range within a column (that were originally solid).
int countAirInRange(const ChunkColumn& col, int yMin, int yMax)
{
    int airCount = 0;
    for (int y = yMin; y <= yMax; ++y)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                if (col.getBlock(x, y, z) == BLOCK_AIR)
                {
                    ++airCount;
                }
            }
        }
    }
    return airCount;
}

BlockRegistry makeTestRegistry()
{
    BlockRegistry registry;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    (void)registry.registerBlock(std::move(stone));

    BlockDefinition bedrock;
    bedrock.stringId = "base:bedrock";
    (void)registry.registerBlock(std::move(bedrock));

    return registry;
}

} // namespace

// ── Determinism ──────────────────────────────────────────────────────────────

TEST_CASE("CaveCarver: determinism — same seed produces identical results", "[world][cave]")
{
    constexpr uint64_t SEED = 42;
    constexpr int SURFACE_H = 120;
    glm::ivec2 coord{3, -7};

    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t bedrockId = registry.getIdByName("base:bedrock");

    CaveCarver carver1(SEED);
    CaveCarver carver2(SEED);

    ChunkColumn col1(coord);
    ChunkColumn col2(coord);
    int surfaceHeights[16][16];
    fillSurfaceHeights(surfaceHeights, SURFACE_H);
    fillTestColumn(col1, SURFACE_H, stoneId, bedrockId);
    fillTestColumn(col2, SURFACE_H, stoneId, bedrockId);

    carver1.carveColumn(col1, coord, surfaceHeights);
    carver2.carveColumn(col2, coord, surfaceHeights);

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

// ── Bedrock protection ───────────────────────────────────────────────────────

TEST_CASE("CaveCarver: bedrock at y=0 is never carved", "[world][cave]")
{
    constexpr uint64_t SEED = 12345;
    constexpr int SURFACE_H = 100;

    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t bedrockId = registry.getIdByName("base:bedrock");

    CaveCarver carver(SEED);

    // Test multiple chunk coordinates to be thorough
    glm::ivec2 coords[] = {{0, 0}, {5, -3}, {-10, 10}, {100, 100}};

    for (auto coord : coords)
    {
        ChunkColumn col(coord);
        int surfaceHeights[16][16];
        fillSurfaceHeights(surfaceHeights, SURFACE_H);
        fillTestColumn(col, SURFACE_H, stoneId, bedrockId);

        carver.carveColumn(col, coord, surfaceHeights);

        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                REQUIRE(col.getBlock(x, 0, z) != BLOCK_AIR);
            }
        }
    }
}

// ── Caves exist at mid-depth ─────────────────────────────────────────────────

TEST_CASE("CaveCarver: caves exist at mid-depth", "[world][cave]")
{
    constexpr uint64_t SEED = 77777;
    constexpr int SURFACE_H = 130;

    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t bedrockId = registry.getIdByName("base:bedrock");

    CaveCarver carver(SEED);

    // Check many chunks to ensure at least some have caves
    int totalMidAir = 0;

    for (int cx = -5; cx <= 5; ++cx)
    {
        for (int cz = -5; cz <= 5; ++cz)
        {
            glm::ivec2 coord{cx, cz};
            ChunkColumn col(coord);
            int surfaceHeights[16][16];
            fillSurfaceHeights(surfaceHeights, SURFACE_H);
            fillTestColumn(col, SURFACE_H, stoneId, bedrockId);

            carver.carveColumn(col, coord, surfaceHeights);

            totalMidAir += countAirInRange(col, 30, 80);
        }
    }

    INFO("Total air blocks in y=[30,80] across 121 chunks: " << totalMidAir);
    REQUIRE(totalMidAir > 0);
}

// ── Depth distribution ───────────────────────────────────────────────────────

TEST_CASE("CaveCarver: depth distribution — more caves at mid-depth than near bedrock or surface", "[world][cave]")
{
    constexpr uint64_t SEED = 54321;
    constexpr int SURFACE_H = 150;

    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t bedrockId = registry.getIdByName("base:bedrock");

    CaveCarver carver(SEED);

    int nearBedrockAir = 0;  // y in [5, 20]
    int midDepthAir = 0;     // y in [40, 80]
    int nearSurfaceAir = 0;  // y in [SURFACE_H - 10, SURFACE_H]

    for (int cx = -5; cx <= 5; ++cx)
    {
        for (int cz = -5; cz <= 5; ++cz)
        {
            glm::ivec2 coord{cx, cz};
            ChunkColumn col(coord);
            int surfaceHeights[16][16];
            fillSurfaceHeights(surfaceHeights, SURFACE_H);
            fillTestColumn(col, SURFACE_H, stoneId, bedrockId);

            carver.carveColumn(col, coord, surfaceHeights);

            nearBedrockAir += countAirInRange(col, 5, 20);
            midDepthAir += countAirInRange(col, 40, 80);
            nearSurfaceAir += countAirInRange(col, SURFACE_H - 10, SURFACE_H);
        }
    }

    INFO("Near-bedrock air [5-20]: " << nearBedrockAir);
    INFO("Mid-depth air [40-80]: " << midDepthAir);
    INFO("Near-surface air [" << (SURFACE_H - 10) << "-" << SURFACE_H << "]: " << nearSurfaceAir);

    // Mid-depth should have more caves than near-bedrock or near-surface
    // due to lower threshold in the peak cave zone
    REQUIRE(midDepthAir > nearBedrockAir);
    REQUIRE(midDepthAir > nearSurfaceAir);
}

// ── Threshold curve ──────────────────────────────────────────────────────────

TEST_CASE("CaveCarver: threshold curve returns expected values", "[world][cave]")
{
    constexpr int SURFACE_H = 120;

    SECTION("Bedrock zone (y <= 5) returns 1.0")
    {
        REQUIRE(CaveCarver::getThreshold(0, SURFACE_H) == 1.0f);
        REQUIRE(CaveCarver::getThreshold(3, SURFACE_H) == 1.0f);
        REQUIRE(CaveCarver::getThreshold(5, SURFACE_H) == 1.0f);
    }

    SECTION("Peak cave zone (y=60) has threshold ~0.78")
    {
        float threshold = CaveCarver::getThreshold(60, SURFACE_H);
        REQUIRE(threshold >= 0.73f);
        REQUIRE(threshold <= 0.83f);
    }

    SECTION("Near bedrock (y=10) has high threshold")
    {
        float threshold = CaveCarver::getThreshold(10, SURFACE_H);
        REQUIRE(threshold > 0.82f);
    }

    SECTION("Near surface has higher threshold than mid-depth")
    {
        float midThreshold = CaveCarver::getThreshold(60, SURFACE_H);
        float nearSurfaceThreshold = CaveCarver::getThreshold(SURFACE_H - 2, SURFACE_H);
        REQUIRE(nearSurfaceThreshold > midThreshold);
    }

    SECTION("Threshold is monotonically decreasing from bedrock to peak zone")
    {
        float prev = CaveCarver::getThreshold(6, SURFACE_H);
        for (int y = 7; y <= 50; ++y)
        {
            float curr = CaveCarver::getThreshold(y, SURFACE_H);
            REQUIRE(curr <= prev + 0.001f); // allow tiny float tolerance
            prev = curr;
        }
    }
}

// ── shouldCarve determinism ──────────────────────────────────────────────────

TEST_CASE("CaveCarver: shouldCarve is deterministic", "[world][cave]")
{
    constexpr uint64_t SEED = 99;
    CaveCarver carver1(SEED);
    CaveCarver carver2(SEED);

    // Test several positions
    float positions[][3] = {{100.0f, 50.0f, 200.0f}, {-50.0f, 30.0f, 75.0f}, {0.0f, 60.0f, 0.0f}};

    for (auto& pos : positions)
    {
        bool result1 = carver1.shouldCarve(pos[0], pos[1], pos[2], 120);
        bool result2 = carver2.shouldCarve(pos[0], pos[1], pos[2], 120);
        REQUIRE(result1 == result2);
    }
}

// ── Spaghetti caves produce elongated shapes ────────────────────────────────

TEST_CASE("CaveCarver: spaghetti caves produce elongated shapes via Y-stretch", "[world][cave]")
{
    constexpr uint64_t SEED = 88888;
    CaveCarver carver(SEED);
    constexpr int SURFACE_H = 150;

    // Measure directional correlation of carved blocks.
    // The Y-axis stretch (0.33) makes noise change 3x slower in Y than in X/Z,
    // so cave features extend further vertically. This produces anisotropic
    // (elongated) shapes: Y-correlation should be measurably higher than X/Z.
    int xzMatches = 0;
    int yMatches = 0;
    int xzChecks = 0;
    int yChecks = 0;

    // Sample in the peak cave zone (threshold is flat at 0.78) over a large XZ area
    for (float x = 0.0f; x < 64.0f; x += 1.0f)
    {
        for (float z = 0.0f; z < 64.0f; z += 1.0f)
        {
            for (float y = 50.0f; y < 80.0f; y += 1.0f)
            {
                if (!carver.shouldCarve(x, y, z, SURFACE_H))
                {
                    continue;
                }

                // Check X neighbor
                ++xzChecks;
                if (carver.shouldCarve(x + 1.0f, y, z, SURFACE_H))
                {
                    ++xzMatches;
                }

                // Check Z neighbor
                ++xzChecks;
                if (carver.shouldCarve(x, y, z + 1.0f, SURFACE_H))
                {
                    ++xzMatches;
                }

                // Check Y neighbor
                ++yChecks;
                if (carver.shouldCarve(x, y + 1.0f, z, SURFACE_H))
                {
                    ++yMatches;
                }
            }
        }
    }

    float xzRate = static_cast<float>(xzMatches) / static_cast<float>(xzChecks);
    float yRate = static_cast<float>(yMatches) / static_cast<float>(yChecks);

    INFO("X/Z neighbor correlation: " << xzRate << " (" << xzMatches << "/" << xzChecks << ")");
    INFO("Y neighbor correlation: " << yRate << " (" << yMatches << "/" << yChecks << ")");

    REQUIRE(xzChecks > 0);
    REQUIRE(yChecks > 0);
    // Y-stretch (0.33) makes noise vary slower in Y, so adjacent Y-blocks are more
    // correlated. This anisotropy (>5% difference) proves the caves are elongated, not isotropic.
    REQUIRE(yRate > xzRate);
    REQUIRE((yRate - xzRate) > 0.05f);
}
