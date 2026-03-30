#include "voxel/scripting/ABMRegistry.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

namespace voxel::scripting
{

ABMRegistry::ABMRegistry()
    : m_rng(std::random_device{}())
{
}

void ABMRegistry::registerABM(ABMDefinition def)
{
    m_accumulators.push_back(0.0f);
    m_abms.push_back(std::move(def));
}

void ABMRegistry::update(
    float dt,
    world::ChunkManager& chunks,
    world::BlockRegistry& /*registry*/,
    BlockCallbackInvoker& invoker)
{
    if (m_abms.empty())
    {
        return;
    }

    // 1. Increment all accumulators
    for (size_t i = 0; i < m_accumulators.size(); ++i)
    {
        m_accumulators[i] += dt;
    }

    // 2. If no scan in progress, check if any ABMs are due
    if (!m_scanInProgress)
    {
        m_dueABMs.clear();
        for (size_t i = 0; i < m_abms.size(); ++i)
        {
            if (m_accumulators[i] >= m_abms[i].interval)
            {
                m_dueABMs.push_back(i);
            }
        }
        if (m_dueABMs.empty())
        {
            return;
        }

        // Start new scan: snapshot chunk list
        m_chunkSnapshot = chunks.getLoadedChunkCoords();
        m_chunkCursor = 0;
        m_sectionCursor = 0;
        m_blockCursor = 0;
        m_scanInProgress = true;
    }

    // 3. Process up to MAX_ABM_BLOCKS_PER_TICK blocks
    int blocksProcessed = 0;
    while (blocksProcessed < MAX_ABM_BLOCKS_PER_TICK && m_chunkCursor < m_chunkSnapshot.size())
    {
        const glm::ivec2& chunkCoord = m_chunkSnapshot[m_chunkCursor];
        world::ChunkColumn* column = chunks.getChunkColumn(chunkCoord);
        if (column == nullptr)
        {
            // Chunk unloaded since snapshot — skip
            m_chunkCursor++;
            m_sectionCursor = 0;
            m_blockCursor = 0;
            continue;
        }

        while (m_sectionCursor < world::ChunkColumn::SECTIONS_PER_COLUMN
               && blocksProcessed < MAX_ABM_BLOCKS_PER_TICK)
        {
            const world::ChunkSection* section = column->getSection(m_sectionCursor);
            if (section == nullptr || section->isEmpty())
            {
                m_sectionCursor++;
                m_blockCursor = 0;
                continue;
            }

            while (m_blockCursor < world::ChunkSection::VOLUME && blocksProcessed < MAX_ABM_BLOCKS_PER_TICK)
            {
                int localY = m_blockCursor / 256;
                int localZ = (m_blockCursor % 256) / 16;
                int localX = m_blockCursor % 16;
                uint16_t blockId = section->getBlock(localX, localY, localZ);

                if (blockId != 0) // Skip air
                {
                    glm::ivec3 worldPos{
                        chunkCoord.x * 16 + localX,
                        m_sectionCursor * 16 + localY,
                        chunkCoord.y * 16 + localZ};

                    for (size_t abmIdx : m_dueABMs)
                    {
                        const auto& abm = m_abms[abmIdx];
                        if (abm.resolvedNodenames.contains(blockId))
                        {
                            // Check neighbor requirement
                            if (abm.hasNeighborRequirement && !checkNeighborRequirement(abm, worldPos, chunks))
                            {
                                continue;
                            }

                            // Roll chance
                            if (abm.chance > 1)
                            {
                                std::uniform_int_distribution<int> dist(0, abm.chance - 1);
                                if (dist(m_rng) != 0)
                                {
                                    continue;
                                }
                            }

                            // Fire callback
                            invoker.invokeABMAction(abm.action, worldPos, blockId, 0);
                        }
                    }
                }

                m_blockCursor++;
                blocksProcessed++;
            }

            if (m_blockCursor >= world::ChunkSection::VOLUME)
            {
                m_sectionCursor++;
                m_blockCursor = 0;
            }
        }

        if (m_sectionCursor >= world::ChunkColumn::SECTIONS_PER_COLUMN)
        {
            m_chunkCursor++;
            m_sectionCursor = 0;
            m_blockCursor = 0;
        }
    }

    // 4. Check if scan completed
    if (m_chunkCursor >= m_chunkSnapshot.size())
    {
        // Reset accumulators for due ABMs
        for (size_t i : m_dueABMs)
        {
            m_accumulators[i] = 0.0f;
        }
        m_scanInProgress = false;
    }
}

bool ABMRegistry::checkNeighborRequirement(
    const ABMDefinition& abm,
    const glm::ivec3& worldPos,
    world::ChunkManager& chunks) const
{
    static constexpr glm::ivec3 OFFSETS[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    for (const auto& offset : OFFSETS)
    {
        uint16_t neighborId = chunks.getBlock(worldPos + offset);
        if (abm.resolvedNeighbors.contains(neighborId))
        {
            return true;
        }
    }
    return false;
}

} // namespace voxel::scripting
