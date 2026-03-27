#include "voxel/renderer/AmbientOcclusion.h"
#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

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

// All-null neighbor array (treat boundaries as air).
static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

TEST_CASE("vertexAO truth table", "[renderer][ao]")
{
    SECTION("no neighbors occluding -> AO = 3")
    {
        REQUIRE(vertexAO(false, false, false) == 3);
    }

    SECTION("one side occluding -> AO = 2")
    {
        REQUIRE(vertexAO(true, false, false) == 2);
    }

    SECTION("corner only occluding -> AO = 2")
    {
        REQUIRE(vertexAO(false, true, false) == 2);
    }

    SECTION("other side occluding -> AO = 2")
    {
        REQUIRE(vertexAO(false, false, true) == 2);
    }

    SECTION("side + corner -> AO = 1")
    {
        REQUIRE(vertexAO(true, true, false) == 1);
    }

    SECTION("corner + other side -> AO = 1")
    {
        REQUIRE(vertexAO(false, true, true) == 1);
    }

    SECTION("both sides -> AO = 0 (corner irrelevant)")
    {
        REQUIRE(vertexAO(true, false, true) == 0);
    }

    SECTION("all three -> AO = 0")
    {
        REQUIRE(vertexAO(true, true, true) == 0);
    }
}

TEST_CASE("Isolated block in empty section has AO = 3 on all faces", "[renderer][ao]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    ChunkSection section;
    section.setBlock(8, 8, 8, stoneId);

    ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);
    REQUIRE(mesh.quadCount == 6);

    for (const uint64_t quad : mesh.quads)
    {
        auto ao = unpackAO(quad);
        REQUIRE(ao[0] == 3);
        REQUIRE(ao[1] == 3);
        REQUIRE(ao[2] == 3);
        REQUIRE(ao[3] == 3);
        REQUIRE(unpackFlip(quad) == false);
    }
}

