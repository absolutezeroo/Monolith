#include "voxel/world/RegionFile.h"

#include "voxel/core/Assert.h"

#include <fstream>

namespace voxel::world
{

namespace
{

/// Read the header entry (offset, size) for a given local index.
/// Uses explicit little-endian encoding for cross-platform portability.
struct HeaderEntry
{
    uint32_t offset = 0;
    uint32_t size = 0;
};

uint32_t readLE32(std::fstream& file)
{
    uint8_t buf[4];
    file.read(reinterpret_cast<char*>(buf), 4);
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
           (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}

void writeLE32(std::fstream& file, uint32_t v)
{
    uint8_t buf[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)};
    file.write(reinterpret_cast<const char*>(buf), 4);
}

HeaderEntry readHeaderEntry(std::fstream& file, int localIndex)
{
    file.seekg(static_cast<std::streamoff>(localIndex) * 8);
    HeaderEntry entry;
    entry.offset = readLE32(file);
    entry.size = readLE32(file);
    return entry;
}

void writeHeaderEntry(std::fstream& file, int localIndex, uint32_t offset, uint32_t size)
{
    file.seekp(static_cast<std::streamoff>(localIndex) * 8);
    writeLE32(file, offset);
    writeLE32(file, size);
}

/// Create a new region file with an empty header (8192 zero bytes).
bool createEmptyRegionFile(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }
    std::vector<uint8_t> emptyHeader(HEADER_BYTES, 0);
    file.write(reinterpret_cast<const char*>(emptyHeader.data()), HEADER_BYTES);
    return file.good();
}

} // namespace

core::Result<void> RegionFile::writeChunk(
    const std::filesystem::path& regionPath,
    int localIndex,
    std::span<const uint8_t> data)
{
    VX_ASSERT(localIndex >= 0 && localIndex < HEADER_ENTRIES, "localIndex out of range");

    // Create file if it doesn't exist
    if (!std::filesystem::exists(regionPath))
    {
        if (!createEmptyRegionFile(regionPath))
        {
            return std::unexpected(core::EngineError{
                core::ErrorCode::InvalidFormat, "Failed to create region file: " + regionPath.string()});
        }
    }

    std::fstream file(regionPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::InvalidFormat, "Failed to open region file for writing: " + regionPath.string()});
    }

    // Seek to end to find append position
    file.seekp(0, std::ios::end);
    auto appendPos = static_cast<uint32_t>(file.tellp());

    // Write the chunk data at end of file
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file.good())
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "Failed to write chunk data to region file"});
    }

    // Update header entry
    writeHeaderEntry(file, localIndex, appendPos, static_cast<uint32_t>(data.size()));
    if (!file.good())
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "Failed to update region file header"});
    }

    return {};
}

core::Result<std::vector<uint8_t>> RegionFile::readChunk(
    const std::filesystem::path& regionPath,
    int localIndex)
{
    VX_ASSERT(localIndex >= 0 && localIndex < HEADER_ENTRIES, "localIndex out of range");

    if (!std::filesystem::exists(regionPath))
    {
        return std::unexpected(core::EngineError::file(regionPath.string()));
    }

    std::fstream file(regionPath, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        return std::unexpected(core::EngineError::file(regionPath.string()));
    }

    HeaderEntry entry = readHeaderEntry(file, localIndex);
    if (entry.offset == 0 && entry.size == 0)
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::ChunkNotLoaded,
            "Chunk not saved in region file (index " + std::to_string(localIndex) + ")"});
    }

    std::vector<uint8_t> data(entry.size);
    file.seekg(static_cast<std::streamoff>(entry.offset));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(entry.size));
    if (!file.good())
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidFormat, "Failed to read chunk data from region file"});
    }

    return data;
}

core::Result<void> RegionFile::compact(const std::filesystem::path& regionPath)
{
    if (!std::filesystem::exists(regionPath))
    {
        return std::unexpected(core::EngineError::file(regionPath.string()));
    }

    // Read the entire header from the original file
    std::fstream src(regionPath, std::ios::in | std::ios::binary);
    if (!src.is_open())
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::InvalidFormat, "Failed to open region file for compaction: " + regionPath.string()});
    }

    // Read all header entries
    HeaderEntry entries[HEADER_ENTRIES];
    for (int i = 0; i < HEADER_ENTRIES; ++i)
    {
        entries[i] = readHeaderEntry(src, i);
    }

    // Write a new temp file: fresh header + live chunks packed sequentially
    auto tempPath = regionPath;
    tempPath += ".compact_tmp";

    {
        std::ofstream dst(tempPath, std::ios::binary);
        if (!dst.is_open())
        {
            return std::unexpected(core::EngineError{
                core::ErrorCode::InvalidFormat, "Failed to create temp file for compaction"});
        }

        // Write empty header placeholder
        std::vector<uint8_t> emptyHeader(HEADER_BYTES, 0);
        dst.write(reinterpret_cast<const char*>(emptyHeader.data()), HEADER_BYTES);

        // Copy live chunks and track new offsets
        uint32_t writePos = HEADER_BYTES;
        HeaderEntry newEntries[HEADER_ENTRIES] = {};

        for (int i = 0; i < HEADER_ENTRIES; ++i)
        {
            if (entries[i].offset == 0 && entries[i].size == 0)
            {
                continue; // no chunk here
            }

            // Read chunk data from source
            std::vector<uint8_t> chunkData(entries[i].size);
            src.seekg(static_cast<std::streamoff>(entries[i].offset));
            src.read(reinterpret_cast<char*>(chunkData.data()), static_cast<std::streamsize>(entries[i].size));
            if (!src.good())
            {
                return std::unexpected(core::EngineError{
                    core::ErrorCode::InvalidFormat, "Failed to read chunk during compaction"});
            }

            // Write chunk data to dest
            dst.write(reinterpret_cast<const char*>(chunkData.data()), static_cast<std::streamsize>(chunkData.size()));
            if (!dst.good())
            {
                return std::unexpected(core::EngineError{
                    core::ErrorCode::InvalidFormat, "Failed to write chunk during compaction"});
            }

            newEntries[i].offset = writePos;
            newEntries[i].size = entries[i].size;
            writePos += entries[i].size;
        }

        // Go back and write the real header with updated offsets
        dst.seekp(0);
        for (int i = 0; i < HEADER_ENTRIES; ++i)
        {
            uint8_t buf[4];
            auto writeLE = [&](uint32_t v) {
                buf[0] = static_cast<uint8_t>(v & 0xFF);
                buf[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                buf[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
                buf[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
                dst.write(reinterpret_cast<const char*>(buf), 4);
            };
            writeLE(newEntries[i].offset);
            writeLE(newEntries[i].size);
        }

        if (!dst.good())
        {
            return std::unexpected(core::EngineError{
                core::ErrorCode::InvalidFormat, "Failed to finalize compacted region file"});
        }
    }

    // Close the source before rename
    src.close();

    // Atomically replace original with compacted file
    std::error_code ec;
    std::filesystem::rename(tempPath, regionPath, ec);
    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        return std::unexpected(core::EngineError{
            core::ErrorCode::InvalidFormat, "Failed to replace region file after compaction"});
    }

    return {};
}

} // namespace voxel::world
