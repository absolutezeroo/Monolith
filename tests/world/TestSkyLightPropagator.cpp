#include "voxel/world/SkyLightPropagator.h"

#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/LightMap.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;

struct SkyTestIds
{
    uint16_t stone = 0;
    uint16_t leaves = 0;
    uint16_t glass = 0;
};

struct SkyTestFixture
{
    BlockRegistry registry;
    SkyTestIds ids;
};

static SkyTestFixture createSkyTestFixture()
{
    SkyTestFixture f;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.lightFilter = 15;
    auto stoneResult = f.registry.registerBlock(std::move(stone));
    REQUIRE(stoneResult.has_value());
    f.ids.stone = stoneResult.value();

    BlockDefinition leaves;
    leaves.stringId = "base:leaves";
    leaves.isSolid = false;
    leaves.lightFilter = 1;
    auto leavesResult = f.registry.registerBlock(std::move(leaves));
    REQUIRE(leavesResult.has_value());
    f.ids.leaves = leavesResult.value();

    BlockDefinition glass;
    glass.stringId = "base:glass";
    glass.isSolid = false;
    glass.lightFilter = 0;
    auto glassResult = f.registry.registerBlock(std::move(glass));
    REQUIRE(glassResult.has_value());
    f.ids.glass = glassResult.value();

    return f;
}

/// Helper: build a flat stone surface at a given world Y, filling all 16x16 blocks.
static void buildFlatSurface(ChunkColumn& column, uint16_t stoneId, int surfaceY)
{
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            for (int y = 0; y <= surfaceY; ++y)
            {
                column.setBlock(x, y, z, stoneId);
            }
        }
    }
}

