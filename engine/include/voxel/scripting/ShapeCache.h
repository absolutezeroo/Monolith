#pragma once

#include "voxel/math/AABB.h"
#include "voxel/math/MathTypes.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace voxel::world
{
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{

class BlockCallbackInvoker;

/// Cached collision and selection shapes for a single block position.
struct CachedShape
{
    std::vector<math::AABB> collisionBoxes;
    std::vector<math::AABB> selectionBoxes;
    bool collisionDirty = true;
    bool selectionDirty = true;
};

/// Caches per-position collision and selection shapes returned by Lua callbacks.
/// Shapes are in block-local coordinates (0,0,0 to 1,1,1). The consumer must
/// offset them to world coordinates: worldBox = localBox + vec3(blockPos).
class ShapeCache
{
public:
    ShapeCache(world::BlockRegistry& registry, BlockCallbackInvoker& invoker);

    /// Get collision boxes for block at pos. Queries Lua callback if dirty/missing.
    /// Returns empty span if block has no custom shape (caller should use default).
    std::span<const math::AABB> getCollisionShape(const glm::ivec3& pos, uint16_t blockId);

    /// Get selection boxes for block at pos. Falls back to collision shape, then default.
    std::span<const math::AABB> getSelectionShape(const glm::ivec3& pos, uint16_t blockId);

    /// Invalidate cached shape at pos and its 6 neighbors.
    void invalidate(const glm::ivec3& pos);

    /// Invalidate all entries (e.g., on hot-reload).
    void clear();

private:
    world::BlockRegistry& m_registry;
    BlockCallbackInvoker& m_invoker;
    std::unordered_map<glm::ivec3, CachedShape, math::IVec3Hash> m_cache;
};

} // namespace voxel::scripting
