#include "voxel/world/PaletteCompression.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace voxel::world;

// Helper: set block by linear index, routing through setBlock() to keep m_nonAirCount consistent
static void setBlockByIndex(ChunkSection& section, int i, uint16_t id)
{
    int x = i % ChunkSection::SIZE;
    int z = (i / ChunkSection::SIZE) % ChunkSection::SIZE;
    int y = i / (ChunkSection::SIZE * ChunkSection::SIZE);
    section.setBlock(x, y, z, id);
}

// Helper: verify all 4096 blocks match between two sections
static void requireBlocksEqual(const ChunkSection& a, const ChunkSection& b)
{
    for (int i = 0; i < ChunkSection::VOLUME; ++i)
    {
        REQUIRE(a.data()[i] == b.data()[i]);
    }
}

// --- Roundtrip Tests ---

TEST_CASE("PaletteCompression: roundtrip identity with random fill", "[world][palette]")
{
    ChunkSection section;
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint16_t> dist(0, 50);

    for (int i = 0; i < ChunkSection::VOLUME; ++i)
    {
        setBlockByIndex(section, i, dist(rng));
    }

    CompressedSection compressed = PaletteCompression::compress(section);
    ChunkSection decompressed = PaletteCompression::decompress(compressed);

    requireBlocksEqual(section, decompressed);
}

// --- Tier Selection Tests ---

TEST_CASE("PaletteCompression: tier selection", "[world][palette]")
{
    SECTION("uniform section (all stone) -> tier 0")
    {
        ChunkSection section;
        section.fill(1); // stone

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 0);
        REQUIRE(compressed.palette.size() == 1);
        REQUIRE(compressed.palette[0] == 1);
        REQUIRE(compressed.data.empty());

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("two types (air + stone) -> tier 1")
    {
        ChunkSection section;
        // First half air (already default), second half stone (1)
        for (int i = ChunkSection::VOLUME / 2; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, 1);
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 1);
        REQUIRE(compressed.palette.size() == 2);
        REQUIRE(compressed.data.size() == 64); // 4096 / 64

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("3-4 types -> tier 2")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 4));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 2);
        REQUIRE(compressed.palette.size() == 4);
        REQUIRE(compressed.data.size() == 128); // 4096 / 32

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("5-16 types -> tier 4")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 16));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 4);
        REQUIRE(compressed.palette.size() == 16);
        REQUIRE(compressed.data.size() == 256); // 4096 / 16

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("17-256 types -> tier 8")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 256));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 8);
        REQUIRE(compressed.palette.size() == 256);
        REQUIRE(compressed.data.size() == 512); // 4096 / 8

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("257+ types -> tier 16 (direct, no palette)")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 300));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 16);
        REQUIRE(compressed.palette.empty());
        REQUIRE(compressed.data.size() == 1024); // 4096 / 4

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }
}

// --- Memory Usage Tests ---

TEST_CASE("PaletteCompression: memoryUsage matches expected values", "[world][palette]")
{
    SECTION("tier 0: 1 type -> 3 bytes")
    {
        ChunkSection section;
        section.fill(1);

        CompressedSection compressed = PaletteCompression::compress(section);

        // sizeof(bitsPerEntry)=1 + 1*sizeof(uint16_t)=2 + 0*sizeof(uint64_t)=0 = 3
        REQUIRE(compressed.memoryUsage() == 3);
    }

    SECTION("tier 1: 2 types -> 517 bytes")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 2));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        // 1 + 2*2 + 64*8 = 1 + 4 + 512 = 517
        REQUIRE(compressed.memoryUsage() == 517);
    }

    SECTION("tier 2: 4 types -> 1033 bytes")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 4));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        // 1 + 4*2 + 128*8 = 1 + 8 + 1024 = 1033
        REQUIRE(compressed.memoryUsage() == 1033);
    }

    SECTION("tier 4: 16 types -> 2081 bytes")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 16));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        // 1 + 16*2 + 256*8 = 1 + 32 + 2048 = 2081
        REQUIRE(compressed.memoryUsage() == 2081);
    }

    SECTION("tier 8: 256 types -> 4609 bytes")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 256));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        // 1 + 256*2 + 512*8 = 1 + 512 + 4096 = 4609
        REQUIRE(compressed.memoryUsage() == 4609);
    }

    SECTION("tier 16: 4096 unique -> 8193 bytes")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        // 1 + 0*2 + 1024*8 = 1 + 0 + 8192 = 8193
        REQUIRE(compressed.memoryUsage() == 8193);
    }
}

// --- Edge Case Tests ---

TEST_CASE("PaletteCompression: edge cases", "[world][palette]")
{
    SECTION("empty section (all AIR) -> tier 0")
    {
        ChunkSection section; // constructor fills with BLOCK_AIR (0)

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 0);
        REQUIRE(compressed.palette.size() == 1);
        REQUIRE(compressed.palette[0] == 0); // BLOCK_AIR
        REQUIRE(compressed.data.empty());

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("every block unique (4096 unique IDs) -> tier 16")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 16);
        REQUIRE(compressed.palette.empty());

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("3 types -> tier 2")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 3));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 2);
        REQUIRE(compressed.palette.size() == 3);

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("5 types -> tier 4")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 5));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 4);
        REQUIRE(compressed.palette.size() == 5);

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("17 types -> tier 8")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 17));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 8);
        REQUIRE(compressed.palette.size() == 17);

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("257 types -> tier 16")
    {
        ChunkSection section;
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, static_cast<uint16_t>(i % 257));
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 16);
        REQUIRE(compressed.palette.empty());

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }

    SECTION("large block IDs near UINT16_MAX roundtrip correctly")
    {
        ChunkSection section;
        uint16_t largeIds[] = {0, 1, 32768, 65534, 65535};
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            setBlockByIndex(section, i, largeIds[i % 5]);
        }

        CompressedSection compressed = PaletteCompression::compress(section);

        REQUIRE(compressed.bitsPerEntry == 4); // 5 unique -> tier 4
        REQUIRE(compressed.palette.size() == 5);

        ChunkSection decompressed = PaletteCompression::decompress(compressed);
        requireBlocksEqual(section, decompressed);
    }
}
