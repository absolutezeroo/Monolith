#include "voxel/world/DynamicLightUpdater.h"

#include "voxel/world/BlockLightPropagator.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/LightMap.h"
#include "voxel/world/SkyLightPropagator.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;

struct DynTestIds
{
    uint16_t stone = 0;
    uint16_t torch = 0;
    uint16_t glass = 0;
    uint16_t glowstone = 0;
};

struct DynTestFixture
{
    BlockRegistry registry;
    DynTestIds ids;
    ChunkManager manager;

    // Non-copyable, non-movable (ChunkManager contains std::mutex)
    DynTestFixture() = default;
    DynTestFixture(const DynTestFixture&) = delete;
    DynTestFixture& operator=(const DynTestFixture&) = delete;
    DynTestFixture(DynTestFixture&&) = delete;
    DynTestFixture& operator=(DynTestFixture&&) = delete;
};

static std::unique_ptr<DynTestFixture> createDynFixture()
{
    auto f = std::make_unique<DynTestFixture>();

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.lightFilter = 15;
    auto stoneResult = f->registry.registerBlock(std::move(stone));
    REQUIRE(stoneResult.has_value());
    f->ids.stone = stoneResult.value();

    BlockDefinition torch;
    torch.stringId = "base:torch";
    torch.lightEmission = 14;
    torch.lightFilter = 0;
    torch.isSolid = false;
    auto torchResult = f->registry.registerBlock(std::move(torch));
    REQUIRE(torchResult.has_value());
    f->ids.torch = torchResult.value();

    BlockDefinition glass;
    glass.stringId = "base:glass";
    glass.lightFilter = 0;
    auto glassResult = f->registry.registerBlock(std::move(glass));
    REQUIRE(glassResult.has_value());
    f->ids.glass = glassResult.value();

    BlockDefinition glowstone;
    glowstone.stringId = "base:glowstone";
    glowstone.lightEmission = 15;
    glowstone.lightFilter = 15;
    auto gsResult = f->registry.registerBlock(std::move(glowstone));
    REQUIRE(gsResult.has_value());
    f->ids.glowstone = gsResult.value();

    // Load a chunk at (0,0) — no world gen, no registry → empty column, no auto-propagation
    f->manager.loadChunk({0, 0});
    // Set registry AFTER loading so we can manually control propagation
    f->manager.setBlockRegistry(&f->registry);

    return f;
}

/// Helper: propagate all light in a column (block + sky).
static void propagateAll(ChunkColumn& column, const BlockRegistry& registry, ChunkManager& manager)
{
    BlockLightPropagator::propagateColumn(column, registry);
    BlockLightPropagator::propagateBorders(column, manager, registry);
    column.buildHeightMap(registry);
    SkyLightPropagator::propagateColumn(column, registry);
    SkyLightPropagator::propagateBorders(column, manager, registry);
}

