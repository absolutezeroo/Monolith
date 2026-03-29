#include "voxel/world/BlockLightPropagator.h"

#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/LightMap.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;

struct TestIds
{
    uint16_t stone = 0;
    uint16_t torch = 0;
    uint16_t glass = 0;
    uint16_t glowstone = 0;
};

struct TestFixture
{
    BlockRegistry registry;
    TestIds ids;
};

static TestFixture createTestFixture()
{
    TestFixture f;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.lightFilter = 15;
    auto stoneResult = f.registry.registerBlock(std::move(stone));
    REQUIRE(stoneResult.has_value());
    f.ids.stone = stoneResult.value();

    BlockDefinition torch;
    torch.stringId = "base:torch";
    torch.lightEmission = 14;
    torch.lightFilter = 0;
    torch.isSolid = false;
    auto torchResult = f.registry.registerBlock(std::move(torch));
    REQUIRE(torchResult.has_value());
    f.ids.torch = torchResult.value();

    BlockDefinition glass;
    glass.stringId = "base:glass";
    glass.lightFilter = 0;
    auto glassResult = f.registry.registerBlock(std::move(glass));
    REQUIRE(glassResult.has_value());
    f.ids.glass = glassResult.value();

    BlockDefinition glowstone;
    glowstone.stringId = "base:glowstone";
    glowstone.lightEmission = 15;
    glowstone.lightFilter = 15;
    auto glowstoneResult = f.registry.registerBlock(std::move(glowstone));
    REQUIRE(glowstoneResult.has_value());
    f.ids.glowstone = glowstoneResult.value();

    return f;
}

