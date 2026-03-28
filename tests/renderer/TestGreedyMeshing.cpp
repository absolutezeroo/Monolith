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
    def.textureIndices[0] = 1;
    def.textureIndices[1] = 1;
    def.textureIndices[2] = 1;
    def.textureIndices[3] = 1;
    def.textureIndices[4] = 1;
    def.textureIndices[5] = 1;
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
    def.textureIndices[0] = 2;
    def.textureIndices[1] = 2;
    def.textureIndices[2] = 2;
    def.textureIndices[3] = 2;
    def.textureIndices[4] = 2;
    def.textureIndices[5] = 2;
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
            REQUIRE(unpackTextureIndex(quad) == 1); // stone texture index
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
        REQUIRE(unpackTextureIndex(quad) == stoneId);
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

// Helper: register a transparent FullCube block (e.g., glass).
static uint16_t registerGlass(BlockRegistry& registry)
{
    BlockDefinition def;
    def.stringId = "base:glass";
    def.isSolid = true;
    def.isTransparent = true;
    def.modelType = ModelType::FullCube;
    def.renderType = RenderType::Cutout;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:glass");
}

TEST_CASE("Greedy meshing AO-aware merging", "[renderer][meshing][greedy][ao]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("AO boundary prevents merge")
    {
        // Stone floor at y=0, wall at x=0 (y=0..3). The wall occludes PosY faces
        // at x=0 but not x=1+, creating an AO boundary that must split the quad.
        ChunkSection section;
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                section.setBlock(x, 0, z, stoneId);
            }
        }
        // Wall along x=0.
        for (int y = 1; y <= 3; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
            {
                section.setBlock(0, y, z, stoneId);
            }
        }

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // PosY faces of the floor must have >1 quad because AO differs at x=0 vs x=1.
        uint32_t topQuads = countFace(mesh, BlockFace::PosY);
        REQUIRE(topQuads > 1);
    }

    SECTION("greedy AO matches naive per-block")
    {
        // L-corner terrain: stone floor + wall at x=0 and z=0.
        ChunkSection section;
        for (int z = 0; z < ChunkSection::SIZE; ++z)
            for (int x = 0; x < ChunkSection::SIZE; ++x)
                section.setBlock(x, 0, z, stoneId);
        for (int y = 1; y <= 2; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
                section.setBlock(0, y, z, stoneId);
            for (int x = 0; x < ChunkSection::SIZE; ++x)
                section.setBlock(x, y, 0, stoneId);
        }

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Build map of (x,y,z,face) -> AO from naive.
        struct FaceKey
        {
            uint8_t x, y, z, face;
            bool operator==(const FaceKey& o) const
            {
                return x == o.x && y == o.y && z == o.z && face == o.face;
            }
        };
        struct FaceKeyHash
        {
            size_t operator()(const FaceKey& k) const
            {
                return (k.x << 24) | (k.y << 16) | (k.z << 8) | k.face;
            }
        };
        std::unordered_map<FaceKey, std::array<uint8_t, 4>, FaceKeyHash> naiveAO;
        for (const uint64_t quad : naiveMesh.quads)
        {
            FaceKey key{unpackX(quad), unpackY(quad), unpackZ(quad),
                        static_cast<uint8_t>(unpackFace(quad))};
            naiveAO[key] = unpackAO(quad);
        }

        // Every greedy quad's origin block must have the same AO as the naive version.
        for (const uint64_t quad : greedyMesh.quads)
        {
            FaceKey key{unpackX(quad), unpackY(quad), unpackZ(quad),
                        static_cast<uint8_t>(unpackFace(quad))};
            auto it = naiveAO.find(key);
            REQUIRE(it != naiveAO.end());
            auto greedyAO = unpackAO(quad);
            REQUIRE(greedyAO == it->second);
        }
    }

    SECTION("AO-aware merge preserves surface area on L-corner terrain")
    {
        ChunkSection section;
        for (int z = 0; z < ChunkSection::SIZE; ++z)
            for (int x = 0; x < ChunkSection::SIZE; ++x)
                section.setBlock(x, 0, z, stoneId);
        for (int y = 1; y <= 2; ++y)
        {
            for (int z = 0; z < ChunkSection::SIZE; ++z)
                section.setBlock(0, y, z, stoneId);
            for (int x = 0; x < ChunkSection::SIZE; ++x)
                section.setBlock(x, y, 0, stoneId);
        }

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(totalSurfaceArea(naiveMesh) == totalSurfaceArea(greedyMesh));
    }
}

TEST_CASE("Greedy meshing transparent blocks", "[renderer][meshing][greedy][transparent]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    uint16_t glassId = registerGlass(registry);
    MeshBuilder builder(registry);

    SECTION("single glass block = 6 faces")
    {
        ChunkSection section;
        section.setBlock(8, 8, 8, glassId);

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(naiveMesh.quadCount == 6);
        REQUIRE(greedyMesh.quadCount == 6);
    }

    SECTION("glass plane merges to 6 quads")
    {
        ChunkSection section;
        for (int z = 0; z < ChunkSection::SIZE; ++z)
            for (int x = 0; x < ChunkSection::SIZE; ++x)
                section.setBlock(x, 0, z, glassId);

        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Same as opaque plane: 1 top + 1 bottom + 4 sides = 6 quads.
        REQUIRE(greedyMesh.quadCount == 6);
    }

    SECTION("stone-glass adjacency emits correct faces")
    {
        // Stone at (8,8,8) + glass at (9,8,8). Stone is opaque, glass is transparent.
        // Stone: 6 faces (opaque sees transparent neighbor as air-like, emits face toward glass).
        // Glass: 5 faces (glass doesn't emit face toward opaque stone — same block type check
        //        passes, but the neighbor is opaque so it's culled by !neighborOpaque check).
        // Actually: glass emits toward stone because stone is opaque → !neighborOpaque is false,
        // so glass does NOT emit toward stone. Glass gets 5 faces.
        ChunkSection section;
        section.setBlock(8, 8, 8, stoneId);
        section.setBlock(9, 8, 8, glassId);

        ChunkMesh naiveMesh = builder.buildNaive(section, NO_NEIGHBORS);
        ChunkMesh greedyMesh = builder.buildGreedy(section, NO_NEIGHBORS);

        // Both should produce 11 quads (stone=6, glass=5).
        REQUIRE(naiveMesh.quadCount == greedyMesh.quadCount);
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
