#include "voxel/physics/Raycast.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

using namespace voxel;
using namespace voxel::physics;
using namespace voxel::world;
using namespace voxel::renderer;
using Catch::Matchers::WithinAbs;

namespace
{

/// Helper: set up a BlockRegistry with air (0), stone (1), glass (2), water (3).
BlockRegistry makeTestRegistry()
{
    BlockRegistry registry;

    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.hasCollision = true;
    auto result = registry.registerBlock(std::move(stone));
    REQUIRE(result.has_value());

    BlockDefinition glass;
    glass.stringId = "base:glass";
    glass.isSolid = false;
    glass.isTransparent = true;
    glass.hasCollision = true; // glass stops the ray
    auto glassResult = registry.registerBlock(std::move(glass));
    REQUIRE(glassResult.has_value());

    BlockDefinition water;
    water.stringId = "base:water";
    water.isSolid = false;
    water.isTransparent = true;
    water.hasCollision = false; // water does NOT stop the ray
    auto waterResult = registry.registerBlock(std::move(water));
    REQUIRE(waterResult.has_value());

    return registry;
}

/// Helper: load a grid of chunks and lay a flat stone floor at groundY.
void setupFlatGround(ChunkManager& cm, const BlockRegistry& registry, int groundY = 64)
{
    for (int cx = -2; cx <= 2; ++cx)
    {
        for (int cz = -2; cz <= 2; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t stoneId = registry.getIdByName("base:stone");
    for (int cx = -2; cx <= 2; ++cx)
    {
        for (int cz = -2; cz <= 2; ++cz)
        {
            for (int lx = 0; lx < ChunkSection::SIZE; ++lx)
            {
                for (int lz = 0; lz < ChunkSection::SIZE; ++lz)
                {
                    int wx = cx * ChunkSection::SIZE + lx;
                    int wz = cz * ChunkSection::SIZE + lz;
                    cm.setBlock(glm::ivec3{wx, groundY, wz}, stoneId);
                }
            }
        }
    }
}

} // namespace

TEST_CASE("Raycast: straight down hits ground and returns NegY face", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    // Standing above ground, looking straight down
    glm::vec3 origin{8.5f, 67.0f, 8.5f};
    glm::vec3 dir{0.0f, -1.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    REQUIRE(result.hit);
    CHECK(result.blockPos == glm::ivec3{8, 64, 8});
    CHECK(result.face == BlockFace::PosY); // entering from above = PosY face
    CHECK_THAT(result.distance, WithinAbs(2.0, 0.01));
}

TEST_CASE("Raycast: into wall returns correct block and face", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t stoneId = registry.getIdByName("base:stone");

    SECTION("Ray +X into NegX face")
    {
        cm.setBlock(glm::ivec3{5, 65, 8}, stoneId);
        glm::vec3 origin{2.5f, 65.5f, 8.5f};
        glm::vec3 dir{1.0f, 0.0f, 0.0f};

        auto result = raycast(origin, dir, MAX_REACH, cm, registry);

        REQUIRE(result.hit);
        CHECK(result.blockPos == glm::ivec3{5, 65, 8});
        CHECK(result.face == BlockFace::NegX);
    }

    SECTION("Ray -X into PosX face")
    {
        cm.setBlock(glm::ivec3{2, 65, 8}, stoneId);
        glm::vec3 origin{5.5f, 65.5f, 8.5f};
        glm::vec3 dir{-1.0f, 0.0f, 0.0f};

        auto result = raycast(origin, dir, MAX_REACH, cm, registry);

        REQUIRE(result.hit);
        CHECK(result.blockPos == glm::ivec3{2, 65, 8});
        CHECK(result.face == BlockFace::PosX);
    }

    SECTION("Ray +Z into NegZ face")
    {
        cm.setBlock(glm::ivec3{8, 65, 5}, stoneId);
        glm::vec3 origin{8.5f, 65.5f, 2.5f};
        glm::vec3 dir{0.0f, 0.0f, 1.0f};

        auto result = raycast(origin, dir, MAX_REACH, cm, registry);

        REQUIRE(result.hit);
        CHECK(result.blockPos == glm::ivec3{8, 65, 5});
        CHECK(result.face == BlockFace::NegZ);
    }

    SECTION("Ray -Z into PosZ face")
    {
        cm.setBlock(glm::ivec3{8, 65, 2}, stoneId);
        glm::vec3 origin{8.5f, 65.5f, 5.5f};
        glm::vec3 dir{0.0f, 0.0f, -1.0f};

        auto result = raycast(origin, dir, MAX_REACH, cm, registry);

        REQUIRE(result.hit);
        CHECK(result.blockPos == glm::ivec3{8, 65, 2});
        CHECK(result.face == BlockFace::PosZ);
    }
}

TEST_CASE("Raycast: into empty air returns no hit", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    // No blocks placed — all air
    glm::vec3 origin{8.5f, 65.5f, 8.5f};
    glm::vec3 dir{1.0f, 0.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    CHECK_FALSE(result.hit);
}

TEST_CASE("Raycast: exceeding max distance returns no hit", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    // Place a block 10 blocks away — beyond MAX_REACH (6)
    uint16_t stoneId = registry.getIdByName("base:stone");
    cm.setBlock(glm::ivec3{18, 65, 8}, stoneId);

    glm::vec3 origin{8.5f, 65.5f, 8.5f};
    glm::vec3 dir{1.0f, 0.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    CHECK_FALSE(result.hit);
}

TEST_CASE("Raycast: previousPos is the air block before the hit", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    setupFlatGround(cm, registry, 64);

    // Looking straight down from above ground
    glm::vec3 origin{8.5f, 67.0f, 8.5f};
    glm::vec3 dir{0.0f, -1.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    REQUIRE(result.hit);
    CHECK(result.blockPos == glm::ivec3{8, 64, 8});
    CHECK(result.previousPos == glm::ivec3{8, 65, 8}); // one block above = placement position
}

TEST_CASE("Raycast: passes through non-solid blocks (water), stops at solid blocks (glass)", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t waterId = registry.getIdByName("base:water");
    uint16_t glassId = registry.getIdByName("base:glass");

    // Place water at x=5, glass at x=7
    cm.setBlock(glm::ivec3{5, 65, 8}, waterId);
    cm.setBlock(glm::ivec3{7, 65, 8}, glassId);

    glm::vec3 origin{3.5f, 65.5f, 8.5f};
    glm::vec3 dir{1.0f, 0.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    REQUIRE(result.hit);
    // Should skip water (hasCollision=false) and hit glass (hasCollision=true)
    CHECK(result.blockPos == glm::ivec3{7, 65, 8});
    CHECK(result.face == BlockFace::NegX);
}

TEST_CASE("Raycast: diagonal ray hits the first solid block", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t stoneId = registry.getIdByName("base:stone");

    // Place two blocks along a diagonal
    cm.setBlock(glm::ivec3{6, 65, 6}, stoneId); // closer
    cm.setBlock(glm::ivec3{8, 65, 8}, stoneId); // farther

    glm::vec3 origin{4.5f, 65.5f, 4.5f};
    glm::vec3 dir = glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f});

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    REQUIRE(result.hit);
    CHECK(result.blockPos == glm::ivec3{6, 65, 6}); // should hit the closer one
}

TEST_CASE("Raycast: origin inside a solid block returns immediate hit", "[physics][raycast]")
{
    auto registry = makeTestRegistry();
    ChunkManager cm;
    for (int cx = -1; cx <= 1; ++cx)
    {
        for (int cz = -1; cz <= 1; ++cz)
        {
            cm.loadChunk(glm::ivec2{cx, cz});
        }
    }

    uint16_t stoneId = registry.getIdByName("base:stone");
    cm.setBlock(glm::ivec3{8, 65, 8}, stoneId);

    // Origin is inside the solid block
    glm::vec3 origin{8.5f, 65.5f, 8.5f};
    glm::vec3 dir{1.0f, 0.0f, 0.0f};

    auto result = raycast(origin, dir, MAX_REACH, cm, registry);

    REQUIRE(result.hit);
    CHECK(result.blockPos == glm::ivec3{8, 65, 8});
    CHECK_THAT(result.distance, WithinAbs(0.0, 0.01)); // immediate hit at distance 0
}