TEST_CASE("SkyLightPropagator", "[world][light]")
{
    auto fixture = createSkyTestFixture();
    auto& registry = fixture.registry;
    auto& ids = fixture.ids;

    SECTION("flat surface open air — all blocks above surface = sky 15")
    {
        ChunkColumn column({0, 0});
        int surfaceY = 64;
        buildFlatSurface(column, ids.stone, surfaceY);

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // All air blocks above surface should have sky=15
        int sectionY = (surfaceY + 1) / ChunkSection::SIZE;
        int localY = (surfaceY + 1) % ChunkSection::SIZE;
        REQUIRE(column.getLightMap(sectionY).getSkyLight(8, localY, 8) == 15);

        // High up in the air
        REQUIRE(column.getLightMap(15).getSkyLight(8, 15, 8) == 15);
        REQUIRE(column.getLightMap(10).getSkyLight(0, 0, 0) == 15);

        // Block AT the surface (opaque) should have sky=0
        int surfaceSectionY = surfaceY / ChunkSection::SIZE;
        int surfaceLocalY = surfaceY % ChunkSection::SIZE;
        REQUIRE(column.getLightMap(surfaceSectionY).getSkyLight(8, surfaceLocalY, 8) == 0);

        // Block BELOW surface should have sky=0
        if (surfaceY > 0)
        {
            int belowSectionY = (surfaceY - 1) / ChunkSection::SIZE;
            int belowLocalY = (surfaceY - 1) % ChunkSection::SIZE;
            REQUIRE(column.getLightMap(belowSectionY).getSkyLight(8, belowLocalY, 8) == 0);
        }
    }

    SECTION("overhang — sky light attenuates horizontally")
    {
        ChunkColumn column({0, 0});
        int surfaceY = 64;
        buildFlatSurface(column, ids.stone, surfaceY);

        // Create an overhang: place a stone roof 3 blocks above the surface, extending from X=0..12
        // Leave X=13..15 open so sky light enters from the east side
        int roofY = surfaceY + 4; // roof at Y=68
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x <= 12; ++x)
            {
                column.setBlock(x, roofY, z, ids.stone);
            }
        }

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // Under the overhang, at Y = surfaceY+1 (65), the blocks are in shadow.
        // Sky light enters from the open edge (X=13) horizontally.
        int underY = surfaceY + 1; // Y=65
        int underSectionY = underY / ChunkSection::SIZE;
        int underLocalY = underY % ChunkSection::SIZE;
        const LightMap& lm = column.getLightMap(underSectionY);

        // X=13 is open sky (no roof above) → sky=15
        REQUIRE(lm.getSkyLight(13, underLocalY, 8) == 15);

        // X=12 is under the overhang, 1 horizontal step from open sky → sky=14
        REQUIRE(lm.getSkyLight(12, underLocalY, 8) == 14);

        // X=11 is 2 steps → sky=13
        REQUIRE(lm.getSkyLight(11, underLocalY, 8) == 13);

        // X=10 is 3 steps → sky=12 (AC: "3 blocks under overhang = 12")
        REQUIRE(lm.getSkyLight(10, underLocalY, 8) == 12);

        // X=9 is 4 steps → sky=11
        REQUIRE(lm.getSkyLight(9, underLocalY, 8) == 11);
    }

    SECTION("sealed cave — all blocks inside = sky 0")
    {
        ChunkColumn column({0, 0});
        int surfaceY = 64;
        buildFlatSurface(column, ids.stone, surfaceY);

        // The cave is sealed below the surface — no opening to sky.
        // Create a hollow space at Y=30..32
        // It's already surrounded by stone on all sides from buildFlatSurface.
        // Carve the inside by removing blocks (set to air = 0)
        for (int z = 4; z <= 12; ++z)
        {
            for (int x = 4; x <= 12; ++x)
            {
                for (int y = 30; y <= 32; ++y)
                {
                    column.setBlock(x, y, z, BLOCK_AIR);
                }
            }
        }

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // All blocks inside the sealed cave should have sky=0
        int caveSectionY = 31 / ChunkSection::SIZE; // section 1
        int caveLocalY = 31 % ChunkSection::SIZE;   // local 15
        REQUIRE(column.getLightMap(caveSectionY).getSkyLight(8, caveLocalY, 8) == 0);
        REQUIRE(column.getLightMap(caveSectionY).getSkyLight(4, caveLocalY, 4) == 0);
        REQUIRE(column.getLightMap(caveSectionY).getSkyLight(12, caveLocalY, 12) == 0);
    }

    SECTION("vertical shaft — sky light stays at 15 all the way down")
    {
        ChunkColumn column({0, 0});
        int surfaceY = 80;
        buildFlatSurface(column, ids.stone, surfaceY);

        // Carve a 1x1 shaft from surface down to Y=10
        for (int y = 10; y <= surfaceY; ++y)
        {
            column.setBlock(8, y, 8, BLOCK_AIR);
        }

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // Sky light should be 15 all the way down the shaft
        for (int y = 10; y <= surfaceY; ++y)
        {
            int sectionY = y / ChunkSection::SIZE;
            int localY = y % ChunkSection::SIZE;
            REQUIRE(column.getLightMap(sectionY).getSkyLight(8, localY, 8) == 15);
        }

        // The block at Y=9 (bottom of shaft, still stone) should have sky=0
        REQUIRE(column.getLightMap(0).getSkyLight(8, 9, 8) == 0);
    }

    SECTION("cross-section boundary — sky light propagates correctly across sections")
    {
        ChunkColumn column({0, 0});

        // Place a stone ceiling just above a section boundary.
        // Section 5 = Y [80..95]. Place stone roof at Y=83 (localY=3 in section 5).
        // Leave an opening at X=15 for sky light to enter horizontally.
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x <= 14; ++x)
            {
                column.setBlock(x, 83, z, ids.stone);
            }
        }
        // Place a floor at the bottom of section 5 (Y=80, localY=0)
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                column.setBlock(x, 79, z, ids.stone);
            }
        }

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // Y=80 (section 5, localY=0) and Y=81 (section 5, localY=1) under the roof:
        // Sky light enters from X=15 (open) and propagates left.
        const LightMap& lm5 = column.getLightMap(5);
        REQUIRE(lm5.getSkyLight(15, 1, 8) == 15); // X=15 is open column
        REQUIRE(lm5.getSkyLight(14, 1, 8) == 14); // 1 horizontal step
        REQUIRE(lm5.getSkyLight(13, 1, 8) == 13); // 2 horizontal steps

        // Y=82 is also under roof, so same pattern
        REQUIRE(lm5.getSkyLight(15, 2, 8) == 15);
        REQUIRE(lm5.getSkyLight(14, 2, 8) == 14);
    }

    SECTION("transparent blocks (leaves) allow sky light through — binary check")
    {
        ChunkColumn column({0, 0});
        int surfaceY = 64;
        buildFlatSurface(column, ids.stone, surfaceY);

        // Place a canopy of leaves above the surface at Y=70
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                column.setBlock(x, 70, z, ids.leaves);
            }
        }

        column.buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(column, registry);

        // Leaves have lightFilter=1 (not 15), so they are treated as transparent for propagation.
        // Sky light should pass through the leaves canopy (binary: lightFilter < 15 = transparent).
        int sectionY = 70 / ChunkSection::SIZE; // section 4
        int localY = 70 % ChunkSection::SIZE;   // localY=6

        // The leaf block itself should have sky=15 (sky passes through it)
        REQUIRE(column.getLightMap(sectionY).getSkyLight(8, localY, 8) == 15);

        // Block below leaves (Y=69) should also be sky=15 (seeded by Phase 1 downward scan)
        int belowSectionY = 69 / ChunkSection::SIZE;
        int belowLocalY = 69 % ChunkSection::SIZE;
        REQUIRE(column.getLightMap(belowSectionY).getSkyLight(8, belowLocalY, 8) == 15);

        // Block above surface (Y=65) should be sky=15
        int aboveSectionY = 65 / ChunkSection::SIZE;
        int aboveLocalY = 65 % ChunkSection::SIZE;
        REQUIRE(column.getLightMap(aboveSectionY).getSkyLight(8, aboveLocalY, 8) == 15);
    }

    SECTION("heightmap accuracy — getHeight matches actual highest opaque block")
    {
        ChunkColumn column({0, 0});

        // Place stone at various heights for different (x,z) positions
        column.setBlock(0, 50, 0, ids.stone);
        column.setBlock(5, 100, 5, ids.stone);
        column.setBlock(15, 200, 15, ids.stone);

        // Place a transparent block above stone — should not affect heightmap
        column.setBlock(5, 101, 5, ids.leaves);

        column.buildHeightMap(registry);

        REQUIRE(column.getHeight(0, 0) == 50);
        REQUIRE(column.getHeight(5, 5) == 100);
        REQUIRE(column.getHeight(15, 15) == 200);

        // Column with no opaque blocks should be 0
        REQUIRE(column.getHeight(8, 8) == 0);
    }

    SECTION("cross-chunk border propagation pushes sky light to neighbor")
    {
        ChunkManager manager;
        manager.loadChunk({0, 0});
        manager.loadChunk({1, 0});

        ChunkColumn* col0 = manager.getChunk({0, 0});
        ChunkColumn* col1 = manager.getChunk({1, 0});
        REQUIRE(col0 != nullptr);
        REQUIRE(col1 != nullptr);

        // Build a surface in chunk 0 at Y=64
        buildFlatSurface(*col0, ids.stone, 64);
        // Build a surface in chunk 1 at Y=64, then add a roof over chunk 1
        buildFlatSurface(*col1, ids.stone, 64);
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                col1->setBlock(x, 68, z, ids.stone);
            }
        }

        // Propagate chunk 0 first (open sky)
        col0->buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(*col0, registry);

        // Propagate chunk 1 (roofed — no direct sky under roof)
        col1->buildHeightMap(registry);
        SkyLightPropagator::propagateColumn(*col1, registry);

        // Now propagate borders — chunk 0 has sky=15 at X=15, should push into chunk 1 at X=0
        SkyLightPropagator::propagateBorders(*col0, manager, registry);

        // Under the roof in chunk 1, at Y=65 (between surface and roof):
        // X=0 should get sky light from chunk 0's X=15 (which is 15) → 15-1 = 14
        int underSectionY = 65 / ChunkSection::SIZE;
        int underLocalY = 65 % ChunkSection::SIZE;
        REQUIRE(col1->getLightMap(underSectionY).getSkyLight(0, underLocalY, 8) == 14);
        REQUIRE(col1->getLightMap(underSectionY).getSkyLight(1, underLocalY, 8) == 13);
    }
}
