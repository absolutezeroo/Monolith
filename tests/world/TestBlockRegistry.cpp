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
        REQUIRE(result.error().code == voxel::core::ErrorCode::InvalidArgument);
    }

    SECTION("invalid namespace format rejected — no colon")
    {
        BlockDefinition bad;
        bad.stringId = "nonamespace";
        auto result = registry.registerBlock(bad);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == voxel::core::ErrorCode::InvalidArgument);
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
        REQUIRE(result.error().code == voxel::core::ErrorCode::FileNotFound);
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
        REQUIRE(result.error().code == voxel::core::ErrorCode::InvalidFormat);

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
        REQUIRE(result.error().code == voxel::core::ErrorCode::InvalidFormat);

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

TEST_CASE("BlockDefinition defaults", "[world]")
{
    SECTION("default-constructed BlockDefinition has correct enum defaults")
    {
        BlockDefinition def;
        REQUIRE(def.renderType == RenderType::Opaque);
        REQUIRE(def.modelType == ModelType::FullCube);
        REQUIRE(def.liquidType == LiquidType::None);
        REQUIRE(def.pushReaction == PushReaction::Normal);
    }

    SECTION("lightFilter default is 0")
    {
        BlockDefinition def;
        REQUIRE(def.lightFilter == 0);
    }

    SECTION("new fields have correct defaults")
    {
        BlockDefinition def;
        REQUIRE(def.tintIndex == 0);
        REQUIRE(def.waving == 0);
        REQUIRE(def.isClimbable == false);
        REQUIRE(def.moveResistance == 0);
        REQUIRE(def.damagePerSecond == 0);
        REQUIRE(def.drowning == 0);
        REQUIRE(def.isBuildableTo == false);
        REQUIRE(def.isFloodable == false);
        REQUIRE(def.isReplaceable == false);
        REQUIRE(def.groups.empty());
        REQUIRE(def.soundFootstep.empty());
        REQUIRE(def.soundDig.empty());
        REQUIRE(def.soundPlace.empty());
        REQUIRE(def.liquidViscosity == 0);
        REQUIRE(def.liquidRange == 8);
        REQUIRE(def.liquidRenewable == true);
        REQUIRE(def.liquidAlternativeFlowing.empty());
        REQUIRE(def.liquidAlternativeSource.empty());
        REQUIRE(def.postEffectColor == 0);
        REQUIRE(def.isFallingBlock == false);
        REQUIRE(def.powerOutput == 0);
        REQUIRE(def.isPowerSource == false);
        REQUIRE(def.isPowerConductor == true);
    }
}

