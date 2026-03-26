#include "voxel/world/ChunkSerializer.h"

#include "voxel/core/Log.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/PaletteCompression.h"
#include "voxel/world/RegionFile.h"

#include <lz4.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace voxel::world
{

namespace
{

/// Region coordinate from chunk coordinate.
glm::ivec2 chunkToRegionCoord(glm::ivec2 chunkCoord)
{
    return {floorDiv(chunkCoord.x, REGION_SIZE), floorDiv(chunkCoord.y, REGION_SIZE)};
}

/// Local index within a region (0..1023) from chunk coordinate.
int chunkToRegionIndex(glm::ivec2 chunkCoord)
{
    int localX = euclideanMod(chunkCoord.x, REGION_SIZE);
    int localZ = euclideanMod(chunkCoord.y, REGION_SIZE); // ivec2.y = chunkZ
    return localX * REGION_SIZE + localZ;
}

/// Build the region file path: regionDir/r.{rx}.{rz}.vxr
std::filesystem::path regionFilePath(const std::filesystem::path& regionDir, glm::ivec2 regionCoord)
{
    std::string filename = "r." + std::to_string(regionCoord.x) + "." + std::to_string(regionCoord.y) + ".vxr";
    return regionDir / filename;
}

// --- Binary writer helpers ---

class BinaryWriter
{
public:
    void writeU8(uint8_t v) { m_buf.push_back(v); }

    void writeU16(uint16_t v)
    {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void writeU32(uint32_t v)
    {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void writeU64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            m_buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    }

    void writeString(const std::string& s)
    {
        writeU16(static_cast<uint16_t>(s.size()));
        m_buf.insert(m_buf.end(), s.begin(), s.end());
    }

    [[nodiscard]] std::vector<uint8_t>& data() { return m_buf; }
    [[nodiscard]] const std::vector<uint8_t>& data() const { return m_buf; }

private:
    std::vector<uint8_t> m_buf;
};

// --- Binary reader helpers ---

class BinaryReader
{
public:
    explicit BinaryReader(std::span<const uint8_t> data) : m_data(data) {}

    [[nodiscard]] bool hasRemaining(size_t bytes) const { return m_pos + bytes <= m_data.size(); }

    [[nodiscard]] core::Result<uint8_t> readU8()
    {
        if (!hasRemaining(1))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint8"});
        }
        return m_data[m_pos++];
    }

    [[nodiscard]] core::Result<uint16_t> readU16()
    {
        if (!hasRemaining(2))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint16"});
        }
        uint16_t v = static_cast<uint16_t>(m_data[m_pos]) | (static_cast<uint16_t>(m_data[m_pos + 1]) << 8);
        m_pos += 2;
        return v;
    }

    [[nodiscard]] core::Result<uint32_t> readU32()
    {
        if (!hasRemaining(4))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint32"});
        }
        uint32_t v = static_cast<uint32_t>(m_data[m_pos]) | (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                     (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
                     (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }

    [[nodiscard]] core::Result<uint64_t> readU64()
    {
        if (!hasRemaining(8))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected uint64"});
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
        {
            v |= static_cast<uint64_t>(m_data[m_pos + i]) << (i * 8);
        }
        m_pos += 8;
        return v;
    }

    [[nodiscard]] core::Result<std::string> readString()
    {
        auto lenResult = readU16();
        if (!lenResult.has_value())
        {
            return std::unexpected(lenResult.error());
        }
        uint16_t len = lenResult.value();
        if (!hasRemaining(len))
        {
            return std::unexpected(
                core::EngineError{core::ErrorCode::InvalidFormat, "Truncated data: expected string"});
        }
        std::string s(reinterpret_cast<const char*>(m_data.data() + m_pos), len);
        m_pos += len;
        return s;
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

} // namespace

std::vector<uint8_t> ChunkSerializer::serializeColumn(const ChunkColumn& column, const BlockRegistry& registry)
{
    BinaryWriter writer;

    // Build section bitmask: bit N set = section N is non-null and non-empty
    uint16_t sectionBitmask = 0;
    for (int i = 0; i < ChunkColumn::SECTIONS_PER_COLUMN; ++i)
    {
        const ChunkSection* section = column.getSection(i);
        if (section != nullptr && !section->isEmpty())
        {
            sectionBitmask |= (1u << i);
        }
    }
    writer.writeU16(sectionBitmask);

    // Serialize each non-null section
    for (int i = 0; i < ChunkColumn::SECTIONS_PER_COLUMN; ++i)
    {
        if ((sectionBitmask & (1u << i)) == 0)
        {
            continue;
        }

        const ChunkSection* section = column.getSection(i);
        CompressedSection compressed = PaletteCompression::compress(*section);

        // bitsPerEntry
        writer.writeU8(compressed.bitsPerEntry);

        // Palette as string IDs
        auto paletteSize = static_cast<uint16_t>(compressed.palette.size());
        writer.writeU16(paletteSize);
        for (uint16_t paletteEntry : compressed.palette)
        {
            const BlockDefinition& def = registry.getBlockType(paletteEntry);
            writer.writeString(def.stringId);
        }

        // Packed data words
        auto dataWordCount = static_cast<uint32_t>(compressed.data.size());
        writer.writeU32(dataWordCount);
        for (uint64_t word : compressed.data)
        {
            writer.writeU64(word);
        }
    }

    return std::move(writer.data());
}

core::Result<ChunkColumn> ChunkSerializer::deserializeColumn(
    std::span<const uint8_t> data,
    glm::ivec2 chunkCoord,
    const BlockRegistry& registry)
{
    BinaryReader reader(data);

    auto bitmaskResult = reader.readU16();
    if (!bitmaskResult.has_value())
    {
        return std::unexpected(bitmaskResult.error());
    }
    uint16_t sectionBitmask = bitmaskResult.value();

    ChunkColumn column(chunkCoord);

    for (int i = 0; i < ChunkColumn::SECTIONS_PER_COLUMN; ++i)
    {
        if ((sectionBitmask & (1u << i)) == 0)
        {
            continue; // Section was null/empty — leave as null in column
        }

        // Read bitsPerEntry
        auto bpeResult = reader.readU8();
        if (!bpeResult.has_value())
        {
            return std::unexpected(bpeResult.error());
        }
        uint8_t bitsPerEntry = bpeResult.value();

        // Read palette (string IDs → numeric IDs)
        auto paletteSizeResult = reader.readU16();
        if (!paletteSizeResult.has_value())
        {
            return std::unexpected(paletteSizeResult.error());
        }
        uint16_t paletteSize = paletteSizeResult.value();

        std::vector<uint16_t> palette(paletteSize);
        for (uint16_t p = 0; p < paletteSize; ++p)
        {
            auto strResult = reader.readString();
            if (!strResult.has_value())
            {
                return std::unexpected(strResult.error());
            }
            uint16_t numericId = registry.getIdByName(strResult.value());
            if (numericId == BLOCK_AIR && strResult.value() != "base:air")
            {
                if (core::Log::getLogger())
                {
                    VX_LOG_WARN("Unknown block '{}' during deserialization — substituting air", strResult.value());
                }
            }
            palette[p] = numericId;
        }

        // Read packed data words
        auto wordCountResult = reader.readU32();
        if (!wordCountResult.has_value())
        {
            return std::unexpected(wordCountResult.error());
        }
        uint32_t dataWordCount = wordCountResult.value();

        std::vector<uint64_t> words(dataWordCount);
        for (uint32_t w = 0; w < dataWordCount; ++w)
        {
            auto wordResult = reader.readU64();
            if (!wordResult.has_value())
            {
                return std::unexpected(wordResult.error());
            }
            words[w] = wordResult.value();
        }

        // Reconstruct CompressedSection and decompress
        CompressedSection compressed;
        compressed.bitsPerEntry = bitsPerEntry;
        compressed.palette = std::move(palette);
        compressed.data = std::move(words);

        ChunkSection decompressed = PaletteCompression::decompress(compressed);

        // Copy into the column (getOrCreateSection allocates if needed)
        ChunkSection& target = column.getOrCreateSection(i);
        std::memcpy(target.blocks, decompressed.blocks, sizeof(target.blocks));
        // Re-fill to update internal non-air count
        // We need to set blocks through the API to maintain m_nonAirCount, but blocks[] is public.
        // Since ChunkSection::blocks is public, just copy and reconstruct the count by filling.
        // Actually, let's set each block properly to keep m_nonAirCount consistent.
        target.fill(BLOCK_AIR); // Reset count
        for (int idx = 0; idx < ChunkSection::VOLUME; ++idx)
        {
            int x = idx % ChunkSection::SIZE;
            int z = (idx / ChunkSection::SIZE) % ChunkSection::SIZE;
            int y = idx / (ChunkSection::SIZE * ChunkSection::SIZE);
            if (decompressed.blocks[idx] != BLOCK_AIR)
            {
                target.setBlock(x, y, z, decompressed.blocks[idx]);
            }
        }
    }

    return column;
}

core::Result<void> ChunkSerializer::save(
    const ChunkColumn& column,
    const BlockRegistry& registry,
    const std::filesystem::path& regionDir)
{
    // Ensure region directory exists
    std::filesystem::create_directories(regionDir);

    glm::ivec2 chunkCoord = column.getChunkCoord();
    glm::ivec2 regionCoord = chunkToRegionCoord(chunkCoord);
    int localIndex = chunkToRegionIndex(chunkCoord);
    std::filesystem::path path = regionFilePath(regionDir, regionCoord);

    // Serialize to binary
    std::vector<uint8_t> serialized = serializeColumn(column, registry);

    // LZ4 compress
    int srcSize = static_cast<int>(serialized.size());
    int maxDstSize = LZ4_compressBound(srcSize);

    // Prepend uncompressed size (uint32_t) before LZ4 blob
    std::vector<uint8_t> regionEntry;
    regionEntry.resize(4 + static_cast<size_t>(maxDstSize));

    // Write uncompressed size as little-endian uint32_t
    auto uncompressedSize = static_cast<uint32_t>(srcSize);
    regionEntry[0] = static_cast<uint8_t>(uncompressedSize & 0xFF);
    regionEntry[1] = static_cast<uint8_t>((uncompressedSize >> 8) & 0xFF);
    regionEntry[2] = static_cast<uint8_t>((uncompressedSize >> 16) & 0xFF);
    regionEntry[3] = static_cast<uint8_t>((uncompressedSize >> 24) & 0xFF);

    int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(serialized.data()),
        reinterpret_cast<char*>(regionEntry.data() + 4),
        srcSize,
        maxDstSize);

    if (compressedSize <= 0)
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "LZ4 compression failed"});
    }

    regionEntry.resize(4 + static_cast<size_t>(compressedSize));

    // Write to region file
    return RegionFile::writeChunk(path, localIndex, regionEntry);
}

