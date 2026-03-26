#include "voxel/world/ChunkSerializer.h"

#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/PaletteCompression.h"
#include "voxel/world/RegionFile.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace voxel::world;
using namespace voxel::core;

namespace
{

/// RAII helper to create and clean up a temporary test directory.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        path = std::filesystem::temp_directory_path() / "vf_test_serializer";
        std::filesystem::create_directories(path);
    }

    ~TempDir() { std::filesystem::remove_all(path); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/// Create a BlockRegistry with a few test blocks.
BlockRegistry makeTestRegistry()
{
    BlockRegistry registry;
    auto stoneResult = registry.registerBlock({.stringId = "base:stone", .isSolid = true});
    REQUIRE(stoneResult.has_value());
    auto dirtResult = registry.registerBlock({.stringId = "base:dirt", .isSolid = true});
    REQUIRE(dirtResult.has_value());
    auto grassResult = registry.registerBlock({.stringId = "base:grass", .isSolid = true});
    REQUIRE(grassResult.has_value());
    return registry;
}

} // namespace

TEST_CASE("ChunkSerializer roundtrip", "[world][serialization]")
{
    TempDir tempDir;
    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t dirtId = registry.getIdByName("base:dirt");
    uint16_t grassId = registry.getIdByName("base:grass");
    REQUIRE(stoneId != BLOCK_AIR);
    REQUIRE(dirtId != BLOCK_AIR);
    REQUIRE(grassId != BLOCK_AIR);

    SECTION("filled chunk saves and loads identically")
    {
        ChunkColumn column({0, 0});
        column.setBlock(5, 64, 3, stoneId);
        column.setBlock(0, 0, 0, dirtId);
        column.setBlock(15, 255, 15, grassId);

        auto saveResult = ChunkSerializer::save(column, registry, tempDir.path);
        REQUIRE(saveResult.has_value());

        auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir.path);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        CHECK(loaded.getBlock(5, 64, 3) == stoneId);
        CHECK(loaded.getBlock(0, 0, 0) == dirtId);
        CHECK(loaded.getBlock(15, 255, 15) == grassId);
        CHECK(loaded.getBlock(8, 128, 8) == BLOCK_AIR);
    }

    SECTION("multiple blocks in same section roundtrip")
    {
        ChunkColumn column({3, 7});
        // Fill section 0 with a pattern
        for (int x = 0; x < 16; ++x)
        {
            for (int z = 0; z < 16; ++z)
            {
                column.setBlock(x, 0, z, stoneId);
                column.setBlock(x, 1, z, dirtId);
            }
        }
        column.setBlock(8, 2, 8, grassId);

        auto saveResult = ChunkSerializer::save(column, registry, tempDir.path);
        REQUIRE(saveResult.has_value());

        auto loadResult = ChunkSerializer::load({3, 7}, registry, tempDir.path);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        for (int x = 0; x < 16; ++x)
        {
            for (int z = 0; z < 16; ++z)
            {
                CHECK(loaded.getBlock(x, 0, z) == stoneId);
                CHECK(loaded.getBlock(x, 1, z) == dirtId);
            }
        }
        CHECK(loaded.getBlock(8, 2, 8) == grassId);
        CHECK(loaded.getBlock(0, 3, 0) == BLOCK_AIR);
    }

    SECTION("negative chunk coordinates roundtrip")
    {
        ChunkColumn column({-5, -10});
        column.setBlock(0, 0, 0, stoneId);
        column.setBlock(15, 15, 15, dirtId);

        auto saveResult = ChunkSerializer::save(column, registry, tempDir.path);
        REQUIRE(saveResult.has_value());

        auto loadResult = ChunkSerializer::load({-5, -10}, registry, tempDir.path);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        CHECK(loaded.getBlock(0, 0, 0) == stoneId);
        CHECK(loaded.getBlock(15, 15, 15) == dirtId);
    }

    SECTION("multiple chunks in same region")
    {
        ChunkColumn col1({0, 0});
        col1.setBlock(0, 0, 0, stoneId);
        ChunkColumn col2({1, 0});
        col2.setBlock(0, 0, 0, dirtId);
        ChunkColumn col3({0, 1});
        col3.setBlock(0, 0, 0, grassId);

        REQUIRE(ChunkSerializer::save(col1, registry, tempDir.path).has_value());
        REQUIRE(ChunkSerializer::save(col2, registry, tempDir.path).has_value());
        REQUIRE(ChunkSerializer::save(col3, registry, tempDir.path).has_value());

        auto load1 = ChunkSerializer::load({0, 0}, registry, tempDir.path);
        auto load2 = ChunkSerializer::load({1, 0}, registry, tempDir.path);
        auto load3 = ChunkSerializer::load({0, 1}, registry, tempDir.path);
        REQUIRE(load1.has_value());
        REQUIRE(load2.has_value());
        REQUIRE(load3.has_value());

        CHECK(load1.value().getBlock(0, 0, 0) == stoneId);
        CHECK(load2.value().getBlock(0, 0, 0) == dirtId);
        CHECK(load3.value().getBlock(0, 0, 0) == grassId);
    }
}

