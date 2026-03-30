#pragma once

#include "voxel/world/BlockInventory.h"
#include "voxel/world/BlockMetadata.h"
#include "voxel/world/ChunkSection.h"
#include "voxel/world/LightMap.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace voxel::world
{
class BlockRegistry;
} // namespace voxel::world

namespace voxel::world
{

class ChunkColumn
{
  public:
    static constexpr int32_t SECTIONS_PER_COLUMN = 16;
    static constexpr int32_t COLUMN_HEIGHT = SECTIONS_PER_COLUMN * ChunkSection::SIZE; // 256

    explicit ChunkColumn(glm::ivec2 chunkCoord);
    ~ChunkColumn() = default;
    ChunkColumn(ChunkColumn&&) = default;
    ChunkColumn& operator=(ChunkColumn&&) = default;
    ChunkColumn(const ChunkColumn&) = delete;
    ChunkColumn& operator=(const ChunkColumn&) = delete;

    [[nodiscard]] glm::ivec2 getChunkCoord() const;

    // Section access
    [[nodiscard]] ChunkSection* getSection(int sectionY);
    [[nodiscard]] const ChunkSection* getSection(int sectionY) const;
    ChunkSection& getOrCreateSection(int sectionY);

    // Block access (y spans full column [0, 255])
    [[nodiscard]] uint16_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint16_t id);

    // Dirty tracking
    [[nodiscard]] bool isSectionDirty(int sectionY) const;
    void markDirty(int sectionY);
    void clearDirty(int sectionY);

    // Light map access (always present — value-type, 4KB per section)
    [[nodiscard]] LightMap& getLightMap(int sectionY);
    [[nodiscard]] const LightMap& getLightMap(int sectionY) const;
    void clearAllLight();

    // Heightmap (tracks highest opaque block Y per x,z column)
    [[nodiscard]] uint8_t getHeight(int x, int z) const;
    void setHeight(int x, int z, uint8_t y);
    void buildHeightMap(const BlockRegistry& registry);

    // Metadata access (per-block key-value store)
    [[nodiscard]] BlockMetadata* getMetadata(int x, int y, int z);
    [[nodiscard]] const BlockMetadata* getMetadata(int x, int y, int z) const;
    BlockMetadata& getOrCreateMetadata(int x, int y, int z);
    void removeMetadata(int x, int y, int z);

    // Inventory access (per-block named lists)
    [[nodiscard]] BlockInventory* getInventory(int x, int y, int z);
    [[nodiscard]] const BlockInventory* getInventory(int x, int y, int z) const;
    BlockInventory& getOrCreateInventory(int x, int y, int z);
    void removeInventory(int x, int y, int z);

    [[nodiscard]] bool hasBlockData(int x, int y, int z) const;

    // Raw map access for serialization
    [[nodiscard]] const std::unordered_map<uint16_t, BlockMetadata>& allMetadata() const { return m_metadata; }
    [[nodiscard]] const std::unordered_map<uint16_t, BlockInventory>& allInventories() const { return m_inventories; }

    /// Pack local coordinates into a flat index for metadata/inventory maps.
    [[nodiscard]] static uint16_t packLocalIndex(int x, int y, int z);

    // Queries
    [[nodiscard]] bool isAllEmpty() const;
    [[nodiscard]] int getHighestNonEmptySection() const;

  private:
    static constexpr int HEIGHTMAP_SIZE = ChunkSection::SIZE * ChunkSection::SIZE; // 256

    glm::ivec2 m_chunkCoord;
    std::array<std::unique_ptr<ChunkSection>, SECTIONS_PER_COLUMN> m_sections;
    std::array<LightMap, SECTIONS_PER_COLUMN> m_lightMaps;
    std::array<bool, SECTIONS_PER_COLUMN> m_dirty;
    std::array<uint8_t, HEIGHTMAP_SIZE> m_heightMap;

    // Sparse per-block data — only blocks with metadata/inventory allocate storage.
    std::unordered_map<uint16_t, BlockMetadata> m_metadata;
    std::unordered_map<uint16_t, BlockInventory> m_inventories;
};

} // namespace voxel::world
