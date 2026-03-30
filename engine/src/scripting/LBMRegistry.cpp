#include "voxel/scripting/LBMRegistry.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/ChunkSection.h"

namespace voxel::scripting
{

void LBMRegistry::registerLBM(LBMDefinition def)
{
    m_lbms.push_back(std::move(def));
}

void LBMRegistry::onChunkLoaded(
    const glm::ivec2& coord,
    world::ChunkManager& chunks,
    world::BlockRegistry& /*registry*/,
    BlockCallbackInvoker& invoker)
{
    if (m_lbms.empty())
    {
        return;
    }

    world::ChunkColumn* column = chunks.getChunkColumn(coord);
    if (column == nullptr)
    {
        return;
    }

    for (auto& lbm : m_lbms)
    {
        // Skip non-repeating LBMs that already ran for this chunk
        if (!lbm.runAtEveryLoad)
        {
            auto it = m_executedLBMs.find(coord);
            if (it != m_executedLBMs.end() && it->second.contains(lbm.label))
            {
                continue;
            }
        }

        bool fired = false;
        for (int sY = 0; sY < world::ChunkColumn::SECTIONS_PER_COLUMN; ++sY)
        {
            const world::ChunkSection* section = column->getSection(sY);
            if (section == nullptr || section->isEmpty())
            {
                continue;
            }

            for (int y = 0; y < 16; ++y)
            {
                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        uint16_t blockId = section->getBlock(x, y, z);
                        if (lbm.resolvedNodenames.contains(blockId))
                        {
                            glm::ivec3 worldPos{coord.x * 16 + x, sY * 16 + y, coord.y * 16 + z};
                            invoker.invokeLBMAction(lbm.action, worldPos, blockId, 0.0f);
                            fired = true;
                        }
                    }
                }
            }
        }

        // Mark non-repeating LBM as executed for this chunk
        if (fired && !lbm.runAtEveryLoad)
        {
            m_executedLBMs[coord].insert(lbm.label);
        }
    }
}

} // namespace voxel::scripting