TEST_CASE("ChunkSerializer empty chunk", "[world][serialization]")
{
    TempDir tempDir;
    BlockRegistry registry = makeTestRegistry();

    SECTION("all-air column serializes minimally and roundtrips")
    {
        ChunkColumn column({0, 0});
        // Don't set any blocks — everything is air

        auto saveResult = ChunkSerializer::save(column, registry, tempDir.path);
        REQUIRE(saveResult.has_value());

        auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir.path);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        // Verify all sections are empty
        for (int y = 0; y < ChunkColumn::COLUMN_HEIGHT; ++y)
        {
            CHECK(loaded.getBlock(0, y, 0) == BLOCK_AIR);
        }
    }

    SECTION("empty column binary is small (just a bitmask)")
    {
        ChunkColumn column({0, 0});
        std::vector<uint8_t> serialized = ChunkSerializer::serializeColumn(column, registry);
        // Only the 2-byte section bitmask (all zeros)
        CHECK(serialized.size() == 2);
    }
}

TEST_CASE("ChunkSerializer missing file handling", "[world][serialization]")
{
    TempDir tempDir;
    BlockRegistry registry = makeTestRegistry();

    SECTION("non-existent region returns FileNotFound")
    {
        auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir.path);
        REQUIRE_FALSE(loadResult.has_value());
        CHECK(loadResult.error().code == ErrorCode::FileNotFound);
    }

    SECTION("valid region but unwritten chunk returns ChunkNotLoaded")
    {
        // Save one chunk to create the region file
        ChunkColumn column({0, 0});
        column.setBlock(0, 0, 0, registry.getIdByName("base:stone"));
        REQUIRE(ChunkSerializer::save(column, registry, tempDir.path).has_value());

        // Try to load a different chunk in the same region
        auto loadResult = ChunkSerializer::load({5, 5}, registry, tempDir.path);
        REQUIRE_FALSE(loadResult.has_value());
        CHECK(loadResult.error().code == ErrorCode::ChunkNotLoaded);
    }
}

TEST_CASE("ChunkSerializer corrupt data handling", "[world][serialization]")
{
    TempDir tempDir;
    BlockRegistry registry = makeTestRegistry();

    SECTION("truncated/garbage data returns InvalidFormat")
    {
        // Save a valid chunk first to create the region file
        ChunkColumn column({0, 0});
        column.setBlock(0, 0, 0, registry.getIdByName("base:stone"));
        REQUIRE(ChunkSerializer::save(column, registry, tempDir.path).has_value());

        // Overwrite the region file data with garbage
        auto regionPath = tempDir.path / "r.0.0.vxr";
        {
            std::fstream file(regionPath, std::ios::in | std::ios::out | std::ios::binary);
            REQUIRE(file.is_open());

            // Read the header to find the chunk's offset
            uint32_t offset = 0;
            uint32_t size = 0;
            file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            REQUIRE(offset > 0);
            REQUIRE(size > 0);

            // Write garbage at the chunk data location
            file.seekp(static_cast<std::streamoff>(offset));
            std::vector<uint8_t> garbage(size, 0xDE);
            // Keep the uncompressed size prefix valid but corrupt the LZ4 data
            file.write(reinterpret_cast<const char*>(garbage.data()), static_cast<std::streamsize>(size));
        }

        auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir.path);
        REQUIRE_FALSE(loadResult.has_value());
        CHECK(loadResult.error().code == ErrorCode::InvalidFormat);
    }
}

