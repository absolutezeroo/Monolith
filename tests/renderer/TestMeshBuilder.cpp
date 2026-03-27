#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

using namespace voxel::renderer;
using namespace voxel::world;

// Helper: register a basic opaque block (e.g., stone).
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

// Helper: register a transparent block (e.g., glass).
static uint16_t registerGlass(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:glass";
    def.isSolid = true;
    def.isTransparent = true;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:glass");
}

// All-null neighbor array (treat boundaries as air).
static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

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

TEST_CASE("MeshBuilder naive face culling", "[renderer][meshing]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("empty section produces 0 quads")
    {
        ChunkSection section;
        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 0);
        REQUIRE(mesh.quads.empty());
    }

    SECTION("single block in empty section produces exactly 6 quads")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        REQUIRE(mesh.quads.size() == 6);

        // Each face direction should appear exactly once.
        REQUIRE(countFace(mesh, BlockFace::PosX) == 1);
        REQUIRE(countFace(mesh, BlockFace::NegX) == 1);
        REQUIRE(countFace(mesh, BlockFace::PosY) == 1);
        REQUIRE(countFace(mesh, BlockFace::NegY) == 1);
        REQUIRE(countFace(mesh, BlockFace::PosZ) == 1);
        REQUIRE(countFace(mesh, BlockFace::NegZ) == 1);

        // Verify quad data for any one face.
        for (const uint64_t quad : mesh.quads)
        {
            REQUIRE(unpackX(quad) == 8);
            REQUIRE(unpackY(quad) == 8);
            REQUIRE(unpackZ(quad) == 8);
            REQUIRE(unpackBlockStateId(quad) == stoneId);
            REQUIRE(unpackWidth(quad) == 1);
            REQUIRE(unpackHeight(quad) == 1);
            REQUIRE(unpackAO01(quad) == 3);
            REQUIRE(unpackAO23(quad) == 3);
        }
    }

    SECTION("two adjacent blocks produce exactly 10 quads (shared face culled)")
    {
        ChunkSection section;
        section.setBlock(7, 8, 8, stoneId);
        section.setBlock(8, 8, 8, stoneId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // 2 blocks * 6 faces = 12, minus 2 shared faces (7,8,8 PosX and 8,8,8 NegX) = 10.
        REQUIRE(mesh.quadCount == 10);
    }

    SECTION("block at section boundary with null neighbor emits face (treated as air)")
    {
        ChunkSection section;
        // Place block at x=15 edge.
        section.setBlock(15, 0, 0, stoneId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // All 6 faces should be emitted (all neighbors are air or boundary→nullptr→air).
        REQUIRE(mesh.quadCount == 6);
        REQUIRE(countFace(mesh, BlockFace::PosX) == 1); // boundary face emitted
    }

    SECTION("block at section boundary with solid neighbor culls face")
    {
        ChunkSection section;
        section.setBlock(15, 0, 0, stoneId);

        // Create a +X neighbor section with a solid block at (0, 0, 0).
        ChunkSection neighborPosX;
        neighborPosX.setBlock(0, 0, 0, stoneId);

        std::array<const ChunkSection*, 6> neighbors = {&neighborPosX, nullptr, nullptr, nullptr, nullptr, nullptr};

        ChunkMesh mesh = builder.buildNaive(section, neighbors);

        // PosX face should be culled because neighbor has a solid block at (0, 0, 0).
        REQUIRE(mesh.quadCount == 5);
        REQUIRE(countFace(mesh, BlockFace::PosX) == 0);
    }

    SECTION("transparent block adjacent to opaque emits faces on both sides")
    {
        uint16_t glassId = registerGlass(registry);

        ChunkSection section;
        section.setBlock(7, 8, 8, stoneId);
        section.setBlock(8, 8, 8, glassId);

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        // Stone block: 5 normal faces + PosX face is adjacent to glass (transparent → emit). Total = 6.
        // Glass block: 5 normal faces + NegX face is adjacent to stone (opaque → NOT transparent → cull). Total = 5.
        // Total = 6 + 5 = 11.
        REQUIRE(mesh.quadCount == 11);

        // Verify: stone's PosX face is emitted (glass is transparent).
        bool foundStonePosX = false;
        bool foundGlassNegX = false;
        for (const uint64_t quad : mesh.quads)
        {
            if (unpackX(quad) == 7 && unpackFace(quad) == BlockFace::PosX)
            {
                foundStonePosX = true;
                REQUIRE(unpackBlockStateId(quad) == stoneId);
            }
            if (unpackX(quad) == 8 && unpackFace(quad) == BlockFace::NegX)
            {
                foundGlassNegX = true;
            }
        }
        REQUIRE(foundStonePosX);
        // Glass NegX face: neighbor is stone which is NOT transparent, so culled.
        REQUIRE_FALSE(foundGlassNegX);
    }

    SECTION("quad packing roundtrip preserves all fields")
    {
        uint8_t x = 15;
        uint8_t y = 7;
        uint8_t z = 31;
        uint16_t blockStateId = 500;
        BlockFace face = BlockFace::PosZ;
        uint8_t w = 10;
        uint8_t h = 20;
        uint8_t ao01 = 2;
        uint8_t ao23 = 1;

        uint64_t quad = packQuad(x, y, z, blockStateId, face, w, h, ao01, ao23);

        REQUIRE(unpackX(quad) == x);
        REQUIRE(unpackY(quad) == y);
        REQUIRE(unpackZ(quad) == (z & 0x3F)); // z is masked to 6 bits
        REQUIRE(unpackBlockStateId(quad) == blockStateId);
        REQUIRE(unpackFace(quad) == face);
        REQUIRE(unpackWidth(quad) == w);
        REQUIRE(unpackHeight(quad) == h);
        REQUIRE(unpackAO01(quad) == ao01);
        REQUIRE(unpackAO23(quad) == ao23);
    }
}

TEST_CASE("MeshBuilder performance benchmarks", "[renderer][meshing][!benchmark]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("dense terrain — fully filled section")
    {
        ChunkSection section;
        section.fill(stoneId);

        BENCHMARK("buildNaive — dense (16^3 all solid)")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }

    SECTION("typical terrain — ground plane + scattered blocks")
    {
        ChunkSection section;
        // Fill bottom 8 layers (ground plane) to simulate typical terrain.
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
        // Add scattered blocks in upper layers.
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

        BENCHMARK("buildNaive — typical terrain (half filled + scattered)")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }
}
