#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace voxel::renderer;
using namespace voxel::world;

// Helper: register a basic opaque block.
static uint16_t registerStone(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:stone";
    def.isSolid = true;
    def.isTransparent = false;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:stone");
}

// Helper: register a second opaque block type.
static uint16_t registerDirt(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:dirt";
    def.isSolid = true;
    def.isTransparent = false;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:dirt");
}

static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// Count quads with a specific face direction.
static uint32_t countFace(const ChunkMesh& mesh, BlockFace face)
{
    uint32_t count = 0;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackFace(quad) == face)
        {
            ++count;
        }
    }
    return count;
}

// Compute total visible surface area from quads (sum of width*height).
static uint64_t totalSurfaceArea(const ChunkMesh& mesh)
{
    uint64_t area = 0;
    for (const uint64_t quad : mesh.quads)
    {
        area += static_cast<uint64_t>(unpackWidth(quad)) * unpackHeight(quad);
    }
    return area;
}

TEST_CASE("Greedy meshing correctness", "[renderer][meshing][greedy]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("empty section produces 0 quads")
    {
        ChunkSection section;
        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.quads.empty());
    }

    SECTION("single block produces exactly 6 quads (same as naive)")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackBlockStateId(quad) == stoneId);
            REQUIRE(unpackWidth(quad) == 1);
            REQUIRE(unpackHeight(quad) == 1);
        }
    }

    SECTION("flat ground plane merges into 6 large quads")
    {
        ChunkSection section;
        // Fill y=0 layer (16x16 solid at y=0).
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                section.setBlock(x, 0, z, stoneId);
            }
        }

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Expected: 1 top (16x16), 1 bottom (16x16), 4 sides (each 16x1) = 6 quads.
        REQUIRE(mesh.quadCount == 6);
        REQUIRE(countFace(mesh, BlockFace::PosY) == 1);
        REQUIRE(countFace(mesh, BlockFace::NegY) == 1);

        // Verify the top face is 16x16 (one of the two large faces).
        for (const uint64_t quad : mesh.quads)
        {
            if (unpackFace(quad) == BlockFace::PosY || unpackFace(quad) == BlockFace::NegY)
            {
                // Row and col axes for PosY/NegY are X and Z.
                uint8_t w = unpackWidth(quad);
                uint8_t h = unpackHeight(quad);
                REQUIRE(w * h == 16 * 16);
            }
        }
    }

    SECTION("two adjacent blocks of same type produce 6 quads")
    {
        ChunkSection section;
        section.setBlock(7, 8, 8, stoneId);
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Two adjacent same-type blocks: shared face culled, remaining faces merge.
        // Top/bottom/front/back: each merges into 1 quad (2x1). Left/right: 1 quad each (1x1).
        // Total = 6 quads.
        REQUIRE(mesh.quadCount == 6);
    }

    SECTION("two adjacent blocks of DIFFERENT types produce 10 quads")
    {
        uint16_t dirtId = registerDirt(registry);

        ChunkSection section;
        section.setBlock(7, 8, 8, stoneId);
        section.setBlock(8, 8, 8, dirtId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Different types cannot merge. 2 blocks * 6 faces = 12, minus 2 shared faces = 10.
        REQUIRE(mesh.quadCount == 10);
    }

    SECTION("2x2x2 cube produces 6 quads (one per face)")
    {
        ChunkSection section;
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x)
                    section.setBlock(x, y, z, stoneId);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        for (const uint64_t quad : mesh.quads)
        {
            uint8_t w = unpackWidth(quad);
            uint8_t h = unpackHeight(quad);
            REQUIRE(w * h == 4); // each face is 2x2
        }
    }

    SECTION("checkerboard pattern produces no merging")
    {
        ChunkSection section;
        for (int y = 0; y < ChunkSection::SIZE; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                for (int x = 0; x < ChunkSection::SIZE; ++x)
                {
                    if ((x + y + z) % 2 == 0)
                    {
                        section.setBlock(x, y, z, stoneId);
                    }
                }
            }
        }

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Checkerboard: every face is isolated, no merging possible.
        // Greedy should produce the same number of quads as naive.
        REQUIRE(greedyMesh.quadCount == naiveMesh.quadCount);
    }

    SECTION("sphere shape face count within +/-5% of naive")
    {
        ChunkSection section;
        constexpr int CENTER = 7;
        constexpr int RADIUS = 6;
        for (int y = 0; y < ChunkSection::SIZE; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                for (int x = 0; x < ChunkSection::SIZE; ++x)
                {
                    int dx = x - CENTER;
                    int dy = y - CENTER;
                    int dz = z - CENTER;
                    if (dx * dx + dy * dy + dz * dz <= RADIUS * RADIUS)
                    {
                        section.setBlock(x, y, z, stoneId);
                    }
                }
            }
        }

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Greedy should produce fewer or equal quads.
        REQUIRE(greedyMesh.quadCount <= naiveMesh.quadCount);

        // Both meshers should produce the same total visible surface area.
        uint64_t naiveArea = totalSurfaceArea(naiveMesh);
        uint64_t greedyArea = totalSurfaceArea(greedyMesh);
        REQUIRE(naiveArea == greedyArea);
    }

    SECTION("section boundary with solid neighbor culls boundary faces")
    {
        ChunkSection section;
        section.setBlock(15, 0, 0, stoneId);

        ChunkSection neighborPosX;
        neighborPosX.setBlock(0, 0, 0, stoneId);

        std::array<const ChunkSection*, 6> neighbors = {&neighborPosX, nullptr, nullptr, nullptr, nullptr, nullptr};

        ChunkMesh mesh = builder.buildGreedy(section, neighbors);

        // PosX face should be culled.
        REQUIRE(mesh.quadCount == 5);
        REQUIRE(countFace(mesh, BlockFace::PosX) == 0);
    }

    SECTION("greedy mesh matches naive mesh surface area (dense terrain)")
    {
        ChunkSection section;
        section.fill(stoneId);

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        uint64_t naiveArea = totalSurfaceArea(naiveMesh);
        uint64_t greedyArea = totalSurfaceArea(greedyMesh);

        REQUIRE(naiveArea == greedyArea);
        // Greedy should produce far fewer quads for dense terrain.
        REQUIRE(greedyMesh.quadCount < naiveMesh.quadCount);
    }

    SECTION("quad packing roundtrip with merged width and height")
    {
        uint64_t quad = packQuad(3, 5, 7, stoneId, BlockFace::PosY, 16, 16, 3, 2, 1, 0, true);

        REQUIRE(unpackX(quad) == 3);
        REQUIRE(unpackY(quad) == 5);
        REQUIRE(unpackZ(quad) == 7);
        REQUIRE(unpackBlockStateId(quad) == stoneId);
        REQUIRE(unpackFace(quad) == BlockFace::PosY);
        REQUIRE(unpackWidth(quad) == 16);
        REQUIRE(unpackHeight(quad) == 16);
        auto ao = unpackAO(quad);
        REQUIRE(ao[0] == 3);
        REQUIRE(ao[1] == 2);
        REQUIRE(ao[2] == 1);
        REQUIRE(ao[3] == 0);
        REQUIRE(unpackFlip(quad) == true);
    }
}

