#pragma once

#include "voxel/core/Result.h"
#include "voxel/world/ChunkColumn.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace voxel::world
{

class BlockRegistry;

/// Serializes and deserializes ChunkColumns to/from region files on disk.
/// Uses palette compression with string-based block IDs for cross-session persistence,
/// and LZ4 compression for compact storage.
class ChunkSerializer
{
public:
    /// Save a chunk column to the appropriate region file.
    /// Creates the region file and parent directories if they don't exist.
    [[nodiscard]] static core::Result<void> save(
        const ChunkColumn& column,
        const BlockRegistry& registry,
        const std::filesystem::path& regionDir
    );

    /// Load a chunk column from its region file.
    /// Returns FileNotFound if region file missing, ChunkNotLoaded if chunk not saved.
    [[nodiscard]] static core::Result<ChunkColumn> load(
        glm::ivec2 chunkCoord,
        const BlockRegistry& registry,
        const std::filesystem::path& regionDir
    );

    /// Serialize a ChunkColumn to binary (pre-LZ4).
    /// Palette-compresses each non-null section and writes string IDs for persistence.
    [[nodiscard]] static std::vector<uint8_t> serializeColumn(
        const ChunkColumn& column,
        const BlockRegistry& registry
    );

    /// Deserialize a ChunkColumn from binary (post-LZ4 decompression).
    /// Resolves string IDs back to current session numeric IDs.
    [[nodiscard]] static core::Result<ChunkColumn> deserializeColumn(
        std::span<const uint8_t> data,
        glm::ivec2 chunkCoord,
        const BlockRegistry& registry
    );
};

} // namespace voxel::world
