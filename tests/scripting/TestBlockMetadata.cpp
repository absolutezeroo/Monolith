#include "voxel/world/BlockInventory.h"
#include "voxel/world/BlockMetadata.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkSerializer.h"
#include "voxel/world/ItemStack.h"

// BinaryIO is an internal header — include from src/ for testing serialization
#include "../../engine/src/world/BinaryIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace voxel::world;
using Catch::Approx;

// ============================================================================
// ItemStack
// ============================================================================

TEST_CASE("ItemStack construction and accessors", "[world][itemstack]")
{
    SECTION("default construction is empty")
    {
        ItemStack stack;
        REQUIRE(stack.isEmpty());
        REQUIRE(stack.getName().empty());
        REQUIRE(stack.getCount() == 0);
    }

    SECTION("parameterized construction")
    {
        ItemStack stack("base:stone", 64);
        REQUIRE_FALSE(stack.isEmpty());
        REQUIRE(stack.getName() == "base:stone");
        REQUIRE(stack.getCount() == 64);
    }

    SECTION("setCount modifies count")
    {
        ItemStack stack("base:dirt", 10);
        stack.setCount(32);
        REQUIRE(stack.getCount() == 32);
    }

    SECTION("clear resets to empty")
    {
        ItemStack stack("base:stone", 64);
        stack.clear();
        REQUIRE(stack.isEmpty());
    }

    SECTION("empty name means empty regardless of count")
    {
        ItemStack stack("", 64);
        REQUIRE(stack.isEmpty());
    }

    SECTION("zero count means empty regardless of name")
    {
        ItemStack stack("base:stone", 0);
        REQUIRE(stack.isEmpty());
    }
}

// ============================================================================
// BlockMetadata
// ============================================================================

TEST_CASE("BlockMetadata typed storage", "[world][metadata]")
{
    BlockMetadata meta;

    SECTION("string values")
    {
        meta.setString("text", "Hello World");
        REQUIRE(meta.getString("text") == "Hello World");
        REQUIRE(meta.getString("missing", "default") == "default");
    }

    SECTION("int values")
    {
        meta.setInt("count", 42);
        REQUIRE(meta.getInt("count") == 42);
        REQUIRE(meta.getInt("missing", -1) == -1);
    }

    SECTION("float values")
    {
        meta.setFloat("temperature", 1200.5f);
        REQUIRE(meta.getFloat("temperature") == Approx(1200.5f));
        REQUIRE(meta.getFloat("missing", 0.0f) == Approx(0.0f));
    }

    SECTION("contains and erase")
    {
        meta.setString("key", "val");
        REQUIRE(meta.contains("key"));
        meta.erase("key");
        REQUIRE_FALSE(meta.contains("key"));
    }

    SECTION("clear and empty")
    {
        REQUIRE(meta.empty());
        meta.setInt("x", 1);
        REQUIRE_FALSE(meta.empty());
        REQUIRE(meta.size() == 1);
        meta.clear();
        REQUIRE(meta.empty());
        REQUIRE(meta.size() == 0);
    }

    SECTION("type mismatch returns default")
    {
        meta.setString("key", "hello");
        REQUIRE(meta.getInt("key", 99) == 99);
        REQUIRE(meta.getFloat("key", 1.0f) == Approx(1.0f));
    }

    SECTION("overwrite with different type")
    {
        meta.setString("key", "hello");
        meta.setInt("key", 42);
        REQUIRE(meta.getInt("key") == 42);
        REQUIRE(meta.getString("key", "gone") == "gone");
    }
}

TEST_CASE("BlockMetadata serialization roundtrip", "[world][metadata][serialization]")
{
    BlockMetadata meta;
    meta.setString("text", "Hello World");
    meta.setInt("count", 42);
    meta.setFloat("temperature", 1200.5f);

    BinaryWriter writer;
    meta.serialize(writer);

    BinaryReader reader(writer.data());
    auto result = BlockMetadata::deserialize(reader);
    REQUIRE(result.has_value());

    const auto& loaded = result.value();
    REQUIRE(loaded.getString("text") == "Hello World");
    REQUIRE(loaded.getInt("count") == 42);
    REQUIRE(loaded.getFloat("temperature") == Approx(1200.5f));
    REQUIRE(loaded.size() == 3);
}