TEST_CASE("BlockLightPropagator", "[world][light]")
{
    auto fixture = createTestFixture();
    auto& registry = fixture.registry;
    auto& ids = fixture.ids;

    SECTION("single torch falloff")
    {
        // Place a torch at the center of section 0 (local 8,8,8)
        ChunkColumn column({0, 0});
        column.setBlock(8, 8, 8, ids.torch);

        BlockLightPropagator::propagateColumn(column, registry);

        const LightMap& lm = column.getLightMap(0);

        // Torch position: light = 14 (emission level)
        REQUIRE(lm.getBlockLight(8, 8, 8) == 14);

        // Adjacent blocks: light = 13
        REQUIRE(lm.getBlockLight(9, 8, 8) == 13);
        REQUIRE(lm.getBlockLight(7, 8, 8) == 13);
        REQUIRE(lm.getBlockLight(8, 9, 8) == 13);
        REQUIRE(lm.getBlockLight(8, 7, 8) == 13);
        REQUIRE(lm.getBlockLight(8, 8, 9) == 13);
        REQUIRE(lm.getBlockLight(8, 8, 7) == 13);

        // 2 blocks away along X: light = 12
        REQUIRE(lm.getBlockLight(10, 8, 8) == 12);
        REQUIRE(lm.getBlockLight(6, 8, 8) == 12);

        // At distance 13 along +X (block 8+13=21, but section is 0..15, so check X=15)
        // Distance from torch (8,8,8) to (15,8,8) = 7 blocks -> light = 14 - 7 = 7
        REQUIRE(lm.getBlockLight(15, 8, 8) == 7);

        // At distance 14, light should be 0 (beyond section in this direction)
        // Within the section at max cardinal distance: (0,8,8) = distance 8 -> light = 14 - 8 = 6
        REQUIRE(lm.getBlockLight(0, 8, 8) == 6);
    }

    SECTION("two torches take max")
    {
        ChunkColumn column({0, 0});
        column.setBlock(4, 8, 8, ids.torch);
        column.setBlock(12, 8, 8, ids.torch);

        BlockLightPropagator::propagateColumn(column, registry);

        const LightMap& lm = column.getLightMap(0);

        // Each torch emits 14
        REQUIRE(lm.getBlockLight(4, 8, 8) == 14);
        REQUIRE(lm.getBlockLight(12, 8, 8) == 14);

        // Midpoint (8,8,8) is distance 4 from each torch -> max(14-4, 14-4) = 10
        REQUIRE(lm.getBlockLight(8, 8, 8) == 10);

        // Position (5,8,8) is distance 1 from torch at 4, distance 7 from torch at 12
        // max(13, 7) = 13
        REQUIRE(lm.getBlockLight(5, 8, 8) == 13);
    }

    SECTION("opaque block fully blocks light")
    {
        ChunkColumn column({0, 0});
        // Torch at (4,8,8), stone wall at (6,8,8)
        column.setBlock(4, 8, 8, ids.torch);
        column.setBlock(6, 8, 8, ids.stone);

        BlockLightPropagator::propagateColumn(column, registry);

        const LightMap& lm = column.getLightMap(0);

        REQUIRE(lm.getBlockLight(4, 8, 8) == 14);
        REQUIRE(lm.getBlockLight(5, 8, 8) == 13);

        // Stone blocks light — no light value set in the opaque block
        REQUIRE(lm.getBlockLight(6, 8, 8) == 0);

        // Block behind stone (along X) can only get light via paths around the stone
        // Shortest alternate path: (5,8,8) at 13 -> (5,9,8) at 12 -> (6,9,8) at 11 -> (7,9,8) at 10 -> (7,8,8) at 9
        REQUIRE(lm.getBlockLight(7, 8, 8) == 9);
    }

    SECTION("transparent block passes light with -1 attenuation only")
    {
        ChunkColumn column({0, 0});
        // Torch at (4,8,8), glass at (5,8,8) and (6,8,8)
        column.setBlock(4, 8, 8, ids.torch);
        column.setBlock(5, 8, 8, ids.glass);
        column.setBlock(6, 8, 8, ids.glass);

        BlockLightPropagator::propagateColumn(column, registry);

        const LightMap& lm = column.getLightMap(0);

        REQUIRE(lm.getBlockLight(4, 8, 8) == 14);
        // Glass is transparent (lightFilter=0), light passes with standard -1 per step
        REQUIRE(lm.getBlockLight(5, 8, 8) == 13);
        REQUIRE(lm.getBlockLight(6, 8, 8) == 12);
        REQUIRE(lm.getBlockLight(7, 8, 8) == 11);
    }

    SECTION("cross-section Y boundary propagation")
    {
        // Place torch near top of section 0 (local y=14, world y=14)
        ChunkColumn column({0, 0});
        column.setBlock(8, 14, 8, ids.torch);

        BlockLightPropagator::propagateColumn(column, registry);

        // Section 0 light: torch at local (8,14,8)
        const LightMap& lm0 = column.getLightMap(0);
        REQUIRE(lm0.getBlockLight(8, 14, 8) == 14);
        REQUIRE(lm0.getBlockLight(8, 15, 8) == 13); // top of section 0

        // Section 1 light: propagated across boundary
        const LightMap& lm1 = column.getLightMap(1);
        REQUIRE(lm1.getBlockLight(8, 0, 8) == 12); // bottom of section 1 (world Y=16)
        REQUIRE(lm1.getBlockLight(8, 1, 8) == 11); // world Y=17
    }

    SECTION("glowstone emission at max level")
    {
        ChunkColumn column({0, 0});
        column.setBlock(8, 8, 8, ids.glowstone);

        BlockLightPropagator::propagateColumn(column, registry);

        const LightMap& lm = column.getLightMap(0);

        // Glowstone emits 15 but is opaque (lightFilter=15)
        // The light is set at the glowstone position
        REQUIRE(lm.getBlockLight(8, 8, 8) == 15);

        // Adjacent: 14 (glowstone is opaque but light is emitted FROM it)
        REQUIRE(lm.getBlockLight(9, 8, 8) == 14);
        REQUIRE(lm.getBlockLight(7, 8, 8) == 14);
    }

    SECTION("ChunkColumn getLightMap round-trip")
    {
        ChunkColumn column({3, -5});

        // All LightMaps start clear
        for (int s = 0; s < ChunkColumn::SECTIONS_PER_COLUMN; ++s)
        {
            REQUIRE(column.getLightMap(s).isClear());
        }

        // Set some values and read them back
        column.getLightMap(0).setBlockLight(1, 2, 3, 7);
        column.getLightMap(5).setSkyLight(4, 5, 6, 12);

        REQUIRE(column.getLightMap(0).getBlockLight(1, 2, 3) == 7);
        REQUIRE(column.getLightMap(5).getSkyLight(4, 5, 6) == 12);

        // clearAllLight resets everything
        column.clearAllLight();
        REQUIRE(column.getLightMap(0).isClear());
        REQUIRE(column.getLightMap(5).isClear());
    }

    SECTION("cross-chunk border propagation pushes light to neighbor")
    {
        // Two adjacent chunks: torch near +X border of chunk (0,0)
        ChunkManager manager;
        manager.loadChunk({0, 0});
        manager.loadChunk({1, 0});

        ChunkColumn* col0 = manager.getChunk({0, 0});
        ChunkColumn* col1 = manager.getChunk({1, 0});
        REQUIRE(col0 != nullptr);
        REQUIRE(col1 != nullptr);

        // Place torch at X=14, near the +X border
        col0->setBlock(14, 8, 8, ids.torch);

        // Propagate within column first
        BlockLightPropagator::propagateColumn(*col0, registry);

        // Verify light reaches the border
        REQUIRE(col0->getLightMap(0).getBlockLight(15, 8, 8) == 13);

        // Propagate borders — should push light into chunk (1,0)
        BlockLightPropagator::propagateBorders(*col0, manager, registry);

        // Verify light crossed into neighbor chunk
        REQUIRE(col1->getLightMap(0).getBlockLight(0, 8, 8) == 12); // 14 - 2
        REQUIRE(col1->getLightMap(0).getBlockLight(1, 8, 8) == 11); // 14 - 3
        REQUIRE(col1->getLightMap(0).getBlockLight(2, 8, 8) == 10); // 14 - 4

        // Verify neighbor section was marked dirty for remeshing
        REQUIRE(col1->isSectionDirty(0));
    }

    SECTION("cross-chunk border propagation pulls light from neighbor")
    {
        // Two adjacent chunks: torch near -X border of chunk (1,0)
        ChunkManager manager;
        manager.loadChunk({0, 0});
        manager.loadChunk({1, 0});

        ChunkColumn* col0 = manager.getChunk({0, 0});
        ChunkColumn* col1 = manager.getChunk({1, 0});
        REQUIRE(col0 != nullptr);
        REQUIRE(col1 != nullptr);

        // Place torch at X=1 in chunk (1,0), near the -X border
        col1->setBlock(1, 8, 8, ids.torch);
        BlockLightPropagator::propagateColumn(*col1, registry);

        // Verify light reaches neighbor's -X border
        REQUIRE(col1->getLightMap(0).getBlockLight(0, 8, 8) == 13);

        // Propagate borders for chunk (0,0) — should PULL light from chunk (1,0)
        BlockLightPropagator::propagateBorders(*col0, manager, registry);

        // Verify light was pulled into chunk (0,0) at X=15
        REQUIRE(col0->getLightMap(0).getBlockLight(15, 8, 8) == 12); // 14 - 2
        REQUIRE(col0->getLightMap(0).getBlockLight(14, 8, 8) == 11); // 14 - 3

        // Verify our section was marked dirty
        REQUIRE(col0->isSectionDirty(0));
    }
}
