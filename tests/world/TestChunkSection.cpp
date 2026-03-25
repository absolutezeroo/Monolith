#include "voxel/world/ChunkSection.h"

#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;

TEST_CASE("ChunkSection", "[world]")
{
    ChunkSection section;

    SECTION("default construction fills with AIR")
    {
        REQUIRE(section.getBlock(0, 0, 0) == BLOCK_AIR);
        REQUIRE(section.getBlock(15, 15, 15) == BLOCK_AIR);
        REQUIRE(section.isEmpty());
        REQUIRE(section.countNonAir() == 0);
    }

    SECTION("get/set roundtrip at multiple positions")
    {
        section.setBlock(5, 8, 3, 42);
        REQUIRE(section.getBlock(5, 8, 3) == 42);

        section.setBlock(0, 0, 0, 100);
        REQUIRE(section.getBlock(0, 0, 0) == 100);

        section.setBlock(10, 12, 7, 65535);
        REQUIRE(section.getBlock(10, 12, 7) == 65535);
    }

    SECTION("boundary corners (0,0,0) and (15,15,15)")
    {
        section.setBlock(0, 0, 0, 1);
        REQUIRE(section.getBlock(0, 0, 0) == 1);

        section.setBlock(15, 15, 15, 2);
        REQUIRE(section.getBlock(15, 15, 15) == 2);
    }

    SECTION("fill sets all blocks to target ID")
    {
        section.fill(7);
        REQUIRE(section.getBlock(0, 0, 0) == 7);
        REQUIRE(section.getBlock(8, 8, 8) == 7);
        REQUIRE(section.getBlock(15, 15, 15) == 7);
        REQUIRE_FALSE(section.isEmpty());
        REQUIRE(section.countNonAir() == ChunkSection::VOLUME);
    }

    SECTION("fill with AIR resets section")
    {
        section.fill(42);
        REQUIRE_FALSE(section.isEmpty());

        section.fill(BLOCK_AIR);
        REQUIRE(section.isEmpty());
        REQUIRE(section.countNonAir() == 0);
    }

    SECTION("isEmpty returns true for fresh section, false after setBlock")
    {
        REQUIRE(section.isEmpty());

        section.setBlock(3, 7, 11, 1);
        REQUIRE_FALSE(section.isEmpty());
    }

    SECTION("countNonAir accuracy after various operations")
    {
        REQUIRE(section.countNonAir() == 0);

        section.setBlock(3, 7, 11, 1);
        REQUIRE(section.countNonAir() == 1);

        section.setBlock(0, 0, 0, 5);
        section.setBlock(15, 15, 15, 10);
        REQUIRE(section.countNonAir() == 3);

        // Setting a block back to AIR should decrement
        section.setBlock(3, 7, 11, BLOCK_AIR);
        REQUIRE(section.countNonAir() == 2);
    }

    SECTION("index calculation correctness y*256 + z*16 + x")
    {
        // Set block at (x=3, y=5, z=7) -> index = 5*256 + 7*16 + 3 = 1395
        section.setBlock(3, 5, 7, 99);
        REQUIRE(section.blocks[1395] == 99);

        // Set block at (x=0, y=0, z=0) -> index = 0
        section.setBlock(0, 0, 0, 50);
        REQUIRE(section.blocks[0] == 50);

        // Set block at (x=15, y=15, z=15) -> index = 15*256 + 15*16 + 15 = 4095
        section.setBlock(15, 15, 15, 77);
        REQUIRE(section.blocks[4095] == 77);
    }
}