TEST_CASE("Block in L-shaped corner produces expected AO gradient on top face", "[renderer][ao]")
{
    // Build an L-shaped corner: floor (y=0 filled) + wall along -X (x=0 column) + wall along -Z (z=0 column).
    // Examine the top face of block at (1, 0, 1) which is on the floor next to both walls.
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    ChunkSection section;

    // Floor: fill y=0 layer.
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        for (int x = 0; x < ChunkSection::SIZE; ++x)
        {
            section.setBlock(x, 0, z, stoneId);
        }
    }

    // Wall along -X: fill x=0, y=1 column.
    for (int z = 0; z < ChunkSection::SIZE; ++z)
    {
        section.setBlock(0, 1, z, stoneId);
    }

    // Wall along -Z: fill z=0, y=1 column.
    for (int x = 0; x < ChunkSection::SIZE; ++x)
    {
        section.setBlock(x, 1, 0, stoneId);
    }

    ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

    // Find the +Y (PosY) face of block at (1, 0, 1).
    bool found = false;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackX(quad) == 1 && unpackY(quad) == 0 && unpackZ(quad) == 1 && unpackFace(quad) == BlockFace::PosY)
        {
            found = true;
            auto ao = unpackAO(quad);

            // PosY face corners for block (1, 0, 1):
            // Corner 0 samples: (-1,+1,0), (0,+1,-1), (-1,+1,-1) relative to (1,0,1)
            //   -> (0,1,1)=wall(-X), (1,1,0)=wall(-Z), (0,1,0)=both walls -> side1=T, side2=T -> AO=0
            REQUIRE(ao[0] == 0);

            // Corner 1 samples: (+1,+1,0), (0,+1,-1), (+1,+1,-1) relative to (1,0,1)
            //   -> (2,1,1)=air, (1,1,0)=wall(-Z), (2,1,0)=wall(-Z, all x) -> s1=F, s2=T, c=T -> AO=1
            REQUIRE(ao[1] == 1);

            // Corner 2 samples: (+1,+1,0), (0,+1,+1), (+1,+1,+1) relative to (1,0,1)
            //   -> (2,1,1)=air, (1,1,2)=air, (2,1,2)=air -> all false -> AO=3
            REQUIRE(ao[2] == 3);

            // Corner 3 samples: (-1,+1,0), (0,+1,+1), (-1,+1,+1) relative to (1,0,1)
            //   -> (0,1,1)=wall(-X), (1,1,2)=air, (0,1,2)=wall(-X, all z) -> s1=T, s2=F, c=T -> AO=1
            REQUIRE(ao[3] == 1);

            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Quad packing roundtrip with AO values", "[renderer][ao]")
{
    uint8_t ao0 = 0;
    uint8_t ao1 = 1;
    uint8_t ao2 = 2;
    uint8_t ao3 = 3;
    bool flip = true;

    uint64_t quad = packQuad(5, 10, 3, 42, BlockFace::NegY, 1, 1, ao0, ao1, ao2, ao3, flip);

    auto ao = unpackAO(quad);
    REQUIRE(ao[0] == ao0);
    REQUIRE(ao[1] == ao1);
    REQUIRE(ao[2] == ao2);
    REQUIRE(ao[3] == ao3);
    REQUIRE(unpackFlip(quad) == true);

    // Also test no-flip case.
    uint64_t quad2 = packQuad(5, 10, 3, 42, BlockFace::NegY, 1, 1, 3, 3, 3, 3, false);
    auto ao2arr = unpackAO(quad2);
    REQUIRE(ao2arr[0] == 3);
    REQUIRE(ao2arr[1] == 3);
    REQUIRE(ao2arr[2] == 3);
    REQUIRE(ao2arr[3] == 3);
    REQUIRE(unpackFlip(quad2) == false);
}

TEST_CASE("Diagonal flip triggers when asymmetric AO across quad", "[renderer][ao]")
{
    // shouldFlipQuad: flip when ao[0]+ao[3] > ao[1]+ao[2].
    SECTION("symmetric AO -> no flip")
    {
        std::array<uint8_t, 4> ao = {3, 3, 3, 3};
        REQUIRE(shouldFlipQuad(ao) == false);
    }

    SECTION("opposite corners darker -> no flip")
    {
        // ao[0]+ao[3] = 1+1 = 2, ao[1]+ao[2] = 3+3 = 6 -> 2 > 6 is false
        std::array<uint8_t, 4> ao = {1, 3, 3, 1};
        REQUIRE(shouldFlipQuad(ao) == false);
    }

    SECTION("default diagonal corners brighter -> flip")
    {
        // ao[0]+ao[3] = 3+3 = 6, ao[1]+ao[2] = 1+1 = 2 -> 6 > 2 is true
        std::array<uint8_t, 4> ao = {3, 1, 1, 3};
        REQUIRE(shouldFlipQuad(ao) == true);
    }

    SECTION("single corner dark -> check direction")
    {
        // ao[0]+ao[3] = 0+3 = 3, ao[1]+ao[2] = 3+3 = 6 -> 3 > 6 is false
        std::array<uint8_t, 4> ao = {0, 3, 3, 3};
        REQUIRE(shouldFlipQuad(ao) == false);
    }
}

TEST_CASE("Block at section boundary with null neighbor treats boundary as air for AO", "[renderer][ao]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    ChunkSection section;

    // Place a block at x=0 edge with blocks above and behind to create AO.
    // The -X boundary has no neighbor (nullptr), so AO sampling into -X should see air.
    section.setBlock(0, 0, 0, stoneId);
    section.setBlock(0, 1, 0, stoneId); // block above

    ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

    // Find the -X (NegX) face of block at (0, 0, 0).
    // The face is on the boundary. AO offsets from the NegX face sample into padded x=0 cells,
    // which default to air (false). So AO should only be affected by blocks within the section.
    bool found = false;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackX(quad) == 0 && unpackY(quad) == 0 && unpackZ(quad) == 0 && unpackFace(quad) == BlockFace::NegX)
        {
            found = true;
            auto ao = unpackAO(quad);
            // NegX face corners sample relative to (-1, *, *).
            // Corner 0: side1=(-1,-1,0), side2=(-1,0,+1), corner=(-1,-1,+1)
            //   -> padded: (0,0,1), (0,1,2), (0,0,2) -> all air -> AO=3
            // Corner 1: side1=(-1,+1,0), side2=(-1,0,+1), corner=(-1,+1,+1)
            //   -> padded: (0,2,1) -> block at (0,1,0) is at padded (1,2,1), NOT (0,2,1)
            //   -> (0,2,1)=air, (0,1,2)=air, (0,2,2)=air -> AO=3
            // All corners should be 3 because boundary samples hit the padded border (all air).
            // The block above (0,1,0) is in the center section, not in the -X border.
            REQUIRE(ao[0] == 3);
            REQUIRE(ao[1] == 3);
            REQUIRE(ao[2] == 3);
            REQUIRE(ao[3] == 3);
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("AO cross-boundary: PosX neighbor with opaque block above face", "[renderer][ao][cross-boundary]")
{
    // Block at (15, 0, 0) with PosX neighbor containing opaque block at (0, 1, 0).
    // The +X face of (15,0,0) should see the neighbor block (0,1,0) occluding from above.
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    ChunkSection section;
    section.setBlock(15, 0, 0, stoneId);

    // PosX neighbor: place an opaque block at (0, 1, 0) — directly above the +X face.
    ChunkSection neighborPosX;
    neighborPosX.setBlock(0, 1, 0, stoneId);

    std::array<const ChunkSection*, 6> neighbors = {&neighborPosX, nullptr, nullptr, nullptr, nullptr, nullptr};

    ChunkMesh mesh = builder.buildNaive(section, neighbors);

    // Find the +X (PosX) face of block at (15, 0, 0).
    bool found = false;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackX(quad) == 15 && unpackY(quad) == 0 && unpackZ(quad) == 0 &&
            unpackFace(quad) == BlockFace::PosX)
        {
            found = true;
            auto ao = unpackAO(quad);

            // PosX face AO_OFFSETS: corner 0 = (-Y,-Z), corner 1 = (+Y,-Z), corner 2 = (+Y,+Z), corner 3 = (-Y,+Z)
            // The neighbor block at (0,1,0) in the +X section maps to padded (17, 2, 1).
            // For block (15,0,0), padded coords are (16, 1, 1).
            // PosX corner 1 samples: side1=(+1,+1,0)→(17,2,1)=opaque, side2=(+1,0,-1)→(17,1,0)=air,
            //   corner=(+1,+1,-1)→(17,2,0)=air → AO = 3 - 1 - 0 - 0 = 2
            // PosX corner 2 samples: side1=(+1,+1,0)→(17,2,1)=opaque, side2=(+1,0,+1)→(17,1,2)=air,
            //   corner=(+1,+1,+1)→(17,2,2)=air → AO = 3 - 1 - 0 - 0 = 2
            // Corners 1 and 2 (the +Y side) should have AO < 3 due to the neighbor above.
            REQUIRE(ao[1] < 3);
            REQUIRE(ao[2] < 3);
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("AO cross-boundary: PosY neighbor with opaque block beside face", "[renderer][ao][cross-boundary]")
{
    // Block at (0, 15, 0) with PosY neighbor containing opaque block at (1, 0, 0).
    // The +Y face of (0,15,0) should see some occlusion from the adjacent block above.
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    ChunkSection section;
    section.setBlock(0, 15, 0, stoneId);

    // PosY neighbor: place an opaque block at (1, 0, 0) — beside the +Y face.
    ChunkSection neighborPosY;
    neighborPosY.setBlock(1, 0, 0, stoneId);

    std::array<const ChunkSection*, 6> neighbors = {nullptr, nullptr, &neighborPosY, nullptr, nullptr, nullptr};

    ChunkMesh mesh = builder.buildNaive(section, neighbors);

    // Find the +Y (PosY) face of block at (0, 15, 0).
    bool found = false;
    for (const uint64_t quad : mesh.quads)
    {
        if (unpackX(quad) == 0 && unpackY(quad) == 15 && unpackZ(quad) == 0 &&
            unpackFace(quad) == BlockFace::PosY)
        {
            found = true;
            auto ao = unpackAO(quad);

            // PosY face AO_OFFSETS: corner 0 = (-X,-Z), corner 1 = (+X,-Z), corner 2 = (+X,+Z), corner 3 = (-X,+Z)
            // The neighbor block at (1,0,0) in the +Y section maps to padded (2, 17, 1).
            // For block (0,15,0), padded coords are (1, 16, 1).
            // PosY corner 1 samples: side1=(+1,+1,0)→(2,17,1)=opaque, side2=(0,+1,-1)→(1,17,0)=air,
            //   corner=(+1,+1,-1)→(2,17,0)=air → AO = 3 - 1 - 0 - 0 = 2
            // PosY corner 2 samples: side1=(+1,+1,0)→(2,17,1)=opaque, side2=(0,+1,+1)→(1,17,2)=air,
            //   corner=(+1,+1,+1)→(2,17,2)=air → AO = 3 - 1 - 0 - 0 = 2
            // Corners 1 and 2 (the +X side) should have AO < 3 due to the neighbor beside.
            REQUIRE(ao[1] < 3);
            REQUIRE(ao[2] < 3);
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("AO performance benchmark with dense terrain", "[renderer][ao][!benchmark]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("dense terrain — fully filled section with AO")
    {
        ChunkSection section;
        section.fill(stoneId);

        BENCHMARK("buildNaive with AO — dense (16^3 all solid)")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }

    SECTION("typical terrain — ground plane + scattered blocks with AO")
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

        BENCHMARK("buildNaive with AO — typical terrain (half filled + scattered)")
        {
            return builder.buildNaive(section, NO_NEIGHBORS);
        };
    }
}
