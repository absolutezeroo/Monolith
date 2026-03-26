#pragma once

#include "voxel/world/ChunkSection.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <memory>

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
    void clearDirty(int sectionY);

    // Queries
    [[nodiscard]] bool isAllEmpty() const;
    [[nodiscard]] int getHighestNonEmptySection() const;

  private:
    glm::ivec2 m_chunkCoord;
    std::array<std::unique_ptr<ChunkSection>, SECTIONS_PER_COLUMN> m_sections;
    std::array<bool, SECTIONS_PER_COLUMN> m_dirty;
};

} // namespace voxel::world
