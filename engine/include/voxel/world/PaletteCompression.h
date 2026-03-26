#pragma once

#include "voxel/world/ChunkSection.h"

#include <cstdint>
#include <vector>

namespace voxel::world
{

struct CompressedSection
{
    std::vector<uint16_t> palette; // localIndex -> globalBlockId
    std::vector<uint64_t> data;   // packed bit entries
    uint8_t bitsPerEntry = 0;     // 0, 1, 2, 4, 8, or 16

    /// Returns actual memory footprint in bytes (palette + data + bitsPerEntry field).
    [[nodiscard]] size_t memoryUsage() const;
};

class PaletteCompression
{
public:
    [[nodiscard]] static CompressedSection compress(const ChunkSection& section);
    [[nodiscard]] static ChunkSection decompress(const CompressedSection& compressed);
};

} // namespace voxel::world
