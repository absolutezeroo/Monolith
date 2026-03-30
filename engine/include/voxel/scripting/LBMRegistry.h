#pragma once

#include "voxel/core/Types.h"
#include "voxel/world/ChunkManager.h"

#include <sol/sol.hpp>

#include <glm/vec2.hpp>

#include <string>
#include <unordered_map>
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

struct LBMDefinition
{
    std::string label;
    std::vector<std::string> nodenames;
    bool runAtEveryLoad = false;
    sol::protected_function action; // (pos, node, dtime_s)

    // Pre-resolved:
    std::unordered_set<uint16_t> resolvedNodenames;
};

/// Manages Loading Block Modifiers — callbacks fired when chunks are loaded.
/// LBMs scan newly loaded chunks for matching blocks and fire actions.
class LBMRegistry
{
public:
    void registerLBM(LBMDefinition def);

    /// Called when a chunk finishes loading. Scans for matching blocks and fires LBM actions.
    void onChunkLoaded(
        const glm::ivec2& coord,
        world::ChunkManager& chunks,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker);

    [[nodiscard]] size_t lbmCount() const { return m_lbms.size(); }

private:
    std::vector<LBMDefinition> m_lbms;

    // Track which non-repeating LBMs have already run for each chunk
    std::unordered_map<glm::ivec2, std::unordered_set<std::string>, world::ChunkCoordHash> m_executedLBMs;

    // TODO(9.4): Persist executed LBM labels per chunk in serialization format.
    // Currently runtime-only — all LBMs re-fire on restart.
};

} // namespace voxel::scripting
