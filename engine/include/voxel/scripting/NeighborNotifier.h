#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <string>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{

class BlockCallbackInvoker;
class GlobalEventRegistry;

/// Notifies adjacent blocks when a block changes, handling cascade destruction
/// (e.g., torches losing support) with a depth limit to prevent infinite recursion.
class NeighborNotifier
{
public:
    NeighborNotifier(
        world::ChunkManager& chunks,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker);

    /// Notify all 6 neighbors that the block at `changedPos` has changed.
    /// Called AFTER setBlock + lighting update.
    void notifyNeighbors(const glm::ivec3& changedPos);

    static constexpr int MAX_CASCADE_DEPTH = 64;

    static constexpr std::array<glm::ivec3, 6> OFFSETS = {{
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
    }};

    static const std::array<std::string, 6>& directionNames();

    /// Inject GlobalEventRegistry for block_neighbor_changed events (optional, nullable).
    void setGlobalEvents(GlobalEventRegistry* registry) { m_globalEvents = registry; }

private:
    void notifySingleNeighbor(
        const glm::ivec3& neighborPos,
        const glm::ivec3& changedPos,
        int currentDepth);

    void digBlock(const glm::ivec3& pos, int currentDepth);

    world::ChunkManager& m_chunkManager;
    world::BlockRegistry& m_registry;
    BlockCallbackInvoker& m_invoker;
    GlobalEventRegistry* m_globalEvents = nullptr;
};

} // namespace voxel::scripting
