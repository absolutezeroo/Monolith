#include "voxel/world/ChunkSerializer.h"

#include "BinaryIO.h"
#include "voxel/core/Log.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/PaletteCompression.h"
#include "voxel/world/RegionFile.h"

#include <lz4.h>

#include <filesystem>
#include <string>

namespace voxel::world
{

// Magic bytes for metadata/inventory sections
static constexpr uint16_t MAGIC_METADATA = 0x4D44;  // "MD"
static constexpr uint16_t MAGIC_INVENTORY = 0x4956;  // "IV"

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

    // --- Metadata section ---
    const auto& metaMap = column.allMetadata();
    writer.writeU16(MAGIC_METADATA);
    writer.writeU16(static_cast<uint16_t>(metaMap.size()));
    for (const auto& [packedIdx, meta] : metaMap)
    {
        writer.writeU16(packedIdx);
        meta.serialize(writer);
    }

    // --- Inventory section ---
    const auto& invMap = column.allInventories();
    writer.writeU16(MAGIC_INVENTORY);
    writer.writeU16(static_cast<uint16_t>(invMap.size()));
    for (const auto& [packedIdx, inv] : invMap)
    {
        writer.writeU16(packedIdx);
        inv.serialize(writer);
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

        // Copy into the column — default copy assignment transfers both blocks[]
        // and m_nonAirCount (correctly maintained by PaletteCompression::decompress).
        column.getOrCreateSection(i) = decompressed;
    }

    // --- Metadata section (backward compatible: old files may not have this) ---
    if (reader.hasRemaining(2))
    {
        auto magicResult = reader.readU16();
        if (magicResult.has_value() && magicResult.value() == MAGIC_METADATA)
        {
            auto metaCountResult = reader.readU16();
            if (!metaCountResult.has_value())
            {
                return std::unexpected(metaCountResult.error());
            }
            uint16_t metaCount = metaCountResult.value();

            for (uint16_t m = 0; m < metaCount; ++m)
            {
                auto idxResult = reader.readU16();
                if (!idxResult.has_value())
                {
                    return std::unexpected(idxResult.error());
                }
                auto metaResult = BlockMetadata::deserialize(reader);
                if (!metaResult.has_value())
                {
                    return std::unexpected(metaResult.error());
                }
                // Unpack index back to local coords for getOrCreateMetadata
                uint16_t packed = idxResult.value();
                int x = packed % ChunkSection::SIZE;
                int z = (packed / ChunkSection::SIZE) % ChunkSection::SIZE;
                int y = packed / (ChunkSection::SIZE * ChunkSection::SIZE);
                column.getOrCreateMetadata(x, y, z) = std::move(metaResult.value());
            }
        }
    }

    // --- Inventory section (backward compatible) ---
    if (reader.hasRemaining(2))
    {
        auto magicResult = reader.readU16();
        if (magicResult.has_value() && magicResult.value() == MAGIC_INVENTORY)
        {
            auto invCountResult = reader.readU16();
            if (!invCountResult.has_value())
            {
                return std::unexpected(invCountResult.error());
            }
            uint16_t invCount = invCountResult.value();

            for (uint16_t v = 0; v < invCount; ++v)
            {
                auto idxResult = reader.readU16();
                if (!idxResult.has_value())
                {
                    return std::unexpected(idxResult.error());
                }
                auto invResult = BlockInventory::deserialize(reader);
                if (!invResult.has_value())
                {
                    return std::unexpected(invResult.error());
                }
                uint16_t packed = idxResult.value();
                int x = packed % ChunkSection::SIZE;
                int z = (packed / ChunkSection::SIZE) % ChunkSection::SIZE;
                int y = packed / (ChunkSection::SIZE * ChunkSection::SIZE);
                column.getOrCreateInventory(x, y, z) = std::move(invResult.value());
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

    // Sanity cap: a ChunkColumn with 16 fully-filled sections serializes well under 1 MB.
    constexpr uint32_t MAX_UNCOMPRESSED_SIZE = 4 * 1024 * 1024; // 4 MB
    if (uncompressedSize > MAX_UNCOMPRESSED_SIZE)
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::InvalidFormat,
            "Uncompressed size exceeds sanity limit (" + std::to_string(uncompressedSize) + " bytes)"});
    }

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
