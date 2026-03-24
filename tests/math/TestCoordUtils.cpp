#include "voxel/math/CoordUtils.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::math;

TEST_CASE("worldToChunk basic conversions", "[math][coords]")
{
    SECTION("origin maps to chunk (0, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{0.0, 0.0, 0.0});
        REQUIRE(chunk.x == 0);
        REQUIRE(chunk.y == 0);
    }

    SECTION("(15, 0, 15) stays in chunk (0, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{15.0, 0.0, 15.0});
        REQUIRE(chunk.x == 0);
        REQUIRE(chunk.y == 0);
    }

    SECTION("(16, 0, 0) maps to chunk (1, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{16.0, 0.0, 0.0});
        REQUIRE(chunk.x == 1);
        REQUIRE(chunk.y == 0);
    }

    SECTION("(-1, 0, 0) maps to chunk (-1, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{-1.0, 0.0, 0.0});
        REQUIRE(chunk.x == -1);
        REQUIRE(chunk.y == 0);
    }

    SECTION("(-16, 0, 0) maps to chunk (-1, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{-16.0, 0.0, 0.0});
        REQUIRE(chunk.x == -1);
        REQUIRE(chunk.y == 0);
    }

    SECTION("(-17, 0, 0) maps to chunk (-2, 0)")
    {
        IVec2 chunk = worldToChunk(DVec3{-17.0, 0.0, 0.0});
        REQUIRE(chunk.x == -2);
        REQUIRE(chunk.y == 0);
    }
}

TEST_CASE("worldToLocal basic conversions", "[math][coords]")
{
    SECTION("origin maps to local (0, 0, 0)")
    {
        IVec3 local = worldToLocal(DVec3{0.0, 0.0, 0.0});
        REQUIRE(local.x == 0);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 0);
    }

    SECTION("(15, 0, 15) maps to local (15, 0, 15)")
    {
        IVec3 local = worldToLocal(DVec3{15.0, 0.0, 15.0});
        REQUIRE(local.x == 15);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 15);
    }

    SECTION("(16, 0, 0) wraps to local (0, 0, 0)")
    {
        IVec3 local = worldToLocal(DVec3{16.0, 0.0, 0.0});
        REQUIRE(local.x == 0);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 0);
    }

    SECTION("(-1, 0, 0) maps to local (15, 0, 0)")
    {
        IVec3 local = worldToLocal(DVec3{-1.0, 0.0, 0.0});
        REQUIRE(local.x == 15);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 0);
    }

    SECTION("(-16, 0, 0) maps to local (0, 0, 0)")
    {
        IVec3 local = worldToLocal(DVec3{-16.0, 0.0, 0.0});
        REQUIRE(local.x == 0);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 0);
    }

    SECTION("(-17, 0, 0) maps to local (15, 0, 0)")
    {
        IVec3 local = worldToLocal(DVec3{-17.0, 0.0, 0.0});
        REQUIRE(local.x == 15);
        REQUIRE(local.y == 0);
        REQUIRE(local.z == 0);
    }

    SECTION("Y coordinate is absolute, not wrapped")
    {
        IVec3 local = worldToLocal(DVec3{5.0, 64.0, 5.0});
        REQUIRE(local.y == 64);
    }
}

TEST_CASE("worldToChunk/localToWorld roundtrip", "[math][coords]")
{
    SECTION("positive coordinates roundtrip")
    {
        IVec2 chunk{3, 5};
        IVec3 local{7, 32, 11};
        DVec3 world = localToWorld(chunk, local);
        REQUIRE(worldToChunk(world) == chunk);
        REQUIRE(worldToLocal(world).x == local.x);
        REQUIRE(worldToLocal(world).y == local.y);
        REQUIRE(worldToLocal(world).z == local.z);
    }

    SECTION("negative coordinates roundtrip")
    {
        IVec2 chunk{-2, -3};
        IVec3 local{15, 10, 0};
        DVec3 world = localToWorld(chunk, local);
        REQUIRE(worldToChunk(world) == chunk);
        REQUIRE(worldToLocal(world).x == local.x);
        REQUIRE(worldToLocal(world).y == local.y);
        REQUIRE(worldToLocal(world).z == local.z);
    }

    SECTION("origin roundtrip")
    {
        IVec2 chunk{0, 0};
        IVec3 local{0, 0, 0};
        DVec3 world = localToWorld(chunk, local);
        REQUIRE(worldToChunk(world) == chunk);
        IVec3 resultLocal = worldToLocal(world);
        REQUIRE(resultLocal.x == local.x);
        REQUIRE(resultLocal.y == local.y);
        REQUIRE(resultLocal.z == local.z);
    }
}

TEST_CASE("blockToIndex and indexToBlock", "[math][coords]")
{
    SECTION("origin index is 0")
    {
        REQUIRE(blockToIndex(0, 0, 0) == 0);
    }

    SECTION("(15, 15, 15) yields last valid index")
    {
        int32_t index = blockToIndex(15, 15, 15);
        REQUIRE(index == 4095);
    }

    SECTION("Y-major layout: incrementing Y changes index by 256")
    {
        REQUIRE(blockToIndex(0, 1, 0) == 256);
        REQUIRE(blockToIndex(0, 2, 0) == 512);
    }

    SECTION("Z changes index by 16")
    {
        REQUIRE(blockToIndex(0, 0, 1) == 16);
    }

    SECTION("X changes index by 1")
    {
        REQUIRE(blockToIndex(1, 0, 0) == 1);
    }

    SECTION("inverse for all valid indices")
    {
        for (int32_t i = 0; i < SECTION_VOLUME; ++i)
        {
            IVec3 block = indexToBlock(i);
            REQUIRE(blockToIndex(block.x, block.y, block.z) == i);
        }
    }

    SECTION("forward/inverse roundtrip for all valid coordinates")
    {
        for (int32_t y = 0; y < SECTION_SIZE; ++y)
        {
            for (int32_t z = 0; z < SECTION_SIZE; ++z)
            {
                for (int32_t x = 0; x < SECTION_SIZE; ++x)
                {
                    int32_t index = blockToIndex(x, y, z);
                    IVec3 result = indexToBlock(index);
                    REQUIRE(result.x == x);
                    REQUIRE(result.y == y);
                    REQUIRE(result.z == z);
                }
            }
        }
    }
}

TEST_CASE("localToWorld basic conversions", "[math][coords]")
{
    SECTION("chunk (0,0) local (0,0,0) maps to world origin")
    {
        DVec3 world = localToWorld(IVec2{0, 0}, IVec3{0, 0, 0});
        REQUIRE(world.x == 0.0);
        REQUIRE(world.y == 0.0);
        REQUIRE(world.z == 0.0);
    }

    SECTION("chunk (1,0) local (0,0,0) maps to world (16,0,0)")
    {
        DVec3 world = localToWorld(IVec2{1, 0}, IVec3{0, 0, 0});
        REQUIRE(world.x == 16.0);
        REQUIRE(world.y == 0.0);
        REQUIRE(world.z == 0.0);
    }

    SECTION("negative chunk maps correctly")
    {
        DVec3 world = localToWorld(IVec2{-1, 0}, IVec3{15, 0, 0});
        REQUIRE(world.x == -1.0);
        REQUIRE(world.y == 0.0);
        REQUIRE(world.z == 0.0);
    }
}
