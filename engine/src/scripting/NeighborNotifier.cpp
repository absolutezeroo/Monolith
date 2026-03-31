#include "voxel/scripting/NeighborNotifier.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/GlobalEventRegistry.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/ChunkManager.h"

#include <sol/sol.hpp>

namespace voxel::scripting
{

NeighborNotifier::NeighborNotifier(
    world::ChunkManager& chunks,
    world::BlockRegistry& registry,
    BlockCallbackInvoker& invoker)
    : m_chunkManager(chunks), m_registry(registry), m_invoker(invoker)
{
}

const std::array<std::string, 6>& NeighborNotifier::directionNames()
{
    static const std::array<std::string, 6> names = {
        "east",  // +X
        "west",  // -X
        "up",    // +Y
        "down",  // -Y
        "south", // +Z
        "north", // -Z
    };
    return names;
}

void NeighborNotifier::notifyNeighbors(const glm::ivec3& changedPos)
{
    for (int i = 0; i < 6; ++i)
    {
        glm::ivec3 neighborPos = changedPos + OFFSETS[i];
        notifySingleNeighbor(neighborPos, changedPos, 0);
    }
}

void NeighborNotifier::notifySingleNeighbor(
    const glm::ivec3& neighborPos,
    const glm::ivec3& changedPos,
    int currentDepth)
{
    // Y-bounds check
    if (neighborPos.y < 0 || neighborPos.y >= world::ChunkColumn::COLUMN_HEIGHT)
    {
        return;
    }

    uint16_t neighborBlockId = m_chunkManager.getBlock(neighborPos);
    if (neighborBlockId == world::BLOCK_AIR)
    {
        return;
    }

    const auto& neighborDef = m_registry.getBlockType(neighborBlockId);

    // Get the string ID of the block that changed
    uint16_t changedBlockId = m_chunkManager.getBlock(changedPos);
    const auto& changedDef = m_registry.getBlockType(changedBlockId);
    const std::string& changedBlockString = changedDef.stringId;

    // Fire on_neighbor_changed
    m_invoker.invokeOnNeighborChanged(neighborDef, neighborPos, changedPos, changedBlockString);

    // Fire global block_neighbor_changed event (9.10)
    if (m_globalEvents)
    {
        m_globalEvents->fireEvent(
            "block_neighbor_changed",
            neighborPos.x, neighborPos.y, neighborPos.z,
            changedPos.x, changedPos.y, changedPos.z,
            changedBlockString);
    }

    // Fire update_shape if defined
    if (neighborDef.callbacks && neighborDef.callbacks->updateShape.has_value())
    {
        // Determine direction from neighbor to changed block
        glm::ivec3 offset = changedPos - neighborPos;
        std::string direction;
        for (int i = 0; i < 6; ++i)
        {
            if (OFFSETS[i] == offset)
            {
                direction = directionNames()[i];
                break;
            }
        }
        if (!direction.empty())
        {
            (void)m_invoker.invokeUpdateShape(neighborDef, neighborPos, direction, sol::lua_nil);
        }
    }

    // Check can_survive — if false, destroy the block
    if (!m_invoker.invokeCanSurvive(neighborDef, neighborPos))
    {
        if (currentDepth >= MAX_CASCADE_DEPTH)
        {
            VX_LOG_WARN(
                "Neighbor cascade depth limit ({}) reached at ({},{},{})",
                MAX_CASCADE_DEPTH,
                neighborPos.x,
                neighborPos.y,
                neighborPos.z);
            return;
        }
        digBlock(neighborPos, currentDepth + 1);
    }
}

void NeighborNotifier::digBlock(const glm::ivec3& pos, int currentDepth)
{
    uint16_t blockId = m_chunkManager.getBlock(pos);
    if (blockId == world::BLOCK_AIR)
    {
        return;
    }

    const auto& def = m_registry.getBlockType(blockId);

    // Fire destruction chain: onDestruct -> setBlock(AIR) -> afterDestruct
    m_invoker.invokeOnDestruct(def, pos);

    m_chunkManager.setBlock(pos, world::BLOCK_AIR);

    // Lighting update: treat as removing a block
    m_chunkManager.updateLightAfterBlockChange(pos, &def, nullptr);

    m_invoker.invokeAfterDestruct(def, pos, blockId);

    // Recursively notify neighbors of the newly-air position
    for (int i = 0; i < 6; ++i)
    {
        glm::ivec3 neighborPos = pos + OFFSETS[i];
        notifySingleNeighbor(neighborPos, pos, currentDepth);
    }
}

} // namespace voxel::scripting
