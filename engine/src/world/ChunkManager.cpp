#include "voxel/world/ChunkManager.h"

#include "voxel/core/Assert.h"
#include "voxel/world/Block.h"
#include "voxel/world/WorldGenerator.h"

namespace voxel::world
{

ChunkColumn* ChunkManager::getChunk(glm::ivec2 coord)
{
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

const ChunkColumn* ChunkManager::getChunk(glm::ivec2 coord) const
{
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

uint16_t ChunkManager::getBlock(const glm::ivec3& worldPos) const
{
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    const ChunkColumn* column = getChunk(chunkCoord);
    if (column == nullptr)
    {
        return BLOCK_AIR;
    }

    glm::ivec3 local = worldToLocalPos(worldPos);
    return column->getBlock(local.x, local.y, local.z);
}

void ChunkManager::setBlock(const glm::ivec3& worldPos, uint16_t id)
{
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    ChunkColumn* column = getChunk(chunkCoord);

    VX_ASSERT(column != nullptr, "setBlock called on unloaded chunk — load chunk first");
    if (column == nullptr)
    {
        return;
    }

    glm::ivec3 local = worldToLocalPos(worldPos);
    column->setBlock(local.x, local.y, local.z, id);
}

void ChunkManager::loadChunk(glm::ivec2 coord)
{
    auto [it, inserted] = m_chunks.try_emplace(coord, nullptr);
    if (inserted)
    {
        if (m_worldGen != nullptr)
        {
            it->second = std::make_unique<ChunkColumn>(m_worldGen->generateChunkColumn(coord));
        }
        else
        {
            it->second = std::make_unique<ChunkColumn>(coord);
        }
    }
}

void ChunkManager::unloadChunk(glm::ivec2 coord)
{
    m_chunks.erase(coord);
}

size_t ChunkManager::loadedChunkCount() const
{
    return m_chunks.size();
}

size_t ChunkManager::dirtyChunkCount() const
{
    size_t count = 0;
    for (const auto& [coord, column] : m_chunks)
    {
        for (int s = 0; s < ChunkColumn::SECTIONS_PER_COLUMN; ++s)
        {
            if (column->isSectionDirty(s))
            {
                ++count;
                break;
            }
        }
    }
    return count;
}

} // namespace voxel::world
