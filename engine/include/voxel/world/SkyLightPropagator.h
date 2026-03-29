#pragma once

namespace voxel::world
{

class BlockRegistry;
class ChunkColumn;
class ChunkManager;

/// Propagates sky light through BFS. All methods are stateless (static).
class SkyLightPropagator
{
  public:
    /// Seed sky light from above and BFS-propagate horizontally/downward within the column.
    /// Call after BlockLightPropagator::propagateColumn(), before marking sections dirty.
    /// Requires heightmap to be built first (ChunkColumn::buildHeightMap()).
    static void propagateColumn(ChunkColumn& column, const BlockRegistry& registry);

    /// Check border blocks and push/pull sky light across loaded neighbor columns.
    /// Must be called after propagateColumn(). Marks affected neighbor sections dirty.
    static void propagateBorders(ChunkColumn& column, ChunkManager& manager, const BlockRegistry& registry);
};

} // namespace voxel::world
