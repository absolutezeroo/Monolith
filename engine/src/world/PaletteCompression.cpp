#include "voxel/world/PaletteCompression.h"

#include "voxel/core/Assert.h"

#include <unordered_map>

namespace voxel::world
{

// --- CompressedSection ---

size_t CompressedSection::memoryUsage() const
{
    return sizeof(bitsPerEntry) + palette.size() * sizeof(uint16_t) + data.size() * sizeof(uint64_t);
}

// --- PaletteCompression helpers ---

namespace
{

/// Inverse of ChunkSection::toIndex — decomposes linear index to block coordinates.
constexpr void indexToCoord(int i, int& x, int& y, int& z)
{
    x = i % ChunkSection::SIZE;
    z = (i / ChunkSection::SIZE) % ChunkSection::SIZE;
    y = i / (ChunkSection::SIZE * ChunkSection::SIZE);
}

uint8_t selectBitsPerEntry(uint32_t uniqueCount)
{
    if (uniqueCount <= 1)
    {
        return 0;
    }
    if (uniqueCount <= 2)
    {
        return 1;
    }
    if (uniqueCount <= 4)
    {
        return 2;
    }
    if (uniqueCount <= 16)
    {
        return 4;
    }
    if (uniqueCount <= 256)
    {
        return 8;
    }
    return 16;
}

} // namespace

// --- compress ---

CompressedSection PaletteCompression::compress(const ChunkSection& section)
{
    // Build palette: scan all blocks to collect unique IDs
    std::unordered_map<uint16_t, uint16_t> blockToPaletteIndex;
    std::vector<uint16_t> palette;

    for (int i = 0; i < ChunkSection::VOLUME; ++i)
    {
        uint16_t blockId = section.data()[i];
        if (!blockToPaletteIndex.contains(blockId))
        {
            blockToPaletteIndex[blockId] = static_cast<uint16_t>(palette.size());
            palette.push_back(blockId);
        }
    }

    uint32_t uniqueCount = static_cast<uint32_t>(palette.size());
    uint8_t bitsPerEntry = selectBitsPerEntry(uniqueCount);

    CompressedSection result;
    result.bitsPerEntry = bitsPerEntry;

    // Tier 0: single value — palette only, no data
    if (bitsPerEntry == 0)
    {
        result.palette = std::move(palette);
        // data stays empty
        return result;
    }

    // Tier 16: direct — no palette, pack raw uint16_t into uint64_t words
    if (bitsPerEntry == 16)
    {
        // No palette for direct mode
        int totalBits = ChunkSection::VOLUME * 16;
        int wordCount = (totalBits + 63) / 64;
        result.data.resize(static_cast<size_t>(wordCount), 0);

        int entriesPerWord = 4; // 64 / 16
        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            int wordIndex = i / entriesPerWord;
            int bitOffset = (i % entriesPerWord) * 16;
            result.data[wordIndex] |= static_cast<uint64_t>(section.data()[i]) << bitOffset;
        }
        return result;
    }

    // Tiers 1, 2, 4, 8: pack palette indices
    result.palette = std::move(palette);
    int totalBits = ChunkSection::VOLUME * bitsPerEntry;
    int wordCount = (totalBits + 63) / 64;
    result.data.resize(static_cast<size_t>(wordCount), 0);

    int entriesPerWord = 64 / bitsPerEntry;
    uint64_t mask = (1ULL << bitsPerEntry) - 1;

    for (int i = 0; i < ChunkSection::VOLUME; ++i)
    {
        uint16_t paletteIndex = blockToPaletteIndex[section.data()[i]];
        int wordIndex = i / entriesPerWord;
        int bitOffset = (i % entriesPerWord) * bitsPerEntry;
        result.data[wordIndex] |= (static_cast<uint64_t>(paletteIndex) & mask) << bitOffset;
    }

    return result;
}

// --- decompress ---

ChunkSection PaletteCompression::decompress(const CompressedSection& compressed)
{
    ChunkSection section;

    // Tier 0: single value — fill entire section
    if (compressed.bitsPerEntry == 0)
    {
        VX_ASSERT(!compressed.palette.empty(), "Tier 0 must have at least one palette entry");
        section.fill(compressed.palette[0]);
        return section;
    }

    // Tier 16: direct — unpack raw uint16_t from uint64_t words
    if (compressed.bitsPerEntry == 16)
    {
        int entriesPerWord = 4; // 64 / 16
        uint64_t mask = 0xFFFF;

        for (int i = 0; i < ChunkSection::VOLUME; ++i)
        {
            int wordIndex = i / entriesPerWord;
            int bitOffset = (i % entriesPerWord) * 16;
            uint16_t blockId = static_cast<uint16_t>((compressed.data[wordIndex] >> bitOffset) & mask);
            int x, y, z;
            indexToCoord(i, x, y, z);
            section.setBlock(x, y, z, blockId);
        }
        return section;
    }

    // Tiers 1, 2, 4, 8: unpack palette indices
    VX_ASSERT(
        compressed.bitsPerEntry == 1 || compressed.bitsPerEntry == 2 || compressed.bitsPerEntry == 4
            || compressed.bitsPerEntry == 8,
        "Invalid bitsPerEntry for palette mode"
    );

    int entriesPerWord = 64 / compressed.bitsPerEntry;
    uint64_t mask = (1ULL << compressed.bitsPerEntry) - 1;

    for (int i = 0; i < ChunkSection::VOLUME; ++i)
    {
        int wordIndex = i / entriesPerWord;
        int bitOffset = (i % entriesPerWord) * compressed.bitsPerEntry;
        uint16_t paletteIndex = static_cast<uint16_t>((compressed.data[wordIndex] >> bitOffset) & mask);

        VX_ASSERT(paletteIndex < compressed.palette.size(), "Palette index out of bounds");
        int x, y, z;
        indexToCoord(i, x, y, z);
        section.setBlock(x, y, z, compressed.palette[paletteIndex]);
    }

    return section;
}

} // namespace voxel::world
