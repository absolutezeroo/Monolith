#pragma once

#include <cstdint>
#include <vector>

namespace voxel::world
{

class BlockRegistry;
class ChunkColumn;
class ChunkManager;
struct BlockDefinition;

/// Node for BFS light propagation/removal queues.
struct LightRemovalNode
{
    int16_t x;     // World X
    int16_t y;     // World Y [0..255]
    int16_t z;     // World Z
    uint8_t light; // Light value at this position before removal
};

/// Handles dynamic light updates when blocks are placed or broken.
/// All methods are stateless (static). Runs synchronously on the main thread.
class DynamicLightUpdater
{
  public:
    /// Update lighting after an opaque/emissive block is broken (solid → air).
    /// Handles block light recovery, sky light recovery, and heightmap maintenance.
    static void onBlockBroken(
        ChunkColumn& column,
        int localX,
        int worldY,
        int localZ,
        const BlockDefinition& oldBlock,
        ChunkManager& manager,
        const BlockRegistry& registry);

    /// Update lighting after a block is placed (air → solid/emissive).
    /// Handles light removal, emissive block seeding, and heightmap maintenance.
    static void onBlockPlaced(
        ChunkColumn& column,
        int localX,
        int worldY,
        int localZ,
        const BlockDefinition& newBlock,
        ChunkManager& manager,
        const BlockRegistry& registry);

  private:
    /// Reverse-BFS that zeroes block light from an origin outward.
    /// Collects boundary sources into reseedQueue for re-propagation.
    static void floodRemoveBlockLight(
        ChunkColumn& column,
        int localX,
        int worldY,
        int localZ,
        uint8_t startLight,
        ChunkManager& manager,
        const BlockRegistry& registry);

    /// Reverse-BFS that zeroes sky light from an origin outward.
    /// Collects boundary sources into reseedQueue for re-propagation.
    static void floodRemoveSkyLight(
        ChunkColumn& column,
        int localX,
        int worldY,
        int localZ,
        uint8_t startLight,
        ChunkManager& manager,
        const BlockRegistry& registry);

    /// BFS re-propagation of block light from seed positions.
    static void reseedBlockLight(
        std::vector<LightRemovalNode>& seeds,
        ChunkManager& manager,
        const BlockRegistry& registry);

    /// BFS re-propagation of sky light from seed positions.
    static void reseedSkyLight(
        std::vector<LightRemovalNode>& seeds,
        ChunkManager& manager,
        const BlockRegistry& registry);
};

} // namespace voxel::world
