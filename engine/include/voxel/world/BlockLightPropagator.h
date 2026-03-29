#pragma once

namespace voxel::world
{

class BlockRegistry;
class ChunkColumn;
class ChunkManager;

/// Propagates block light through BFS. All methods are stateless (static).
class BlockLightPropagator
{
  public:
    /// Seed all light-emitting blocks in a column and BFS-propagate within the column.
    /// Call after world generation, before marking sections dirty.
    static void propagateColumn(ChunkColumn& column, const BlockRegistry& registry);

    /// Check border blocks and push light into loaded neighbor columns.
    /// Must be called after propagateColumn(). Marks affected neighbor sections dirty.
    static void propagateBorders(ChunkColumn& column, ChunkManager& manager, const BlockRegistry& registry);
};

} // namespace voxel::world
