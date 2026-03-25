#include "voxel/world/BlockRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace voxel::world;

TEST_CASE("BlockRegistry", "[world]")
{
    BlockRegistry registry;

    SECTION("constructor registers air at ID 0")
    {
        REQUIRE(registry.blockCount() == 1);
        REQUIRE(registry.getBlock(0).stringId == "base:air");
        REQUIRE(registry.getBlock(0).isSolid == false);
        REQUIRE(registry.getBlock(0).isTransparent == true);
        REQUIRE(registry.getBlock(0).hasCollision == false);
        REQUIRE(registry.getIdByName("base:air") == BLOCK_AIR);
    }

    SECTION("registerBlock assigns sequential IDs")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        auto result = registry.registerBlock(stone);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 1);

        BlockDefinition dirt;
        dirt.stringId = "base:dirt";
        result = registry.registerBlock(dirt);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 2);
    }

    SECTION("getBlock returns correct definition by ID")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        stone.hardness = 1.5f;
        stone.isSolid = true;
        auto id = registry.registerBlock(stone);
        REQUIRE(id.has_value());

        const auto& block = registry.getBlock(id.value());
        REQUIRE(block.stringId == "base:stone");
        REQUIRE(block.hardness == 1.5f);
        REQUIRE(block.isSolid == true);
    }

    SECTION("getIdByName returns correct ID by string name")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        auto id = registry.registerBlock(stone);
        REQUIRE(id.has_value());
        REQUIRE(registry.getIdByName("base:stone") == id.value());
    }

    SECTION("getIdByName returns BLOCK_AIR for unknown name")
    {
        REQUIRE(registry.getIdByName("base:nonexistent") == BLOCK_AIR);
    }

    SECTION("duplicate name registration is rejected")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        REQUIRE(registry.registerBlock(stone).has_value());

        BlockDefinition stoneDup;
        stoneDup.stringId = "base:stone";
        auto result = registry.registerBlock(stoneDup);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == voxel::core::EngineError::InvalidArgument);
    }

    SECTION("invalid namespace format rejected — no colon")
    {
        BlockDefinition bad;
        bad.stringId = "nonamespace";
        auto result = registry.registerBlock(bad);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == voxel::core::EngineError::InvalidArgument);
    }

    SECTION("invalid namespace format rejected — empty namespace")
    {
        BlockDefinition bad;
        bad.stringId = ":empty_ns";
        auto result = registry.registerBlock(bad);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("invalid namespace format rejected — empty name")
    {
        BlockDefinition bad;
        bad.stringId = "empty_name:";
        auto result = registry.registerBlock(bad);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("invalid namespace format rejected — multiple colons")
    {
        BlockDefinition bad;
        bad.stringId = "a:b:c";
        auto result = registry.registerBlock(bad);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("blockCount increases with registrations")
    {
        REQUIRE(registry.blockCount() == 1);

        BlockDefinition stone;
        stone.stringId = "base:stone";
        [[maybe_unused]] auto r1 = registry.registerBlock(stone);
        REQUIRE(registry.blockCount() == 2);

        BlockDefinition dirt;
        dirt.stringId = "base:dirt";
        [[maybe_unused]] auto r2 = registry.registerBlock(dirt);
        REQUIRE(registry.blockCount() == 3);
    }
}

TEST_CASE("BlockRegistry JSON loading", "[world]")
{
    BlockRegistry registry;

    SECTION("loadFromJson parses sample blocks.json")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        auto result = registry.loadFromJson(blocksPath);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 10);

        // Verify specific blocks loaded correctly
        REQUIRE(registry.getIdByName("base:stone") != BLOCK_AIR);
        REQUIRE(registry.getBlock(registry.getIdByName("base:stone")).isSolid == true);
        REQUIRE(registry.getBlock(registry.getIdByName("base:stone")).hardness == 1.5f);

        REQUIRE(registry.getIdByName("base:water") != BLOCK_AIR);
        REQUIRE(registry.getBlock(registry.getIdByName("base:water")).isSolid == false);
        REQUIRE(registry.getBlock(registry.getIdByName("base:water")).isTransparent == true);
        REQUIRE(registry.getBlock(registry.getIdByName("base:water")).hasCollision == false);

        REQUIRE(registry.getIdByName("base:glowstone") != BLOCK_AIR);
        REQUIRE(registry.getBlock(registry.getIdByName("base:glowstone")).lightEmission == 15);

        // Total: 1 air + 10 from JSON
        REQUIRE(registry.blockCount() == 11);
    }

    SECTION("loadFromJson returns FileNotFound for missing file")
    {
        auto result = registry.loadFromJson("nonexistent/path.json");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == voxel::core::EngineError::FileNotFound);
    }

    SECTION("loadFromJson returns InvalidFormat for malformed JSON")
    {
        // Create a temporary file with invalid JSON
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::filesystem::path badJsonPath = tempDir / "bad_blocks_test.json";
        {
            std::ofstream out(badJsonPath);
            out << "{ not valid json [[[";
        }

        auto result = registry.loadFromJson(badJsonPath);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == voxel::core::EngineError::InvalidFormat);

        std::filesystem::remove(badJsonPath);
    }

    SECTION("loadFromJson returns InvalidFormat for non-array JSON")
    {
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::filesystem::path objJsonPath = tempDir / "obj_blocks_test.json";
        {
            std::ofstream out(objJsonPath);
            out << R"({"stringId": "base:stone"})";
        }

        auto result = registry.loadFromJson(objJsonPath);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == voxel::core::EngineError::InvalidFormat);

        std::filesystem::remove(objJsonPath);
    }

    SECTION("loadFromJson texture indices are parsed correctly")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        [[maybe_unused]] auto r = registry.loadFromJson(blocksPath);

        // grass_block has asymmetric textures [4, 4, 3, 2, 4, 4]
        auto grassId = registry.getIdByName("base:grass_block");
        REQUIRE(grassId != BLOCK_AIR);
        const auto& grass = registry.getBlock(grassId);
        REQUIRE(grass.textureIndices[0] == 4);
        REQUIRE(grass.textureIndices[2] == 3);
        REQUIRE(grass.textureIndices[3] == 2);
    }
}
