#include "voxel/renderer/ChunkMesh.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkSection.h"
#include "voxel/world/LightMap.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
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
    for (auto& t : def.textureIndices)
        t = 1;
    auto result = registry.registerBlock(std::move(def));
    REQUIRE(result.has_value());
    return registry.getIdByName("base:stone");
}

static constexpr std::array<const ChunkSection*, 6> NO_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr std::array<const LightMap*, 6> NO_LIGHT_NEIGHBORS = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

TEST_CASE("MeshBuilder produces parallel light data (naive)", "[renderer][meshing][light]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("light vector size matches quad count when no LightMap")
    {
        ChunkSection section;
        section.setBlock(5, 5, 5, stoneId);
        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        REQUIRE(mesh.quadLightData.size() == mesh.quads.size());

        // Default: sky=15, block=0 for all corners
        for (uint32_t light : mesh.quadLightData)
        {
            REQUIRE(light == DEFAULT_CORNER_LIGHT);
        }
    }

    SECTION("light vector size matches quad count with LightMap")
    {
        ChunkSection section;
        section.setBlock(5, 5, 5, stoneId);

        LightMap lightMap;
        // Set some light around the block
        lightMap.setSkyLight(5, 6, 5, 10); // above
        lightMap.setBlockLight(6, 5, 5, 8); // +X neighbor

        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS, &lightMap, NO_LIGHT_NEIGHBORS);

        REQUIRE(mesh.quadCount == 6);
        REQUIRE(mesh.quadLightData.size() == mesh.quads.size());
    }

    SECTION("empty section produces no light data")
    {
        ChunkSection section;
        ChunkMesh mesh = builder.buildNaive(section, NO_NEIGHBORS);
        REQUIRE(mesh.quadLightData.empty());
    }
}

TEST_CASE("MeshBuilder produces parallel light data (greedy)", "[renderer][meshing][light]")
{
    BlockRegistry registry;
    uint16_t stoneId = registerStone(registry);
    MeshBuilder builder(registry);

    SECTION("light vector size matches quad count when no LightMap")
    {
        ChunkSection section;
        section.setBlock(0, 0, 0, stoneId);
        section.setBlock(1, 0, 0, stoneId);
        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS);

        REQUIRE(mesh.quadCount > 0);
        REQUIRE(mesh.quadLightData.size() == mesh.quads.size());

        for (uint32_t light : mesh.quadLightData)
        {
            REQUIRE(light == DEFAULT_CORNER_LIGHT);
        }
    }

    SECTION("greedy merge: light doesn't affect merging")
    {
        // Two adjacent blocks with different neighbor light should still merge
        ChunkSection section;
        section.setBlock(0, 0, 0, stoneId);
        section.setBlock(1, 0, 0, stoneId);

        LightMap lightMap;
        // Different light above each block
        lightMap.setSkyLight(0, 1, 0, 10);
        lightMap.setSkyLight(1, 1, 0, 5);

        ChunkMesh mesh = builder.buildGreedy(section, NO_NEIGHBORS, &lightMap, NO_LIGHT_NEIGHBORS);

        REQUIRE(mesh.quadCount > 0);
        REQUIRE(mesh.quadLightData.size() == mesh.quads.size());
    }
}

TEST_CASE("packCornerLight / unpackCornerLightByte round-trip", "[renderer][meshing][light]")
{
    uint32_t packed = packCornerLight(15, 0, 10, 5, 7, 3, 0, 15);

    // corner 0: sky=15, block=0 → byte = 0xF0
    REQUIRE(unpackCornerLightByte(packed, 0) == 0xF0);
    // corner 1: sky=10, block=5 → byte = 0xA5
    REQUIRE(unpackCornerLightByte(packed, 1) == 0xA5);
    // corner 2: sky=7, block=3 → byte = 0x73
    REQUIRE(unpackCornerLightByte(packed, 2) == 0x73);
    // corner 3: sky=0, block=15 → byte = 0x0F
    REQUIRE(unpackCornerLightByte(packed, 3) == 0x0F);
}

TEST_CASE("DEFAULT_CORNER_LIGHT is sky=15, block=0 for all corners", "[renderer][meshing][light]")
{
    for (uint8_t c = 0; c < 4; ++c)
    {
        uint8_t byte = unpackCornerLightByte(DEFAULT_CORNER_LIGHT, c);
        REQUIRE(((byte >> 4) & 0xF) == 15); // sky
        REQUIRE((byte & 0xF) == 0);          // block
    }
}