TEST_CASE("ChunkSerializer ID remapping", "[world][serialization]")
{
    TempDir tempDir;

    SECTION("save with one registry, load with different numeric IDs for same string names")
    {
        // Registry 1: register blocks in order stone, dirt, grass
        BlockRegistry registry1;
        auto s1 = registry1.registerBlock({.stringId = "base:stone", .isSolid = true});
        auto d1 = registry1.registerBlock({.stringId = "base:dirt", .isSolid = true});
        auto g1 = registry1.registerBlock({.stringId = "base:grass", .isSolid = true});
        REQUIRE(s1.has_value());
        REQUIRE(d1.has_value());
        REQUIRE(g1.has_value());

        uint16_t stoneId1 = s1.value();
        uint16_t dirtId1 = d1.value();
        uint16_t grassId1 = g1.value();

        // Save a chunk with registry 1
        ChunkColumn column({0, 0});
        column.setBlock(0, 0, 0, stoneId1);
        column.setBlock(1, 0, 0, dirtId1);
        column.setBlock(2, 0, 0, grassId1);
        REQUIRE(ChunkSerializer::save(column, registry1, tempDir.path).has_value());

        // Registry 2: register in DIFFERENT order — grass, stone, dirt
        // This simulates a different mod load order across sessions
        BlockRegistry registry2;
        auto g2 = registry2.registerBlock({.stringId = "base:grass", .isSolid = true});
        auto s2 = registry2.registerBlock({.stringId = "base:stone", .isSolid = true});
        auto d2 = registry2.registerBlock({.stringId = "base:dirt", .isSolid = true});
        REQUIRE(g2.has_value());
        REQUIRE(s2.has_value());
        REQUIRE(d2.has_value());

        uint16_t stoneId2 = s2.value();
        uint16_t dirtId2 = d2.value();
        uint16_t grassId2 = g2.value();

        // Numeric IDs should be different between registries
        // (unless they happen to match, which they won't with different order)
        REQUIRE(stoneId1 != stoneId2); // stone was 1st in reg1, 2nd in reg2

        // Load with registry 2 — should remap correctly
        auto loadResult = ChunkSerializer::load({0, 0}, registry2, tempDir.path);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        CHECK(loaded.getBlock(0, 0, 0) == stoneId2);
        CHECK(loaded.getBlock(1, 0, 0) == dirtId2);
        CHECK(loaded.getBlock(2, 0, 0) == grassId2);
    }

    SECTION("unknown block on load is substituted with air")
    {
        // Registry 1: has "base:obsidian"
        BlockRegistry registry1;
        auto obs = registry1.registerBlock({.stringId = "base:obsidian", .isSolid = true});
        REQUIRE(obs.has_value());

        ChunkColumn column({0, 0});
        column.setBlock(0, 0, 0, obs.value());
        REQUIRE(ChunkSerializer::save(column, registry1, tempDir.path).has_value());

        // Registry 2: does NOT have "base:obsidian"
        BlockRegistry registry2;
        auto stoneReg = registry2.registerBlock({.stringId = "base:stone", .isSolid = true});
        REQUIRE(stoneReg.has_value());

        auto loadResult = ChunkSerializer::load({0, 0}, registry2, tempDir.path);
        REQUIRE(loadResult.has_value());

        // Unknown "base:obsidian" should become air
        CHECK(loadResult.value().getBlock(0, 0, 0) == BLOCK_AIR);
    }
}

TEST_CASE("RegionFile compact eliminates dead space", "[world][serialization]")
{
    TempDir tempDir;
    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");
    uint16_t dirtId = registry.getIdByName("base:dirt");

    // Save a chunk, then overwrite it (creates dead data from the first write)
    ChunkColumn column({0, 0});
    column.setBlock(0, 0, 0, stoneId);
    REQUIRE(ChunkSerializer::save(column, registry, tempDir.path).has_value());

    auto regionPath = tempDir.path / "r.0.0.vxr";
    auto sizeBeforeOverwrite = std::filesystem::file_size(regionPath);

    // Overwrite same chunk with different data
    column.setBlock(1, 0, 0, dirtId);
    REQUIRE(ChunkSerializer::save(column, registry, tempDir.path).has_value());

    auto sizeAfterOverwrite = std::filesystem::file_size(regionPath);
    // File grew because old data is still there (append-only)
    REQUIRE(sizeAfterOverwrite > sizeBeforeOverwrite);

    // Compact should shrink the file
    auto compactResult = RegionFile::compact(regionPath);
    REQUIRE(compactResult.has_value());

    auto sizeAfterCompact = std::filesystem::file_size(regionPath);
    CHECK(sizeAfterCompact < sizeAfterOverwrite);

    // Data should still load correctly
    auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir.path);
    REQUIRE(loadResult.has_value());
    CHECK(loadResult.value().getBlock(0, 0, 0) == stoneId);
    CHECK(loadResult.value().getBlock(1, 0, 0) == dirtId);
}

TEST_CASE("ChunkSerializer serializeColumn/deserializeColumn direct", "[world][serialization]")
{
    BlockRegistry registry = makeTestRegistry();
    uint16_t stoneId = registry.getIdByName("base:stone");

    SECTION("serialize and deserialize without LZ4")
    {
        ChunkColumn column({2, 3});
        column.setBlock(7, 42, 9, stoneId);

        std::vector<uint8_t> binary = ChunkSerializer::serializeColumn(column, registry);
        REQUIRE(binary.size() > 2); // More than just the bitmask

        auto result = ChunkSerializer::deserializeColumn(binary, {2, 3}, registry);
        REQUIRE(result.has_value());
        CHECK(result.value().getBlock(7, 42, 9) == stoneId);
        CHECK(result.value().getBlock(0, 0, 0) == BLOCK_AIR);
    }
}