TEST_CASE("DynamicLightUpdater", "[world][light]")
{
    SECTION("break wall next to torch — light fills gap")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Setup: torch at (4,8,8), stone wall at (6,8,8)
        col->setBlock(4, 8, 8, f->ids.torch);
        col->setBlock(6, 8, 8, f->ids.stone);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        // Before break: stone blocks light, (7,8,8) gets light via detour
        REQUIRE(col->getLightMap(0).getBlockLight(5, 8, 8) == 13);
        REQUIRE(col->getLightMap(0).getBlockLight(6, 8, 8) == 0); // Stone
        REQUIRE(col->getLightMap(0).getBlockLight(7, 8, 8) == 9); // Detour

        // Break the stone wall
        const BlockDefinition& stoneDef = f->registry.getBlockType(f->ids.stone);
        col->setBlock(6, 8, 8, BLOCK_AIR);
        DynamicLightUpdater::onBlockBroken(*col, 6, 8, 8, stoneDef, f->manager, f->registry);

        // After break: light flows through the gap — (6,8,8) is now air
        // Distance from torch at (4,8,8) to (6,8,8) = 2 → light = 14 - 2 = 12
        REQUIRE(col->getLightMap(0).getBlockLight(6, 8, 8) == 12);
        // (7,8,8) = distance 3 → light = 14 - 3 = 11 (better than previous 9)
        REQUIRE(col->getLightMap(0).getBlockLight(7, 8, 8) == 11);
    }

    SECTION("place block in lit area — shadow cast")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Setup: torch at (4,8,8), open area
        col->setBlock(4, 8, 8, f->ids.torch);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        // Before place: light flows freely
        REQUIRE(col->getLightMap(0).getBlockLight(6, 8, 8) == 12);
        REQUIRE(col->getLightMap(0).getBlockLight(7, 8, 8) == 11);

        // Place stone wall at (6,8,8)
        const BlockDefinition& stoneDef = f->registry.getBlockType(f->ids.stone);
        col->setBlock(6, 8, 8, f->ids.stone);
        DynamicLightUpdater::onBlockPlaced(*col, 6, 8, 8, stoneDef, f->manager, f->registry);

        // After place: stone blocks light
        REQUIRE(col->getLightMap(0).getBlockLight(6, 8, 8) == 0);
        // (7,8,8) should only get light via detour: down to 9
        REQUIRE(col->getLightMap(0).getBlockLight(7, 8, 8) == 9);
    }

    SECTION("place torch — correct BFS falloff")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Empty column — no initial light
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 0);

        // Place a torch
        const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);
        col->setBlock(8, 8, 8, f->ids.torch);
        DynamicLightUpdater::onBlockPlaced(*col, 8, 8, 8, torchDef, f->manager, f->registry);

        // Verify BFS falloff
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 13);
        REQUIRE(col->getLightMap(0).getBlockLight(10, 8, 8) == 12);
        REQUIRE(col->getLightMap(0).getBlockLight(15, 8, 8) == 7);
        REQUIRE(col->getLightMap(0).getBlockLight(0, 8, 8) == 6);
    }

    SECTION("break torch — area goes dark")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Setup: torch at (8,8,8)
        col->setBlock(8, 8, 8, f->ids.torch);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 13);

        // Break the torch
        const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);
        col->setBlock(8, 8, 8, BLOCK_AIR);
        DynamicLightUpdater::onBlockBroken(*col, 8, 8, 8, torchDef, f->manager, f->registry);

        // All light from this torch should be removed
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 0);
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 0);
        REQUIRE(col->getLightMap(0).getBlockLight(10, 8, 8) == 0);
        REQUIRE(col->getLightMap(0).getBlockLight(15, 8, 8) == 0);
    }

    SECTION("two torches, break one — remaining torch keeps its area lit")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Setup: torch A at (4,8,8), torch B at (12,8,8)
        col->setBlock(4, 8, 8, f->ids.torch);
        col->setBlock(12, 8, 8, f->ids.torch);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        // Before break: both torches illuminate
        REQUIRE(col->getLightMap(0).getBlockLight(4, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(12, 8, 8) == 14);
        // Midpoint (8,8,8): distance 4 from each → max(10, 10) = 10
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 10);

        // Break torch A at (4,8,8)
        const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);
        col->setBlock(4, 8, 8, BLOCK_AIR);
        DynamicLightUpdater::onBlockBroken(*col, 4, 8, 8, torchDef, f->manager, f->registry);

        // Torch A gone: light near its position should be from torch B only
        // (4,8,8) is distance 8 from torch B at (12,8,8) → light = 14 - 8 = 6
        REQUIRE(col->getLightMap(0).getBlockLight(4, 8, 8) == 6);

        // Torch B still fully lit
        REQUIRE(col->getLightMap(0).getBlockLight(12, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(11, 8, 8) == 13);

        // Midpoint: now only from torch B, distance 4 → light = 10
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 10);
    }

    SECTION("Y boundary crossing — light propagates across sections")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Place torch at Y=15 (top of section 0)
        const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);
        col->setBlock(8, 15, 8, f->ids.torch);
        DynamicLightUpdater::onBlockPlaced(*col, 8, 15, 8, torchDef, f->manager, f->registry);

        // Light should propagate into section 1 (Y=16+)
        REQUIRE(col->getLightMap(0).getBlockLight(8, 15, 8) == 14);
        REQUIRE(col->getLightMap(1).getBlockLight(8, 0, 8) == 13); // Y=16
        REQUIRE(col->getLightMap(1).getBlockLight(8, 1, 8) == 12); // Y=17

        // And into section below (Y=14-)
        REQUIRE(col->getLightMap(0).getBlockLight(8, 14, 8) == 13);
    }

    SECTION("sky light recovery — break opaque roof block, sky light floods down")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Build a stone floor at Y=80 (section 5, localY=0) for a 3x3 area
        for (int x = 7; x <= 9; ++x)
        {
            for (int z = 7; z <= 9; ++z)
            {
                col->setBlock(x, 80, z, f->ids.stone);
            }
        }

        // Run full initial propagation
        propagateAll(*col, f->registry, f->manager);

        // Above the roof should be sky=15
        REQUIRE(col->getLightMap(5).getBlockLight(8, 0, 8) == 0);
        uint8_t skyAbove = col->getLightMap(5).getSkyLight(8, 1, 8); // Y=81
        REQUIRE(skyAbove == 15);

        // Below the roof should have reduced sky light
        uint8_t skyBelow = col->getLightMap(4).getSkyLight(8, 15, 8); // Y=79
        REQUIRE(skyBelow < 15); // Blocked by stone roof

        // Break the center roof block at (8,80,8)
        const BlockDefinition& stoneDef = f->registry.getBlockType(f->ids.stone);
        col->setBlock(8, 80, 8, BLOCK_AIR);
        DynamicLightUpdater::onBlockBroken(*col, 8, 80, 8, stoneDef, f->manager, f->registry);

        // After break: sky light should flood down through the hole
        uint8_t skyAtHole = col->getLightMap(5).getSkyLight(8, 0, 8); // Y=80 is now air
        REQUIRE(skyAtHole == 15);
        uint8_t skyBelowHole = col->getLightMap(4).getSkyLight(8, 15, 8); // Y=79
        REQUIRE(skyBelowHole == 15); // Sky light propagates down without attenuation
    }

    SECTION("place opaque block above surface — heightmap updated, sky light blocked")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Build a stone floor at Y=64 (section 4, localY=0)
        col->setBlock(8, 64, 8, f->ids.stone);
        propagateAll(*col, f->registry, f->manager);

        uint8_t heightBefore = col->getHeight(8, 8);
        REQUIRE(heightBefore == 64);

        // Sky above Y=64 should be 15
        REQUIRE(col->getLightMap(4).getSkyLight(8, 1, 8) == 15); // Y=65

        // Place stone block above at Y=66
        const BlockDefinition& stoneDef = f->registry.getBlockType(f->ids.stone);
        col->setBlock(8, 66, 8, f->ids.stone);
        DynamicLightUpdater::onBlockPlaced(*col, 8, 66, 8, stoneDef, f->manager, f->registry);

        // Heightmap should be updated to 66
        REQUIRE(col->getHeight(8, 8) == 66);

        // Sky light at Y=65 (below the new block) should be reduced
        uint8_t skyBetween = col->getLightMap(4).getSkyLight(8, 1, 8); // Y=65
        REQUIRE(skyBetween < 15);
    }

    SECTION("place emissive+opaque block (glowstone) — emits light correctly")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Place glowstone (emission=15, lightFilter=15) at (8,8,8)
        const BlockDefinition& gsDef = f->registry.getBlockType(f->ids.glowstone);
        col->setBlock(8, 8, 8, f->ids.glowstone);
        DynamicLightUpdater::onBlockPlaced(*col, 8, 8, 8, gsDef, f->manager, f->registry);

        // Glowstone itself should have full emission
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 15);
        // Neighbors should have correct BFS falloff
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(10, 8, 8) == 13);
        REQUIRE(col->getLightMap(0).getBlockLight(0, 8, 8) == 7);
    }

    SECTION("break emissive+opaque block (glowstone) — light removed")
    {
        auto f = createDynFixture();
        ChunkColumn* col = f->manager.getChunk({0, 0});
        REQUIRE(col != nullptr);

        // Setup: place glowstone, propagate
        col->setBlock(8, 8, 8, f->ids.glowstone);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 15);
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 14);

        // Break glowstone
        const BlockDefinition& gsDef = f->registry.getBlockType(f->ids.glowstone);
        col->setBlock(8, 8, 8, BLOCK_AIR);
        DynamicLightUpdater::onBlockBroken(*col, 8, 8, 8, gsDef, f->manager, f->registry);

        // All light should be removed
        REQUIRE(col->getLightMap(0).getBlockLight(8, 8, 8) == 0);
        REQUIRE(col->getLightMap(0).getBlockLight(9, 8, 8) == 0);
    }

    SECTION("cross-chunk X boundary — torch light propagates into neighbor")
    {
        auto f = createDynFixture();
        // Load a neighbor chunk at (1,0)
        f->manager.loadChunk({1, 0});

        ChunkColumn* col = f->manager.getChunk({0, 0});
        ChunkColumn* neighbor = f->manager.getChunk({1, 0});
        REQUIRE(col != nullptr);
        REQUIRE(neighbor != nullptr);

        // Place torch at localX=14 in chunk (0,0) → world X=14, near +X boundary
        const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);
        col->setBlock(14, 8, 8, f->ids.torch);
        DynamicLightUpdater::onBlockPlaced(*col, 14, 8, 8, torchDef, f->manager, f->registry);

        // Light should propagate within chunk (0,0)
        REQUIRE(col->getLightMap(0).getBlockLight(14, 8, 8) == 14);
        REQUIRE(col->getLightMap(0).getBlockLight(15, 8, 8) == 13);

        // Light should cross into neighbor chunk (1,0) at localX=0 (world X=16)
        REQUIRE(neighbor->getLightMap(0).getBlockLight(0, 8, 8) == 12);
        REQUIRE(neighbor->getLightMap(0).getBlockLight(1, 8, 8) == 11);
    }
}

TEST_CASE("DynamicLightUpdater performance", "[world][light][!benchmark]")
{
    auto f = createDynFixture();
    ChunkColumn* col = f->manager.getChunk({0, 0});
    REQUIRE(col != nullptr);

    const BlockDefinition& torchDef = f->registry.getBlockType(f->ids.torch);

    BENCHMARK_ADVANCED("single torch place")(Catch::Benchmark::Chronometer meter)
    {
        meter.measure([&] {
            col->clearAllLight();
            col->setBlock(8, 8, 8, f->ids.torch);
            DynamicLightUpdater::onBlockPlaced(*col, 8, 8, 8, torchDef, f->manager, f->registry);
        });
    };

    BENCHMARK_ADVANCED("single torch break")(Catch::Benchmark::Chronometer meter)
    {
        // Setup before each batch: place torch and propagate
        col->clearAllLight();
        col->setBlock(8, 8, 8, f->ids.torch);
        BlockLightPropagator::propagateColumn(*col, f->registry);

        meter.measure([&] {
            col->setBlock(8, 8, 8, BLOCK_AIR);
            DynamicLightUpdater::onBlockBroken(*col, 8, 8, 8, torchDef, f->manager, f->registry);
        });
    };
}
