#pragma once

#include "voxel/core/Result.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace voxel::world
{

/// Constants for the region file format.
/// Each region covers a 32x32 grid of chunk columns.
constexpr int32_t REGION_SIZE = 32;
constexpr int32_t HEADER_ENTRIES = REGION_SIZE * REGION_SIZE; // 1024
constexpr int32_t HEADER_BYTES = HEADER_ENTRIES * 8;         // 8192

/// Low-level random-access I/O for a single region file.
/// Format: 8192-byte header (1024 × [uint32_t offset, uint32_t size]),
/// followed by concatenated chunk data blobs.
class RegionFile
{
public:
    /// Write chunk data at the given local index (0..1023).
    /// Creates the region file if it doesn't exist.
    /// Appends data at end of file and updates the header entry.
    [[nodiscard]] static core::Result<void> writeChunk(
        const std::filesystem::path& regionPath,
        int localIndex,
        std::span<const uint8_t> data
    );

    /// Read chunk data at the given local index (0..1023).
    /// Returns FileNotFound if region file doesn't exist.
    /// Returns ChunkNotLoaded if chunk has no entry (offset=0, size=0).
    [[nodiscard]] static core::Result<std::vector<uint8_t>> readChunk(
        const std::filesystem::path& regionPath,
        int localIndex
    );

    /// Rewrite the region file with only live chunk data, eliminating dead space
    /// from previous overwrites. Replaces the original file atomically via rename.
    [[nodiscard]] static core::Result<void> compact(const std::filesystem::path& regionPath);
};

} // namespace voxel::world