core::Result<ChunkColumn> ChunkSerializer::load(
    glm::ivec2 chunkCoord,
    const BlockRegistry& registry,
    const std::filesystem::path& regionDir)
{
    glm::ivec2 regionCoord = chunkToRegionCoord(chunkCoord);
    int localIndex = chunkToRegionIndex(chunkCoord);
    std::filesystem::path path = regionFilePath(regionDir, regionCoord);

    // Read from region file
    auto readResult = RegionFile::readChunk(path, localIndex);
    if (!readResult.has_value())
    {
        return std::unexpected(readResult.error());
    }
    const std::vector<uint8_t>& regionEntry = readResult.value();

    // Must have at least the uint32_t uncompressed size prefix
    if (regionEntry.size() < 4)
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "Region entry too small (missing size prefix)"});
    }

    // Read uncompressed size (little-endian uint32_t)
    uint32_t uncompressedSize = static_cast<uint32_t>(regionEntry[0]) |
                                (static_cast<uint32_t>(regionEntry[1]) << 8) |
                                (static_cast<uint32_t>(regionEntry[2]) << 16) |
                                (static_cast<uint32_t>(regionEntry[3]) << 24);

    int compressedSize = static_cast<int>(regionEntry.size()) - 4;

    // LZ4 decompress
    std::vector<uint8_t> decompressed(uncompressedSize);
    int result = LZ4_decompress_safe(
        reinterpret_cast<const char*>(regionEntry.data() + 4),
        reinterpret_cast<char*>(decompressed.data()),
        compressedSize,
        static_cast<int>(uncompressedSize));

    if (result < 0)
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "LZ4 decompression failed (corrupt data)"});
    }

    return deserializeColumn(decompressed, chunkCoord, registry);
}

} // namespace voxel::world