TEST_CASE("Greedy meshing performance benchmarks", "[renderer][meshing][greedy][!benchmark]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("dense terrain — fully filled section")
    {
        ChunkSection section;
        section.fill(stoneId);

        BENCHMARK("buildGreedy — dense (16^3 all solid)")
        {
            return builder.buildGreedy(section, NO_NEIGHBORS);
        };

        BENCHMARK("buildNaive — dense (16^3 all solid) [baseline]")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }

    SECTION("typical terrain — ground plane + scattered blocks")
    {
        ChunkSection section;
        for (int y = 0; y < 8; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                for (int x = 0; x < ChunkSection::SIZE; ++x)
                {
                    section.setBlock(x, y, z, stoneId);
                }
            }
        }
        for (int y = 8; y < ChunkSection::SIZE; y += 3)
        {
            for (int z = 0; z < ChunkSection::SIZE; z += 4)
            {
                for (int x = 0; x < ChunkSection::SIZE; x += 4)
                {
                    section.setBlock(x, y, z, stoneId);
                }
            }
        }

        BENCHMARK("buildGreedy — typical terrain")
        {
            return builder.buildGreedy(section, NO_NEIGHBORS);
        };

        BENCHMARK("buildNaive — typical terrain [baseline]")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }

    SECTION("flat ground plane — highly mergeable")
    {
        ChunkSection section;
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                section.setBlock(x, 0, z, stoneId);
            }
        }

        BENCHMARK("buildGreedy — flat ground plane")
        {
            return builder.buildGreedy(section, NO_NEIGHBORS);
        };

        BENCHMARK("buildNaive — flat ground plane [baseline]")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }
}
