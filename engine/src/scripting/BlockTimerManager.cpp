#include "voxel/scripting/BlockTimerManager.h"

#include "voxel/core/Log.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/world/Block.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"

#include <algorithm>

namespace voxel::scripting
{

BlockTimerManager::BlockTimerManager(world::ChunkManager& chunks)
    : m_chunkManager(chunks)
{
}

void BlockTimerManager::setTimer(const glm::ivec3& pos, float seconds)
{
    uint16_t blockId = m_chunkManager.getBlock(pos);
    TimerEntry entry{seconds, seconds, blockId};

    if (m_insideUpdate)
    {
        m_pendingTimers.emplace_back(pos, entry);
    }
    else
    {
        m_timers[pos] = entry;
    }
}

std::optional<float> BlockTimerManager::getTimer(const glm::ivec3& pos) const
{
    auto it = m_timers.find(pos);
    if (it != m_timers.end())
    {
        return it->second.remaining;
    }
    return std::nullopt;
}

void BlockTimerManager::cancelTimer(const glm::ivec3& pos)
{
    m_timers.erase(pos);
}

void BlockTimerManager::onBlockRemoved(const glm::ivec3& pos)
{
    m_timers.erase(pos);
}

void BlockTimerManager::update(float dt, world::BlockRegistry& registry, BlockCallbackInvoker& invoker)
{
    if (m_timers.empty())
    {
        return;
    }

    m_insideUpdate = true;

    std::vector<glm::ivec3> toRemove;

    for (auto& [pos, entry] : m_timers)
    {
        entry.remaining -= dt;
        if (entry.remaining <= 0.0f)
        {
            float elapsed = entry.interval + (-entry.remaining); // actual elapsed time
            const auto& def = registry.getBlockType(entry.blockId);

            bool restart = invoker.invokeOnTimer(def, pos, elapsed);
            if (restart)
            {
                entry.remaining = entry.interval; // reset timer
            }
            else
            {
                toRemove.push_back(pos);
            }
        }
    }

    for (const auto& pos : toRemove)
    {
        m_timers.erase(pos);
    }

    m_insideUpdate = false;

    // Apply pending timers set during callbacks
    for (auto& [pos, entry] : m_pendingTimers)
    {
        m_timers[pos] = std::move(entry);
    }
    m_pendingTimers.clear();
}

void BlockTimerManager::scheduleTick(const glm::ivec3& pos, int delayTicks, int priority)
{
    uint16_t blockId = m_chunkManager.getBlock(pos);
    ScheduledTick tick{pos, m_currentTick + static_cast<uint64_t>(delayTicks), priority, blockId};
    m_scheduledTicks.push_back(tick);
    std::push_heap(m_scheduledTicks.begin(), m_scheduledTicks.end(), std::greater<>{});
}

void BlockTimerManager::updateScheduledTicks(
    uint64_t currentTick, world::BlockRegistry& registry, BlockCallbackInvoker& invoker)
{
    m_currentTick = currentTick;

    while (!m_scheduledTicks.empty() && m_scheduledTicks.front().targetTick <= currentTick)
    {
        std::pop_heap(m_scheduledTicks.begin(), m_scheduledTicks.end(), std::greater<>{});
        ScheduledTick tick = m_scheduledTicks.back();
        m_scheduledTicks.pop_back();

        // Verify block still exists at position
        uint16_t currentBlockId = m_chunkManager.getBlock(tick.pos);
        if (currentBlockId == world::BLOCK_AIR)
        {
            continue;
        }

        const auto& def = registry.getBlockType(currentBlockId);
        float elapsed = static_cast<float>(currentTick - (tick.targetTick - 1)) * 0.05f; // ticks to seconds
        bool restart = invoker.invokeOnTimer(def, tick.pos, elapsed);
        if (restart)
        {
            // Re-schedule with same delay (1 tick) and priority
            scheduleTick(tick.pos, 1, tick.priority);
        }
    }
}

void BlockTimerManager::setTimerActive(const glm::ivec3& pos, bool active)
{
    if (active)
    {
        // Move from paused back to active
        auto it = m_pausedTimers.find(pos);
        if (it != m_pausedTimers.end())
        {
            m_timers[pos] = it->second;
            m_pausedTimers.erase(it);
        }
    }
    else
    {
        // Move from active to paused
        auto it = m_timers.find(pos);
        if (it != m_timers.end())
        {
            m_pausedTimers[pos] = it->second;
            m_timers.erase(it);
        }
    }
}

} // namespace voxel::scripting