TEST_CASE("BlockRegistry JSON new fields", "[world]")
{
    BlockRegistry registry;

    SECTION("loadFromJson parses renderType cutout")
    {
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::filesystem::path testPath = tempDir / "test_rendertype.json";
        {
            std::ofstream out(testPath);
            out << R"([{"stringId": "test:glass", "renderType": "cutout"}])";
        }

        auto result = registry.loadFromJson(testPath);
        REQUIRE(result.has_value());
        auto id = registry.getIdByName("test:glass");
        REQUIRE(id != BLOCK_AIR);
        REQUIRE(registry.getBlock(id).renderType == RenderType::Cutout);

        std::filesystem::remove(testPath);
    }

    SECTION("loadFromJson parses groups map")
    {
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::filesystem::path testPath = tempDir / "test_groups.json";
        {
            std::ofstream out(testPath);
            out << R"([{"stringId": "test:stone", "groups": {"cracky": 3, "stone": 1}}])";
        }

        auto result = registry.loadFromJson(testPath);
        REQUIRE(result.has_value());
        auto id = registry.getIdByName("test:stone");
        REQUIRE(id != BLOCK_AIR);
        const auto& block = registry.getBlock(id);
        REQUIRE(block.groups.size() == 2);
        REQUIRE(block.groups.at("cracky") == 3);
        REQUIRE(block.groups.at("stone") == 1);

        std::filesystem::remove(testPath);
    }

    SECTION("loadFromJson omitted fields use defaults (backward compatibility)")
    {
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::filesystem::path testPath = tempDir / "test_defaults.json";
        {
            std::ofstream out(testPath);
            out << R"([{"stringId": "test:minimal"}])";
        }

        auto result = registry.loadFromJson(testPath);
        REQUIRE(result.has_value());
        auto id = registry.getIdByName("test:minimal");
        REQUIRE(id != BLOCK_AIR);
        const auto& block = registry.getBlock(id);
        REQUIRE(block.renderType == RenderType::Opaque);
        REQUIRE(block.modelType == ModelType::FullCube);
        REQUIRE(block.liquidType == LiquidType::None);
        REQUIRE(block.pushReaction == PushReaction::Normal);
        REQUIRE(block.lightFilter == 0);
        REQUIRE(block.groups.empty());
        REQUIRE(block.isFallingBlock == false);
        REQUIRE(block.isFloodable == false);

        std::filesystem::remove(testPath);
    }

    SECTION("water block from JSON has correct liquid and physics fields")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        auto result = registry.loadFromJson(blocksPath);
        REQUIRE(result.has_value());

        auto waterId = registry.getIdByName("base:water");
        REQUIRE(waterId != BLOCK_AIR);
        const auto& water = registry.getBlock(waterId);
        REQUIRE(water.liquidType == LiquidType::Source);
        REQUIRE(water.drowning == 1);
        REQUIRE(water.moveResistance == 3);
        REQUIRE(water.isReplaceable == true);
        REQUIRE(water.renderType == RenderType::Translucent);
        REQUIRE(water.liquidViscosity == 1);
        REQUIRE(water.liquidRange == 8);
        REQUIRE(water.liquidRenewable == true);
        REQUIRE(water.postEffectColor == 0x80000044);
    }

    SECTION("existing blocks.json still loads all 10 blocks correctly")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        auto result = registry.loadFromJson(blocksPath);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 10);
        REQUIRE(registry.blockCount() == 11);
    }

    SECTION("oak_leaves has cutout renderType and waving")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        [[maybe_unused]] auto r = registry.loadFromJson(blocksPath);

        auto leavesId = registry.getIdByName("base:oak_leaves");
        REQUIRE(leavesId != BLOCK_AIR);
        const auto& leaves = registry.getBlock(leavesId);
        REQUIRE(leaves.renderType == RenderType::Cutout);
        REQUIRE(leaves.waving == 1);
        REQUIRE(leaves.isFloodable == true);
        REQUIRE(leaves.groups.at("choppy") == 3);
        REQUIRE(leaves.groups.at("leafdecay") == 3);
    }

    SECTION("torch has correct modelType and groups")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        [[maybe_unused]] auto r = registry.loadFromJson(blocksPath);

        auto torchId = registry.getIdByName("base:torch");
        REQUIRE(torchId != BLOCK_AIR);
        const auto& torch = registry.getBlock(torchId);
        REQUIRE(torch.modelType == ModelType::Torch);
        REQUIRE(torch.isFloodable == true);
        REQUIRE(torch.isBuildableTo == false);
        REQUIRE(torch.groups.at("dig_immediate") == 3);
    }

    SECTION("sand has isFallingBlock and groups")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        [[maybe_unused]] auto r = registry.loadFromJson(blocksPath);

        auto sandId = registry.getIdByName("base:sand");
        REQUIRE(sandId != BLOCK_AIR);
        const auto& sand = registry.getBlock(sandId);
        REQUIRE(sand.isFallingBlock == true);
        REQUIRE(sand.groups.at("crumbly") == 3);
        REQUIRE(sand.groups.at("falling_node") == 1);
    }

    SECTION("stone has groups parsed correctly")
    {
        std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
        auto blocksPath = assetsDir / "scripts" / "base" / "blocks.json";
        [[maybe_unused]] auto r = registry.loadFromJson(blocksPath);

        auto stoneId = registry.getIdByName("base:stone");
        REQUIRE(stoneId != BLOCK_AIR);
        const auto& stone = registry.getBlock(stoneId);
        REQUIRE(stone.groups.at("cracky") == 3);
        REQUIRE(stone.groups.at("stone") == 1);
        REQUIRE(stone.lightFilter == 15);
    }
}