TEST_CASE("BlockMetadata empty serialization roundtrip", "[world][metadata][serialization]")
{
    BlockMetadata meta;

    BinaryWriter writer;
    meta.serialize(writer);

    BinaryReader reader(writer.data());
    auto result = BlockMetadata::deserialize(reader);
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

// ============================================================================
// BlockInventory
// ============================================================================

TEST_CASE("BlockInventory named lists", "[world][inventory]")
{
    BlockInventory inv;

    SECTION("set_size creates list")
    {
        inv.setSize("main", 27);
        REQUIRE(inv.getSize("main") == 27);
        REQUIRE(inv.isEmpty("main"));
    }

    SECTION("set_stack and get_stack")
    {
        inv.setSize("main", 9);
        inv.setStack("main", 0, ItemStack{"base:stone", 64});
        auto stack = inv.getStack("main", 0);
        REQUIRE(stack.getName() == "base:stone");
        REQUIRE(stack.getCount() == 64);
        REQUIRE_FALSE(inv.isEmpty("main"));
    }

    SECTION("out-of-bounds returns empty")
    {
        inv.setSize("main", 3);
        auto stack = inv.getStack("main", 99);
        REQUIRE(stack.isEmpty());
    }

    SECTION("nonexistent list returns empty/0")
    {
        REQUIRE(inv.getSize("nope") == 0);
        REQUIRE(inv.isEmpty("nope"));
        auto stack = inv.getStack("nope", 0);
        REQUIRE(stack.isEmpty());
    }

    SECTION("isEmpty all lists")
    {
        REQUIRE(inv.isEmpty());
        inv.setSize("main", 3);
        REQUIRE(inv.isEmpty()); // All slots empty
        inv.setStack("main", 0, ItemStack{"base:dirt", 1});
        REQUIRE_FALSE(inv.isEmpty());
    }

    SECTION("multiple lists")
    {
        inv.setSize("input", 5);
        inv.setSize("output", 5);
        inv.setStack("input", 0, ItemStack{"base:ore", 10});

        auto names = inv.getListNames();
        REQUIRE(names.size() == 2);
        REQUIRE_FALSE(inv.isEmpty("input"));
        REQUIRE(inv.isEmpty("output"));
    }

    SECTION("resize preserves existing items within new bounds")
    {
        inv.setSize("main", 9);
        inv.setStack("main", 0, ItemStack{"base:stone", 64});
        inv.setSize("main", 3);
        auto stack = inv.getStack("main", 0);
        REQUIRE(stack.getName() == "base:stone");
        REQUIRE(stack.getCount() == 64);
    }

    SECTION("set_stack on nonexistent list is no-op")
    {
        inv.setStack("nope", 0, ItemStack{"base:stone", 1});
        REQUIRE(inv.getSize("nope") == 0);
    }
}

TEST_CASE("BlockInventory serialization roundtrip", "[world][inventory][serialization]")
{
    BlockInventory inv;
    inv.setSize("main", 9);
    inv.setStack("main", 0, ItemStack{"base:stone", 64});
    inv.setStack("main", 5, ItemStack{"base:dirt", 32});
    inv.setSize("fuel", 1);
    inv.setStack("fuel", 0, ItemStack{"base:coal", 8});

    BinaryWriter writer;
    inv.serialize(writer);

    BinaryReader reader(writer.data());
    auto result = BlockInventory::deserialize(reader);
    REQUIRE(result.has_value());

    const auto& loaded = result.value();
    REQUIRE(loaded.getSize("main") == 9);
    REQUIRE(loaded.getStack("main", 0).getName() == "base:stone");
    REQUIRE(loaded.getStack("main", 0).getCount() == 64);
    REQUIRE(loaded.getStack("main", 5).getName() == "base:dirt");
    REQUIRE(loaded.getStack("main", 5).getCount() == 32);
    REQUIRE(loaded.getStack("main", 1).isEmpty());
    REQUIRE(loaded.getSize("fuel") == 1);
    REQUIRE(loaded.getStack("fuel", 0).getName() == "base:coal");
}

// ============================================================================
// ChunkColumn metadata/inventory storage
// ============================================================================

TEST_CASE("ChunkColumn metadata storage", "[world][chunkcolumn][metadata]")
{
    ChunkColumn column({0, 0});

    SECTION("getMetadata returns nullptr when no metadata exists")
    {
        REQUIRE(column.getMetadata(0, 0, 0) == nullptr);
    }

    SECTION("getOrCreateMetadata creates and returns reference")
    {
        auto& meta = column.getOrCreateMetadata(5, 64, 5);
        meta.setString("owner", "player1");
        REQUIRE(column.getMetadata(5, 64, 5) != nullptr);
        REQUIRE(column.getMetadata(5, 64, 5)->getString("owner") == "player1");
    }

    SECTION("removeMetadata removes data")
    {
        column.getOrCreateMetadata(0, 0, 0).setInt("x", 1);
        column.removeMetadata(0, 0, 0);
        REQUIRE(column.getMetadata(0, 0, 0) == nullptr);
    }

    SECTION("setBlock clears metadata when block changes")
    {
        // Place a block first
        column.setBlock(3, 10, 3, 5);
        column.getOrCreateMetadata(3, 10, 3).setString("data", "hello");
        REQUIRE(column.getMetadata(3, 10, 3) != nullptr);

        // Replace with different block
        column.setBlock(3, 10, 3, 6);
        REQUIRE(column.getMetadata(3, 10, 3) == nullptr);
    }

    SECTION("setBlock with same ID does not clear metadata")
    {
        column.setBlock(3, 10, 3, 5);
        column.getOrCreateMetadata(3, 10, 3).setString("data", "hello");

        // Set same block ID
        column.setBlock(3, 10, 3, 5);
        REQUIRE(column.getMetadata(3, 10, 3) != nullptr);
        REQUIRE(column.getMetadata(3, 10, 3)->getString("data") == "hello");
    }

    SECTION("hasBlockData returns true when metadata exists")
    {
        REQUIRE_FALSE(column.hasBlockData(0, 0, 0));
        column.getOrCreateMetadata(0, 0, 0).setInt("x", 1);
        REQUIRE(column.hasBlockData(0, 0, 0));
    }
}

TEST_CASE("ChunkColumn inventory storage", "[world][chunkcolumn][inventory]")
{
    ChunkColumn column({0, 0});

    SECTION("getInventory returns nullptr when no inventory exists")
    {
        REQUIRE(column.getInventory(0, 0, 0) == nullptr);
    }

    SECTION("getOrCreateInventory creates and returns reference")
    {
        auto& inv = column.getOrCreateInventory(5, 64, 5);
        inv.setSize("main", 27);
        REQUIRE(column.getInventory(5, 64, 5) != nullptr);
        REQUIRE(column.getInventory(5, 64, 5)->getSize("main") == 27);
    }

    SECTION("setBlock clears inventory when block changes")
    {
        column.setBlock(3, 10, 3, 5);
        column.getOrCreateInventory(3, 10, 3).setSize("main", 9);
        column.setBlock(3, 10, 3, 6);
        REQUIRE(column.getInventory(3, 10, 3) == nullptr);
    }
}

TEST_CASE("ChunkColumn packLocalIndex correctness", "[world][chunkcolumn]")
{
    REQUIRE(ChunkColumn::packLocalIndex(0, 0, 0) == 0);
    REQUIRE(ChunkColumn::packLocalIndex(15, 0, 0) == 15);
    REQUIRE(ChunkColumn::packLocalIndex(0, 0, 15) == 15 * 16);
    REQUIRE(ChunkColumn::packLocalIndex(0, 1, 0) == 256);
    REQUIRE(ChunkColumn::packLocalIndex(15, 255, 15) == 15 + 15 * 16 + 255 * 256);
}

// ============================================================================
// ChunkSerializer roundtrip with metadata/inventory
// ============================================================================

TEST_CASE("ChunkSerializer roundtrip with metadata and inventory", "[world][serialization][metadata]")
{
    // Minimal block registry with just air
    BlockRegistry registry;

    ChunkColumn column({5, 7});

    // Add metadata at position (3, 64, 3)
    column.getOrCreateMetadata(3, 64, 3).setString("owner", "player1");
    column.getOrCreateMetadata(3, 64, 3).setInt("level", 5);

    // Add inventory at position (3, 64, 3)
    auto& inv = column.getOrCreateInventory(3, 64, 3);
    inv.setSize("main", 9);
    inv.setStack("main", 0, ItemStack{"base:stone", 64});

    // Serialize
    auto serialized = ChunkSerializer::serializeColumn(column, registry);

    // Deserialize
    auto result = ChunkSerializer::deserializeColumn(serialized, {5, 7}, registry);
    REQUIRE(result.has_value());

    auto& loaded = result.value();

    // Verify metadata
    auto* meta = loaded.getMetadata(3, 64, 3);
    REQUIRE(meta != nullptr);
    REQUIRE(meta->getString("owner") == "player1");
    REQUIRE(meta->getInt("level") == 5);

    // Verify inventory
    auto* loadedInv = loaded.getInventory(3, 64, 3);
    REQUIRE(loadedInv != nullptr);
    REQUIRE(loadedInv->getSize("main") == 9);
    REQUIRE(loadedInv->getStack("main", 0).getName() == "base:stone");
    REQUIRE(loadedInv->getStack("main", 0).getCount() == 64);
}

TEST_CASE("ChunkSerializer backward compat — old format without metadata", "[world][serialization][metadata]")
{
    BlockRegistry registry;
    ChunkColumn column({0, 0});

    // Serialize without any metadata/inventory (but format includes empty sections)
    auto serialized = ChunkSerializer::serializeColumn(column, registry);

    // Manually truncate to simulate old format — remove everything after section bitmask+sections
    // Old format: just [u16 sectionBitmask] (2 bytes for empty column)
    // New format adds metadata/inventory sections after.
    // We simulate old format by truncating to just the section data.
    std::vector<uint8_t> oldFormat(serialized.begin(), serialized.begin() + 2);

    auto result = ChunkSerializer::deserializeColumn(oldFormat, {0, 0}, registry);
    REQUIRE(result.has_value());

    auto& loaded = result.value();
    REQUIRE(loaded.allMetadata().empty());
    REQUIRE(loaded.allInventories().empty());
}
