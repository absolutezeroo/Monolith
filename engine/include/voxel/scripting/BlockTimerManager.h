#pragma once

#include "voxel/math/MathTypes.h"

#include <glm/vec3.hpp>

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{
class BlockCallbackInvoker;

struct TimerEntry
{
    float remaining = 0.0f; // seconds until fire
    float interval = 0.0f;  // original interval (for restart on true return)
    uint16_t blockId = 0;   // block ID at the time timer was set
};

/// Manages per-block timers that fire on_timer callbacks when they expire.
/// Main-thread only — no synchronization needed.
class BlockTimerManager
{
public:
    explicit BlockTimerManager(world::ChunkManager& chunks);

    /// Start or replace a timer on the block at pos. Timer fires on_timer after seconds elapse.
    void setTimer(const glm::ivec3& pos, float seconds);

    /// Returns remaining seconds on the timer at pos, or nullopt if no timer.
    [[nodiscard]] std::optional<float> getTimer(const glm::ivec3& pos) const;

    /// Cancel the timer at pos. No-op if no timer exists.
    void cancelTimer(const glm::ivec3& pos);

    /// Called when a block is broken or overwritten. Idempotent — no-op if no timer.
    void onBlockRemoved(const glm::ivec3& pos);

    /// Tick all active timers. Fires on_timer callbacks for expired timers.
    void update(float dt, world::BlockRegistry& registry, BlockCallbackInvoker& invoker);

    [[nodiscard]] size_t activeTimerCount() const { return m_timers.size(); }

private:
    world::ChunkManager& m_chunkManager;
    std::unordered_map<glm::ivec3, TimerEntry, math::IVec3Hash> m_timers;

    // Pending timers set during update iteration (from inside on_timer callbacks)
    std::vector<std::pair<glm::ivec3, TimerEntry>> m_pendingTimers;
    bool m_insideUpdate = false;

    // TODO(9.4): Persist timer state in chunk serialization.
    // Timer data (pos -> remaining, interval) needs to be saved/loaded alongside block data.
    // Deferred until save/load is next touched — runtime-only timers are sufficient for V1.
};

} // namespace voxel::scripting
