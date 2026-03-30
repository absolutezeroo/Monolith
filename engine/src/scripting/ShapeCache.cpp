#include "voxel/scripting/ShapeCache.h"

#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/NeighborNotifier.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"

namespace voxel::scripting
{

ShapeCache::ShapeCache(world::BlockRegistry& registry, BlockCallbackInvoker& invoker)
    : m_registry(registry), m_invoker(invoker)
{
}

std::span<const math::AABB> ShapeCache::getCollisionShape(const glm::ivec3& pos, uint16_t blockId)
{
    auto& entry = m_cache[pos];

    if (!entry.collisionDirty)
    {
        return entry.collisionBoxes;
    }

    const auto& def = m_registry.getBlockType(blockId);

    // If no custom callback, return empty (caller uses default full-block)
    if (!def.callbacks || !def.callbacks->getCollisionShape.has_value())
    {
        entry.collisionBoxes.clear();
        entry.collisionDirty = false;
        return entry.collisionBoxes;
    }

    entry.collisionBoxes = m_invoker.invokeGetCollisionShape(def, pos);
    entry.collisionDirty = false;
    return entry.collisionBoxes;
}

std::span<const math::AABB> ShapeCache::getSelectionShape(const glm::ivec3& pos, uint16_t blockId)
{
    auto& entry = m_cache[pos];

    if (!entry.selectionDirty)
    {
        return entry.selectionBoxes;
    }

    const auto& def = m_registry.getBlockType(blockId);

    // Try selection shape callback first
    if (def.callbacks && def.callbacks->getSelectionShape.has_value())
    {
        entry.selectionBoxes = m_invoker.invokeGetSelectionShape(def, pos);
        if (!entry.selectionBoxes.empty())
        {
            entry.selectionDirty = false;
            return entry.selectionBoxes;
        }
    }

    // Fall back to collision shape
    auto collisionSpan = getCollisionShape(pos, blockId);
    if (!collisionSpan.empty())
    {
        entry.selectionBoxes.assign(collisionSpan.begin(), collisionSpan.end());
        entry.selectionDirty = false;
        return entry.selectionBoxes;
    }

    // No custom shapes — return empty (caller uses default)
    entry.selectionBoxes.clear();
    entry.selectionDirty = false;
    return entry.selectionBoxes;
}

void ShapeCache::invalidate(const glm::ivec3& pos)
{
    auto it = m_cache.find(pos);
    if (it != m_cache.end())
    {
        it->second.collisionDirty = true;
        it->second.selectionDirty = true;
    }

    // Also invalidate 6 neighbors (connected blocks may change shape)
    for (const auto& offset : NeighborNotifier::OFFSETS)
    {
        glm::ivec3 neighborPos = pos + offset;
        auto nit = m_cache.find(neighborPos);
        if (nit != m_cache.end())
        {
            nit->second.collisionDirty = true;
            nit->second.selectionDirty = true;
        }
    }
}

void ShapeCache::clear()
{
    m_cache.clear();
}

} // namespace voxel::scripting
