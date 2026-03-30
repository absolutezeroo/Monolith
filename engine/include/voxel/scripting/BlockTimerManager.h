#pragma once

#include "voxel/math/MathTypes.h"

#include <glm/vec3.hpp>

#include <cstdint>
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

    /// Schedule a tick-based timer (fires after delayTicks game ticks).
    /// Lower priority values execute first at the same target tick.
    void scheduleTick(const glm::ivec3& pos, int delayTicks, int priority = 0);

    /// Process scheduled ticks for the current game tick.
    /// @param currentTick The absolute game tick number.
    void updateScheduledTicks(uint64_t currentTick, world::BlockRegistry& registry, BlockCallbackInvoker& invoker);

    /// Pause or resume the wall-clock timer at pos without resetting it.
    void setTimerActive(const glm::ivec3& pos, bool active);

    [[nodiscard]] size_t activeTimerCount() const { return m_timers.size(); }
    [[nodiscard]] size_t scheduledTickCount() const { return m_scheduledTicks.size(); }

private:
    world::ChunkManager& m_chunkManager;
    std::unordered_map<glm::ivec3, TimerEntry, math::IVec3Hash> m_timers;

    // Pending timers set during update iteration (from inside on_timer callbacks)
    std::vector<std::pair<glm::ivec3, TimerEntry>> m_pendingTimers;
    bool m_insideUpdate = false;

    // Paused timers (setTimerActive(pos, false) moves entry here; true moves back)
    std::unordered_map<glm::ivec3, TimerEntry, math::IVec3Hash> m_pausedTimers;

    // Scheduled tick-based timers (fire on exact game ticks)
    struct ScheduledTick
    {
        glm::ivec3 pos;
        uint64_t targetTick = 0;
        int priority = 0;
        uint16_t blockId = 0;

        /// Comparison for min-heap: lower targetTick first, then lower priority first.
        bool operator>(const ScheduledTick& other) const
        {
            if (targetTick != other.targetTick)
            {
                return targetTick > other.targetTick;
            }
            return priority > other.priority;
        }
    };
    std::vector<ScheduledTick> m_scheduledTicks; // kept as a min-heap

    // Absolute game tick counter (incremented by caller)
    uint64_t m_currentTick = 0;

    // TODO(9.4): Persist timer state in chunk serialization.
    // Timer data (pos -> remaining, interval) needs to be saved/loaded alongside block data.
    // Deferred until save/load is next touched — runtime-only timers are sufficient for V1.
};

} // namespace voxel::scripting
