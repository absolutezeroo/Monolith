#pragma once

#include "voxel/core/Types.h"

#include <sol/sol.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{
class BlockCallbackInvoker;

struct ABMDefinition
{
    std::string label;
    std::vector<std::string> nodenames;  // original string IDs
    std::vector<std::string> neighbors;  // optional neighbor requirements
    float interval = 1.0f;              // seconds between scans
    int chance = 1;                     // 1/chance probability
    sol::protected_function action;     // (pos, node, active_object_count)

    // Pre-resolved at registration time:
    std::unordered_set<uint16_t> resolvedNodenames;
    std::unordered_set<uint16_t> resolvedNeighbors;
    bool hasNeighborRequirement = false;
};

/// Manages Active Block Modifiers — area-based random block updates.
/// ABMs scan loaded chunks and fire callbacks for matching block types.
/// Scans are spread across multiple ticks to avoid frame spikes.
class ABMRegistry
{
public:
    static constexpr int MAX_ABM_BLOCKS_PER_TICK = 4096;

    ABMRegistry();

    void registerABM(ABMDefinition def);

    void update(
        float dt,
        world::ChunkManager& chunks,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker);

    [[nodiscard]] size_t abmCount() const { return m_abms.size(); }

private:
    bool checkNeighborRequirement(
        const ABMDefinition& abm,
        const glm::ivec3& worldPos,
        world::ChunkManager& chunks) const;

    std::vector<ABMDefinition> m_abms;
    std::vector<float> m_accumulators; // per-ABM time since last full scan

    // Scan cursor (persists across ticks)
    std::vector<glm::ivec2> m_chunkSnapshot; // snapshot of loaded chunks at scan start
    size_t m_chunkCursor = 0;
    int m_sectionCursor = 0;
    int m_blockCursor = 0;
    bool m_scanInProgress = false;
    std::vector<size_t> m_dueABMs; // indices of ABMs due this scan cycle

    std::mt19937 m_rng;
};

} // namespace voxel::scripting
