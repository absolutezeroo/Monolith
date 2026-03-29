#include "voxel/world/LightMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace voxel::world;

TEST_CASE("LightMap default state", "[world][light]")
{
    LightMap lm;

    SECTION("newly constructed LightMap is all zeros")
    {
        REQUIRE(lm.isClear());
        REQUIRE(lm.getSkyLight(0, 0, 0) == 0);
        REQUIRE(lm.getBlockLight(0, 0, 0) == 0);
        REQUIRE(lm.getRaw(0, 0, 0) == 0);
    }
}

TEST_CASE("LightMap set/get sky and block light", "[world][light]")
{
    LightMap lm;

    SECTION("setSkyLight preserves block light")
    {
        lm.setBlockLight(1, 2, 3, 7);
        lm.setSkyLight(1, 2, 3, 12);
        REQUIRE(lm.getSkyLight(1, 2, 3) == 12);
        REQUIRE(lm.getBlockLight(1, 2, 3) == 7);
    }

    SECTION("setBlockLight preserves sky light")
    {
        lm.setSkyLight(5, 5, 5, 14);
        lm.setBlockLight(5, 5, 5, 3);
        REQUIRE(lm.getSkyLight(5, 5, 5) == 14);
        REQUIRE(lm.getBlockLight(5, 5, 5) == 3);
    }

    SECTION("max values (15, 15)")
    {
        lm.setSkyLight(0, 0, 0, 15);
        lm.setBlockLight(0, 0, 0, 15);
        REQUIRE(lm.getSkyLight(0, 0, 0) == 15);
        REQUIRE(lm.getBlockLight(0, 0, 0) == 15);
        REQUIRE(lm.getRaw(0, 0, 0) == 0xFF);
    }

    SECTION("setRaw stores both channels")
    {
        lm.setRaw(3, 3, 3, 0xA5); // sky=10, block=5
        REQUIRE(lm.getSkyLight(3, 3, 3) == 10);
        REQUIRE(lm.getBlockLight(3, 3, 3) == 5);
    }
}

TEST_CASE("LightMap clear resets all values", "[world][light]")
{
    LightMap lm;
    lm.setSkyLight(0, 0, 0, 15);
    lm.setBlockLight(15, 15, 15, 8);

    REQUIRE_FALSE(lm.isClear());
    lm.clear();
    REQUIRE(lm.isClear());
    REQUIRE(lm.getSkyLight(0, 0, 0) == 0);
    REQUIRE(lm.getBlockLight(15, 15, 15) == 0);
}

TEST_CASE("LightMap isClear with single non-zero byte", "[world][light]")
{
    LightMap lm;
    REQUIRE(lm.isClear());
    lm.setBlockLight(7, 8, 9, 1);
    REQUIRE_FALSE(lm.isClear());
}

TEST_CASE("LightMap boundary coordinates", "[world][light]")
{
    LightMap lm;

    lm.setSkyLight(0, 0, 0, 1);
    lm.setSkyLight(15, 0, 0, 2);
    lm.setSkyLight(0, 15, 0, 3);
    lm.setSkyLight(0, 0, 15, 4);
    lm.setSkyLight(15, 15, 15, 5);

    REQUIRE(lm.getSkyLight(0, 0, 0) == 1);
    REQUIRE(lm.getSkyLight(15, 0, 0) == 2);
    REQUIRE(lm.getSkyLight(0, 15, 0) == 3);
    REQUIRE(lm.getSkyLight(0, 0, 15) == 4);
    REQUIRE(lm.getSkyLight(15, 15, 15) == 5);
}

TEST_CASE("LightMap data pointer access", "[world][light]")
{
    LightMap lm;
    lm.setRaw(0, 0, 0, 0xAB);

    const uint8_t* data = lm.data();
    REQUIRE(data[0] == 0xAB); // index 0 = (0,0,0)
}
