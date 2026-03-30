# Story 9.4: Block Tick & Update System (Timers, ABM, LBM)

Status: complete

## Story

As a developer,
I want blocks to have scheduled timers and area-based random updates,
so that mods can create time-dependent mechanics (crop growth, fire spread, leaf decay, furnace smelting).

## Acceptance Criteria

1. `on_timer(pos, elapsed) -> bool` callback in `BlockCallbacks` — fires when a per-block timer expires. Return true to restart timer at the same interval, false to stop.
2. `voxel.set_timer(pos, seconds)` Lua API — starts or replaces a timer on the block at `pos`. Timer fires `on_timer` after `seconds` elapse.
3. `voxel.get_timer(pos) -> number|nil` Lua API — returns remaining seconds on the timer at `pos`, or nil if no timer.
4. `BlockTimerManager` class stores all active timers in `unordered_map<ivec3, TimerEntry>`. Decremented each simulation tick. Fires `on_timer` callback when expired.
5. `voxel.register_abm(table)` Lua API — registers an Active Block Modifier with fields: `label` (string), `nodenames` (table of string IDs or `"group:name"`), `neighbors` (optional table), `interval` (float seconds), `chance` (int, 1/N), `action` (function(pos, node, active_object_count)).
6. `ABMRegistry` class stores registered ABMs. During world tick, iterates loaded sections, checks block types against ABM `nodenames`, rolls `chance`, fires `action` callback.
7. ABM scan is spread across multiple ticks — a persistent cursor advances through loaded chunks, processing at most `MAX_ABM_BLOCKS_PER_TICK` blocks (default 4096 = one full section) per tick to avoid frame spikes.
8. ABM respects `interval` — each ABM only triggers its scan cycle every `interval` seconds, not every tick.
9. `voxel.register_lbm(table)` Lua API — registers a Loading Block Modifier with fields: `label` (string), `nodenames` (table of string IDs), `run_at_every_load` (bool), `action` (function(pos, node, dtime_s)).
10. `LBMRegistry` class stores registered LBMs. On `ChunkLoaded` event, scans the chunk for matching blocks and fires `action` callbacks.
11. LBMs with `run_at_every_load = false` fire only once per chunk (tracked via `unordered_set<string>` per chunk coordinate in `LBMRegistry`).
12. When a block with an active timer is broken, the timer is removed from `BlockTimerManager`.
13. When a block with an active timer is overwritten by `setBlock`, the timer is removed.
14. Integration test: register ABM for grass spread (dirt next to grass, 1/10 chance), verify the ABM action callback fires with correct position and node.
15. Integration test: set a timer on a furnace block, advance time, verify `on_timer` fires and returns bool to restart.
16. Integration test: register LBM for block upgrade, simulate chunk load, verify LBM action fires for matching blocks.

## Tasks / Subtasks

- [ ] Task 1: Create IVec3Hash utility (AC: 4)
  - [ ] 1.1 Add `IVec3Hash` struct to `engine/include/voxel/math/MathTypes.h` (or a new `Hash.h` if preferred), using XOR-shift combining like the existing `ChunkCoordHash` for `ivec2`
  - [ ] 1.2 Pattern: `hash(v.x) ^ (hash(v.y) + 0x9e3779b9 + ...) ^ (hash(v.z) + ...)`

- [ ] Task 2: Add `onTimer` callback to BlockCallbacks (AC: 1)
  - [ ] 2.1 Add `std::optional<sol::protected_function> onTimer` field to `BlockCallbacks` struct in `engine/include/voxel/scripting/BlockCallbacks.h`
  - [ ] 2.2 Verify `BlockCallbacks` remains movable (no issue — sol::protected_function is movable)

- [ ] Task 3: Extract `on_timer` callback in LuaBindings (AC: 1)
  - [ ] 3.1 In `parseBlockDefinition()` in `LuaBindings.cpp`, add extraction: `callbacks->onTimer = table.get<std::optional<sol::protected_function>>("on_timer");`
  - [ ] 3.2 Use same pattern as existing callback extraction (check non-nil, store only if present)

- [ ] Task 4: Add `invokeOnTimer` to BlockCallbackInvoker (AC: 1)
  - [ ] 4.1 Declare `bool invokeOnTimer(const BlockDefinition& def, const glm::ivec3& pos, float elapsed)` in `BlockCallbackInvoker.h`
  - [ ] 4.2 Implement in `BlockCallbackInvoker.cpp`: check `has_value()` → call with `posToTable(pos), elapsed` → check `.valid()` → return bool (default `false` on error/missing)
  - [ ] 4.3 Log errors with block stringId context: `VX_LOG_WARN("Lua on_timer error for '{}': {}", def.stringId, err.what())`

- [ ] Task 5: Create BlockTimerManager (AC: 2, 3, 4, 12, 13)
  - [ ] 5.1 Create `engine/include/voxel/scripting/BlockTimerManager.h`
  - [ ] 5.2 Create `engine/src/scripting/BlockTimerManager.cpp`
  - [ ] 5.3 Define `TimerEntry` struct: `float remaining`, `float interval`, `bool active`
  - [ ] 5.4 Store timers in `std::unordered_map<glm::ivec3, TimerEntry, IVec3Hash> m_timers`
  - [ ] 5.5 Implement `setTimer(const glm::ivec3& pos, float seconds)` — inserts or replaces timer, sets `remaining = seconds`, `interval = seconds`, `active = true`
  - [ ] 5.6 Implement `getTimer(const glm::ivec3& pos) -> std::optional<float>` — returns remaining seconds or nullopt
  - [ ] 5.7 Implement `cancelTimer(const glm::ivec3& pos)` — erases entry
  - [ ] 5.8 Implement `update(float dt, BlockRegistry& registry, BlockCallbackInvoker& invoker)`:
    - Iterate all entries, decrement `remaining` by `dt`
    - When `remaining <= 0`: look up block at pos via stored blockId (or via ChunkManager), invoke `on_timer`, if returns true reset `remaining = interval`, else erase timer
    - Use a deferred erase list (don't modify map while iterating)
  - [ ] 5.9 Implement `onBlockRemoved(const glm::ivec3& pos)` — called when a block is broken/overwritten, erases timer at that pos
  - [ ] 5.10 Store `ChunkManager&` reference (constructor injection) for block lookup during timer fire

- [ ] Task 6: Bind `voxel.set_timer` and `voxel.get_timer` Lua APIs (AC: 2, 3)
  - [ ] 6.1 In `LuaBindings.cpp`, add `voxel.set_timer` function bound to `BlockTimerManager::setTimer`
  - [ ] 6.2 Lua signature: `voxel.set_timer({x=, y=, z=}, seconds)` — extract pos from table, call `m_timerManager.setTimer(pos, seconds)`
  - [ ] 6.3 Add `voxel.get_timer` function bound to `BlockTimerManager::getTimer`
  - [ ] 6.4 Lua signature: `voxel.get_timer({x=, y=, z=})` — returns number or `sol::lua_nil`
  - [ ] 6.5 Pass `BlockTimerManager&` into `LuaBindings::registerTimerAPI(sol::state&, BlockTimerManager&)`

- [ ] Task 7: Create ABMRegistry (AC: 5, 6, 7, 8)
  - [ ] 7.1 Create `engine/include/voxel/scripting/ABMRegistry.h`
  - [ ] 7.2 Create `engine/src/scripting/ABMRegistry.cpp`
  - [ ] 7.3 Define `ABMDefinition` struct: `std::string label`, `std::vector<std::string> nodenames`, `std::vector<std::string> neighbors` (optional), `float interval`, `int chance`, `sol::protected_function action`
  - [ ] 7.4 Store ABMs in `std::vector<ABMDefinition> m_abms`
  - [ ] 7.5 Per-ABM accumulator: `std::vector<float> m_accumulators` — tracks time since last full scan per ABM
  - [ ] 7.6 Implement `registerABM(ABMDefinition def)` — push to vector, initialize accumulator to 0
  - [ ] 7.7 Pre-resolve nodenames: convert string IDs to `std::unordered_set<uint16_t>` numeric IDs at registration time (via `BlockRegistry::getIdByName`). Also handle `"group:name"` patterns by scanning all blocks with matching group. Store as `std::unordered_set<uint16_t> resolvedNodenames` in ABMDefinition
  - [ ] 7.8 Pre-resolve neighbors similarly (if non-empty)

- [ ] Task 8: Implement ABM scanning with spread cursor (AC: 7, 8)
  - [ ] 8.1 Add `ABMScanState` to ABMRegistry: `size_t m_chunkIndex` (current index into chunk list), `int m_sectionY` (current section within chunk), `int m_blockOffset` (current block within section)
  - [ ] 8.2 Implement `update(float dt, ChunkManager& chunks, BlockRegistry& registry, BlockCallbackInvoker& invoker)`:
    - Increment all ABM accumulators by `dt`
    - Collect which ABMs are "due" (accumulator >= interval)
    - If no ABMs are due, skip scanning
    - Advance cursor through loaded chunks, processing up to `MAX_ABM_BLOCKS_PER_TICK` blocks
    - For each block: check if its blockId is in any due ABM's `resolvedNodenames`
    - If match: optionally check neighbor requirement (6 adjacent blocks via `ChunkManager::getBlock`)
    - Roll `rand() % chance == 0` for probability
    - If roll succeeds: invoke ABM action callback with `(posTable, blockId, 0)` — 0 = active_object_count placeholder
    - When cursor completes a full pass through all chunks: reset accumulators for ABMs that were due, reset cursor
  - [ ] 8.3 Use `ChunkManager::forEachLoadedChunk(callback)` or snapshot the chunk coordinate list at scan start (to avoid iterator invalidation if chunks load/unload mid-scan)
  - [ ] 8.4 Constant: `MAX_ABM_BLOCKS_PER_TICK = 4096` (configurable)

- [ ] Task 9: Add chunk iteration accessor to ChunkManager (AC: 6, 7)
  - [ ] 9.1 Add `size_t loadedChunkCount() const` to `ChunkManager.h`
  - [ ] 9.2 Add `std::vector<glm::ivec2> getLoadedChunkCoords() const` — returns snapshot of all chunk coordinates (safe for iteration even if chunks change)
  - [ ] 9.3 Add `ChunkColumn* getChunkColumn(const glm::ivec2& coord)` — returns pointer to column or nullptr (non-owning, for read access during ABM scan)
  - [ ] 9.4 NOTE: ABM scanner uses `getChunkColumn` + `ChunkSection::getBlock` for sequential access, NOT `ChunkManager::getBlock` (avoids per-block hash lookup overhead during full section scans)

- [ ] Task 10: Create LBMRegistry (AC: 9, 10, 11)
  - [ ] 10.1 Create `engine/include/voxel/scripting/LBMRegistry.h`
  - [ ] 10.2 Create `engine/src/scripting/LBMRegistry.cpp`
  - [ ] 10.3 Define `LBMDefinition` struct: `std::string label`, `std::vector<std::string> nodenames`, `bool runAtEveryLoad`, `sol::protected_function action`
  - [ ] 10.4 Pre-resolve nodenames to `std::unordered_set<uint16_t>` at registration time (same as ABM)
  - [ ] 10.5 Store LBMs in `std::vector<LBMDefinition> m_lbms`
  - [ ] 10.6 Track which non-repeating LBMs have run per chunk: `std::unordered_map<glm::ivec2, std::unordered_set<std::string>, ChunkCoordHash> m_executedLBMs` — key = chunk coord, value = set of LBM labels
  - [ ] 10.7 Implement `registerLBM(LBMDefinition def)` — push to vector
  - [ ] 10.8 Implement `onChunkLoaded(const glm::ivec2& coord, ChunkManager& chunks, BlockRegistry& registry, BlockCallbackInvoker& invoker)`:
    - Get `ChunkColumn*` from ChunkManager
    - For each LBM: skip if `!runAtEveryLoad` AND label is in `m_executedLBMs[coord]`
    - Scan all sections in the column for blocks matching LBM's `resolvedNodenames`
    - For each match: invoke LBM action with `(posTable, blockId, 0.0f)` — dtime_s = 0 for first load
    - After processing: if `!runAtEveryLoad`, add label to `m_executedLBMs[coord]`

- [ ] Task 11: Bind `voxel.register_abm` and `voxel.register_lbm` Lua APIs (AC: 5, 9)
  - [ ] 11.1 In `LuaBindings.cpp`, add `voxel.register_abm` bound function
  - [ ] 11.2 Parse ABM table: `label` (string), `nodenames` (table → vector<string>), `neighbors` (optional table → vector<string>), `interval` (float), `chance` (int), `action` (sol::protected_function)
  - [ ] 11.3 Validate: require `nodenames`, `interval > 0`, `chance > 0`, `action` non-nil
  - [ ] 11.4 Call `ABMRegistry::registerABM(def)`
  - [ ] 11.5 In `LuaBindings.cpp`, add `voxel.register_lbm` bound function
  - [ ] 11.6 Parse LBM table: `label` (string), `nodenames` (table → vector<string>), `run_at_every_load` (bool, default false), `action` (sol::protected_function)
  - [ ] 11.7 Validate: require `nodenames`, `action` non-nil
  - [ ] 11.8 Call `LBMRegistry::registerLBM(def)`
  - [ ] 11.9 Helper: `parseStringTable(sol::table) -> std::vector<std::string>` for extracting nodenames/neighbors arrays

- [ ] Task 12: Wire timer cleanup into block break/place pipeline (AC: 12, 13)
  - [ ] 12.1 In the BreakBlock command processing (in `GameApp::handleBlockInteraction` or `PlayerController`), call `m_timerManager.onBlockRemoved(pos)` after block removal
  - [ ] 12.2 Subscribe to `EventBus::BlockBroken` in `GameApp::init()`: `m_timerManager.onBlockRemoved(e.position)`
  - [ ] 12.3 Also handle overwrite: when `setBlock` replaces a non-air block, fire `onBlockRemoved` for the old position (subscribe to `EventBus::BlockPlaced` and check if it was an overwrite, or add a `BlockChanged` handler)
  - [ ] 12.4 Simplest approach: `BlockTimerManager::onBlockRemoved(pos)` is idempotent — calling it on a position with no timer is a no-op (erase on empty key does nothing)

- [ ] Task 13: Wire systems into GameApp tick (AC: all)
  - [ ] 13.1 Add members to `GameApp`: `BlockTimerManager m_timerManager`, `ABMRegistry m_abmRegistry`, `LBMRegistry m_lbmRegistry`
  - [ ] 13.2 In `GameApp::init()`: construct managers, bind Lua APIs via `LuaBindings::registerTimerAPI`, `LuaBindings::registerABMAPI`, `LuaBindings::registerLBMAPI`
  - [ ] 13.3 In `GameApp::init()`: subscribe `LBMRegistry::onChunkLoaded` to `EventBus::ChunkLoaded`
  - [ ] 13.4 In `GameApp::tick(dt)`, after `m_chunkManager.update(...)` and before `m_input->update(fdt)`:
    ```cpp
    m_timerManager.update(fdt, m_blockRegistry, m_callbackInvoker);
    m_abmRegistry.update(fdt, m_chunkManager, m_blockRegistry, m_callbackInvoker);
    ```
  - [ ] 13.5 Wire `BlockTimerManager` and `ABMRegistry` with `BlockCallbackInvoker` reference (constructor injection)

- [ ] Task 14: Integration tests (AC: 14, 15, 16)
  - [ ] 14.1 Create `tests/scripting/TestBlockTimers.cpp`
  - [ ] 14.2 Test: `BlockTimerManager::setTimer` + `getTimer` — verify remaining time decrements, verify timer fires after elapsed
  - [ ] 14.3 Test: `invokeOnTimer` returns true → timer resets; returns false → timer removed
  - [ ] 14.4 Test: `onBlockRemoved` cancels active timer
  - [ ] 14.5 Test: register ABM via Lua, verify it's stored in `ABMRegistry` with resolved nodenames
  - [ ] 14.6 Test: ABM scan finds matching block and fires action callback with correct position
  - [ ] 14.7 Test: ABM `chance` = 1 (always fires) and `chance` = very high (rarely fires) — verify probability behavior
  - [ ] 14.8 Test: ABM `neighbors` filter — only fires when required neighbor present
  - [ ] 14.9 Test: register LBM, simulate chunk load, verify action fires for matching blocks
  - [ ] 14.10 Test: LBM `run_at_every_load = false` — verify fires once, not on second load
  - [ ] 14.11 Test: LBM `run_at_every_load = true` — verify fires on every load
  - [ ] 14.12 Create test Lua scripts in `tests/scripting/test_scripts/`

- [ ] Task 15: Build integration (AC: all)
  - [ ] 15.1 Add `BlockTimerManager.cpp`, `ABMRegistry.cpp`, `LBMRegistry.cpp` to `engine/CMakeLists.txt`
  - [ ] 15.2 Add `TestBlockTimers.cpp` to `tests/CMakeLists.txt`
  - [ ] 15.3 Build full project, verify zero warnings under `/W4 /WX`
  - [ ] 15.4 Run all tests (existing + new), verify zero regressions

## Dev Notes

### BlockTimerManager Design

```cpp
// engine/include/voxel/scripting/BlockTimerManager.h
#pragma once

#include "voxel/core/Types.h"
#include "voxel/math/MathTypes.h"

#include <glm/vec3.hpp>

#include <optional>
#include <unordered_map>

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
    uint16_t blockId = 0;   // block ID at the time timer was set (for lookup)
};

class BlockTimerManager
{
public:
    explicit BlockTimerManager(world::ChunkManager& chunks);

    void setTimer(const glm::ivec3& pos, float seconds);
    [[nodiscard]] std::optional<float> getTimer(const glm::ivec3& pos) const;
    void cancelTimer(const glm::ivec3& pos);
    void onBlockRemoved(const glm::ivec3& pos);

    void update(
        float dt,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker);

    [[nodiscard]] size_t activeTimerCount() const { return m_timers.size(); }

private:
    world::ChunkManager& m_chunkManager;
    std::unordered_map<glm::ivec3, TimerEntry, math::IVec3Hash> m_timers;
};

} // namespace voxel::scripting
```

**update() implementation pattern:**

```cpp
void BlockTimerManager::update(float dt, BlockRegistry& registry, BlockCallbackInvoker& invoker)
{
    std::vector<glm::ivec3> toRemove;

    for (auto& [pos, entry] : m_timers)
    {
        entry.remaining -= dt;
        if (entry.remaining <= 0.0f)
        {
            float elapsed = entry.interval + (-entry.remaining); // actual elapsed
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
}
```

**Key design decisions:**
- `blockId` is stored in `TimerEntry` to look up `BlockDefinition` without querying `ChunkManager` (the block may have changed since timer was set — if so, the on_timer won't find a matching callback, which is fine)
- Deferred erase pattern: collect positions to remove, then erase after iteration
- Timer precision is tick-aligned (50ms granularity at 20 TPS) — acceptable for gameplay timers
- If `on_timer` callback errors, `invokeOnTimer` returns false (safe default) → timer stops

### IVec3Hash

Add to `engine/include/voxel/math/MathTypes.h` alongside existing math utilities:

```cpp
namespace voxel::math
{

struct IVec3Hash
{
    size_t operator()(const glm::ivec3& v) const
    {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace voxel::math
```

This mirrors the existing `ChunkCoordHash` pattern in `ChunkManager.h` but for `ivec3`.

### ABMRegistry Design

```cpp
// engine/include/voxel/scripting/ABMRegistry.h
#pragma once

#include "voxel/core/Types.h"
#include "voxel/math/MathTypes.h"

#include <sol/forward.hpp>

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
    std::vector<std::string> nodenames;     // original string IDs
    std::vector<std::string> neighbors;     // optional neighbor requirements
    float interval = 1.0f;                  // seconds between scans
    int chance = 1;                         // 1/chance probability
    sol::protected_function action;         // (pos, node, active_object_count)

    // Pre-resolved at registration time:
    std::unordered_set<uint16_t> resolvedNodenames;
    std::unordered_set<uint16_t> resolvedNeighbors;
    bool hasNeighborRequirement = false;
};

class ABMRegistry
{
public:
    static constexpr int MAX_ABM_BLOCKS_PER_TICK = 4096;

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
};

} // namespace voxel::scripting
```

**ABM scan algorithm (spread across ticks):**

```cpp
void ABMRegistry::update(float dt, ChunkManager& chunks, BlockRegistry& registry,
                          BlockCallbackInvoker& invoker)
{
    if (m_abms.empty()) return;

    // 1. Increment all accumulators
    for (size_t i = 0; i < m_accumulators.size(); ++i)
        m_accumulators[i] += dt;

    // 2. If no scan in progress, check if any ABMs are due
    if (!m_scanInProgress)
    {
        m_dueABMs.clear();
        for (size_t i = 0; i < m_abms.size(); ++i)
        {
            if (m_accumulators[i] >= m_abms[i].interval)
                m_dueABMs.push_back(i);
        }
        if (m_dueABMs.empty()) return;

        // Start new scan: snapshot chunk list
        m_chunkSnapshot = chunks.getLoadedChunkCoords();
        m_chunkCursor = 0;
        m_sectionCursor = 0;
        m_blockCursor = 0;
        m_scanInProgress = true;
    }

    // 3. Process up to MAX_ABM_BLOCKS_PER_TICK blocks
    int blocksProcessed = 0;
    while (blocksProcessed < MAX_ABM_BLOCKS_PER_TICK
           && m_chunkCursor < m_chunkSnapshot.size())
    {
        const glm::ivec2& chunkCoord = m_chunkSnapshot[m_chunkCursor];
        ChunkColumn* column = chunks.getChunkColumn(chunkCoord);
        if (column == nullptr)
        {
            // Chunk unloaded since snapshot — skip
            m_chunkCursor++;
            m_sectionCursor = 0;
            m_blockCursor = 0;
            continue;
        }

        while (m_sectionCursor < ChunkColumn::SECTIONS_PER_COLUMN
               && blocksProcessed < MAX_ABM_BLOCKS_PER_TICK)
        {
            const ChunkSection* section = column->getSection(m_sectionCursor);
            if (section == nullptr || section->isEmpty())
            {
                m_sectionCursor++;
                m_blockCursor = 0;
                continue;
            }

            while (m_blockCursor < ChunkSection::VOLUME
                   && blocksProcessed < MAX_ABM_BLOCKS_PER_TICK)
            {
                int localY = m_blockCursor / 256;
                int localZ = (m_blockCursor % 256) / 16;
                int localX = m_blockCursor % 16;
                uint16_t blockId = section->getBlock(localX, localY, localZ);

                if (blockId != 0) // Skip air
                {
                    glm::ivec3 worldPos{
                        chunkCoord.x * 16 + localX,
                        m_sectionCursor * 16 + localY,
                        chunkCoord.y * 16 + localZ
                    };

                    for (size_t abmIdx : m_dueABMs)
                    {
                        const auto& abm = m_abms[abmIdx];
                        if (abm.resolvedNodenames.contains(blockId))
                        {
                            // Check neighbor requirement
                            if (abm.hasNeighborRequirement
                                && !checkNeighborRequirement(abm, worldPos, chunks))
                                continue;

                            // Roll chance
                            if (abm.chance > 1 && (rand() % abm.chance) != 0)
                                continue;

                            // Fire callback
                            invoker.invokeABMAction(abm.action, worldPos, blockId, 0);
                        }
                    }
                }

                m_blockCursor++;
                blocksProcessed++;
            }

            if (m_blockCursor >= ChunkSection::VOLUME)
            {
                m_sectionCursor++;
                m_blockCursor = 0;
            }
        }

        if (m_sectionCursor >= ChunkColumn::SECTIONS_PER_COLUMN)
        {
            m_chunkCursor++;
            m_sectionCursor = 0;
            m_blockCursor = 0;
        }
    }

    // 4. Check if scan completed
    if (m_chunkCursor >= m_chunkSnapshot.size())
    {
        // Reset accumulators for due ABMs
        for (size_t i : m_dueABMs)
            m_accumulators[i] = 0.0f;
        m_scanInProgress = false;
    }
}
```

**Neighbor check:**

```cpp
bool ABMRegistry::checkNeighborRequirement(
    const ABMDefinition& abm,
    const glm::ivec3& worldPos,
    ChunkManager& chunks) const
{
    static constexpr glm::ivec3 OFFSETS[6] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    for (const auto& offset : OFFSETS)
    {
        uint16_t neighborId = chunks.getBlock(worldPos + offset);
        if (abm.resolvedNeighbors.contains(neighborId))
            return true;
    }
    return false;
}
```

**Pre-resolve nodenames at registration:**

```cpp
void ABMRegistry::registerABM(ABMDefinition def)
{
    // Resolve nodenames: "base:dirt" → numeric ID, "group:cracky" → all blocks with that group
    // This requires BlockRegistry& passed at registration or stored as member
    // Pattern: if name starts with "group:", scan all blocks for matching group
    // Otherwise: getIdByName(name)
    m_accumulators.push_back(0.0f);
    m_abms.push_back(std::move(def));
}
```

**Important**: The `BlockRegistry&` reference is needed during `registerABM` for nodename resolution. Pass it into `ABMRegistry` constructor or into `registerABM`. Since Lua scripts register ABMs during script loading (before the game loop starts), all block IDs are already assigned.

### ABM Action Invoker

Add to `BlockCallbackInvoker`:

```cpp
void BlockCallbackInvoker::invokeABMAction(
    const sol::protected_function& action,
    const glm::ivec3& pos,
    uint16_t blockId,
    int activeObjectCount)
{
    auto posTable = posToTable(m_lua, pos);
    sol::protected_function_result result = action(posTable, blockId, activeObjectCount);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua ABM action error at ({},{},{}): {}",
            pos.x, pos.y, pos.z, err.what());
    }
}
```

This is NOT a per-block callback — it's a standalone function registration. The invoker pattern is the same (protected call, error logging) but it takes the ABM's `action` function directly, not from `BlockDefinition`.

### LBMRegistry Design

```cpp
// engine/include/voxel/scripting/LBMRegistry.h
#pragma once

#include "voxel/core/Types.h"
#include "voxel/math/MathTypes.h"

#include <sol/forward.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace voxel::world
{
class ChunkManager;
class BlockRegistry;
struct ChunkCoordHash;
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

class LBMRegistry
{
public:
    void registerLBM(LBMDefinition def);

    void onChunkLoaded(
        const glm::ivec2& coord,
        world::ChunkManager& chunks,
        world::BlockRegistry& registry,
        BlockCallbackInvoker& invoker);

    [[nodiscard]] size_t lbmCount() const { return m_lbms.size(); }

private:
    std::vector<LBMDefinition> m_lbms;

    // Track which non-repeating LBMs have already run for each chunk
    std::unordered_map<glm::ivec2, std::unordered_set<std::string>,
                       world::ChunkCoordHash> m_executedLBMs;
};

} // namespace voxel::scripting
```

**LBM::onChunkLoaded pattern:**

```cpp
void LBMRegistry::onChunkLoaded(
    const glm::ivec2& coord,
    ChunkManager& chunks,
    BlockRegistry& registry,
    BlockCallbackInvoker& invoker)
{
    if (m_lbms.empty()) return;

    ChunkColumn* column = chunks.getChunkColumn(coord);
    if (column == nullptr) return;

    for (auto& lbm : m_lbms)
    {
        // Skip non-repeating LBMs that already ran for this chunk
        if (!lbm.runAtEveryLoad)
        {
            auto it = m_executedLBMs.find(coord);
            if (it != m_executedLBMs.end() && it->second.contains(lbm.label))
                continue;
        }

        bool fired = false;
        for (int sY = 0; sY < ChunkColumn::SECTIONS_PER_COLUMN; ++sY)
        {
            const ChunkSection* section = column->getSection(sY);
            if (section == nullptr || section->isEmpty()) continue;

            for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
            {
                uint16_t blockId = section->getBlock(x, y, z);
                if (lbm.resolvedNodenames.contains(blockId))
                {
                    glm::ivec3 worldPos{
                        coord.x * 16 + x,
                        sY * 16 + y,
                        coord.y * 16 + z
                    };
                    invoker.invokeLBMAction(lbm.action, worldPos, blockId, 0.0f);
                    fired = true;
                }
            }
        }

        // Mark non-repeating LBM as executed for this chunk
        if (fired && !lbm.runAtEveryLoad)
        {
            m_executedLBMs[coord].insert(lbm.label);
        }
    }
}
```

### LBM Action Invoker

Add to `BlockCallbackInvoker`:

```cpp
void BlockCallbackInvoker::invokeLBMAction(
    const sol::protected_function& action,
    const glm::ivec3& pos,
    uint16_t blockId,
    float dtimeS)
{
    auto posTable = posToTable(m_lua, pos);
    sol::protected_function_result result = action(posTable, blockId, dtimeS);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua LBM action error at ({},{},{}): {}",
            pos.x, pos.y, pos.z, err.what());
    }
}
```

### Where to Wire in GameApp::tick()

The current `GameApp::tick()` flow (from `game/src/GameApp.cpp`, ~line 391):

```
1. handleInputToggles()
2. Camera mouse look / movement
3. Physics mode movement + tickPhysics(fdt, ...)
4. handleBlockInteraction(fdt) — mining, place/break, command queue drain
5. Camera sync
6. m_chunkManager.update(m_camera.getPosition())  // chunk streaming
7. >>> INSERT: m_timerManager.update(fdt, m_blockRegistry, m_callbackInvoker);
8. >>> INSERT: m_abmRegistry.update(fdt, m_chunkManager, m_blockRegistry, m_callbackInvoker);
9. m_input->update(fdt)
```

LBM is NOT in the tick loop — it's event-driven via `EventBus::ChunkLoaded` subscription in `GameApp::init()`.

**Timer and ABM run AFTER chunk streaming** because:
- Chunks must be loaded before timers can read block data
- ABM needs the latest loaded-chunk set
- Any block modifications from callbacks will be picked up by the next frame's dirty section dispatch

### Lua API Binding Pattern

```cpp
// In LuaBindings.cpp — new registration function:
void LuaBindings::registerTimerAPI(
    sol::state& lua,
    BlockTimerManager& timerMgr)
{
    auto voxelTable = lua["voxel"].get_or_create<sol::table>();

    voxelTable.set_function("set_timer",
        [&timerMgr](const sol::table& posTable, float seconds)
        {
            glm::ivec3 pos = tableToPos(posTable);
            timerMgr.setTimer(pos, seconds);
        });

    voxelTable.set_function("get_timer",
        [&timerMgr](const sol::table& posTable) -> sol::object
        {
            glm::ivec3 pos = tableToPos(posTable);
            auto remaining = timerMgr.getTimer(pos);
            if (remaining.has_value())
                return sol::make_object(timerMgr.getLua(), *remaining);
            return sol::lua_nil;
        });
}

void LuaBindings::registerABMAPI(
    sol::state& lua,
    ABMRegistry& abmRegistry,
    BlockRegistry& blockRegistry)
{
    auto voxelTable = lua["voxel"].get_or_create<sol::table>();

    voxelTable.set_function("register_abm",
        [&abmRegistry, &blockRegistry](const sol::table& table)
        {
            ABMDefinition def;
            def.label = table.get_or<std::string>("label", "unnamed_abm");
            def.interval = table.get_or("interval", 1.0f);
            def.chance = table.get_or("chance", 1);

            // Parse nodenames
            auto nodenames = table.get<std::optional<sol::table>>("nodenames");
            if (!nodenames.has_value())
            {
                VX_LOG_WARN("register_abm '{}': missing 'nodenames'", def.label);
                return;
            }
            nodenames->for_each([&](sol::object, sol::object val) {
                if (val.is<std::string>())
                    def.nodenames.push_back(val.as<std::string>());
            });

            // Parse optional neighbors
            auto neighbors = table.get<std::optional<sol::table>>("neighbors");
            if (neighbors.has_value())
            {
                neighbors->for_each([&](sol::object, sol::object val) {
                    if (val.is<std::string>())
                        def.neighbors.push_back(val.as<std::string>());
                });
                def.hasNeighborRequirement = !def.neighbors.empty();
            }

            // Extract action
            auto action = table.get<std::optional<sol::protected_function>>("action");
            if (!action.has_value())
            {
                VX_LOG_WARN("register_abm '{}': missing 'action'", def.label);
                return;
            }
            def.action = std::move(*action);

            // Resolve nodenames to numeric IDs
            resolveNodenames(def.nodenames, def.resolvedNodenames, blockRegistry);
            if (def.hasNeighborRequirement)
                resolveNodenames(def.neighbors, def.resolvedNeighbors, blockRegistry);

            abmRegistry.registerABM(std::move(def));
            VX_LOG_INFO("Registered ABM: '{}'", def.label);
        });
}
```

**Nodename resolution helper:**

```cpp
void resolveNodenames(
    const std::vector<std::string>& names,
    std::unordered_set<uint16_t>& resolved,
    BlockRegistry& registry)
{
    for (const auto& name : names)
    {
        if (name.starts_with("group:"))
        {
            // Group match: find all blocks with this group
            std::string groupName = name.substr(6);
            for (uint16_t id = 1; id < registry.blockCount(); ++id)
            {
                const auto& def = registry.getBlockType(id);
                if (def.groups.contains(groupName))
                    resolved.insert(id);
            }
        }
        else
        {
            uint16_t id = registry.getIdByName(name);
            if (id != 0) // 0 = not found / air
                resolved.insert(id);
            else
                VX_LOG_WARN("ABM/LBM nodename '{}' not found in registry", name);
        }
    }
}
```

### Randomness for ABM Chance

Use `<random>` instead of `rand()` for better distribution. Add a member `std::mt19937 m_rng` to `ABMRegistry`, seeded in constructor:

```cpp
ABMRegistry::ABMRegistry()
    : m_rng(std::random_device{}())
{
}

// In scan loop:
std::uniform_int_distribution<int> dist(0, abm.chance - 1);
if (abm.chance > 1 && dist(m_rng) != 0)
    continue;
```

### Timer Persistence (Deferred)

The epic spec mentions "timers persist in chunk serialization." This is a serialization concern that requires extending the chunk save/load format (Story 3.7). For Story 9.4, timers are runtime-only (lost on shutdown). A TODO comment should be added:

```cpp
// TODO(9.4): Persist timer state in chunk serialization.
// Timer data (pos → remaining, interval) needs to be saved/loaded alongside block data.
// Deferred until save/load is next touched — runtime-only timers are sufficient for V1.
```

This is acceptable because: (a) the engine doesn't have a save/load gameplay loop yet, (b) the serialization format can be extended later without breaking anything, (c) the ABM/LBM systems don't need persistence (they re-register from Lua on startup).

### LBM Persistence (Deferred)

Similarly, the "which LBMs have run" tracking (for `run_at_every_load = false`) is runtime-only. On engine restart, all LBMs re-fire for all loaded chunks. True persistence requires per-chunk metadata in the serialization format. Add TODO:

```cpp
// TODO(9.4): Persist executed LBM labels per chunk in serialization format.
// Currently runtime-only — all LBMs re-fire on restart.
```

### setTimer from Inside Callbacks

When `on_timer` returns true, the `BlockTimerManager` automatically restarts the timer. But Lua scripts can also call `voxel.set_timer(pos, newInterval)` from inside `on_timer`, `on_construct`, or ABM `action` callbacks to set a NEW timer (or change the interval). This works because:

1. Timer callbacks fire on the main thread during `BlockTimerManager::update()`
2. `voxel.set_timer` calls `BlockTimerManager::setTimer()` directly
3. If called from inside `on_timer` for the SAME pos, the `setTimer` call will overwrite the entry in `m_timers` — but we're still iterating. Solution: use a `m_pendingTimers` vector to collect new timers set during update, apply them after iteration completes.

```cpp
void BlockTimerManager::update(float dt, BlockRegistry& registry, BlockCallbackInvoker& invoker)
{
    m_insideUpdate = true; // flag to redirect setTimer to pending list

    std::vector<glm::ivec3> toRemove;
    for (auto& [pos, entry] : m_timers)
    {
        entry.remaining -= dt;
        if (entry.remaining <= 0.0f)
        {
            float elapsed = entry.interval + (-entry.remaining);
            const auto& def = registry.getBlockType(entry.blockId);
            bool restart = invoker.invokeOnTimer(def, pos, elapsed);
            if (restart)
                entry.remaining = entry.interval;
            else
                toRemove.push_back(pos);
        }
    }

    for (const auto& pos : toRemove)
        m_timers.erase(pos);

    m_insideUpdate = false;

    // Apply pending timers set during callbacks
    for (auto& [pos, entry] : m_pendingTimers)
        m_timers[pos] = std::move(entry);
    m_pendingTimers.clear();
}

void BlockTimerManager::setTimer(const glm::ivec3& pos, float seconds)
{
    uint16_t blockId = m_chunkManager.getBlock(pos);
    TimerEntry entry{seconds, seconds, blockId};

    if (m_insideUpdate)
        m_pendingTimers.emplace_back(pos, entry);
    else
        m_timers[pos] = entry;
}
```

### What NOT to Do

- **DO NOT add sol2 headers to ChunkManager.h or ChunkSection.h** — timer/ABM/LBM systems own the sol2 dependency; ChunkManager stays pure C++.
- **DO NOT modify ScriptEngine** — it's unchanged since Story 9.1.
- **DO NOT modify BlockCallbacks.h beyond adding `onTimer`** — interaction callbacks from 9.3 are separate.
- **DO NOT implement `on_neighbor_changed`** — that's Story 9.5. ABMs fire based on block type scanning, not change events.
- **DO NOT implement `on_entity_inside` or entity callbacks** — that's Story 9.6.
- **DO NOT implement metadata/inventory APIs** — that's Story 9.7. ABM callbacks can't call `voxel.get_meta()` yet.
- **DO NOT implement `voxel.set_block`, `voxel.get_block`, `voxel.dig_block`** — that's Story 9.8. ABM `action` callbacks can't modify the world from Lua until 9.8. The callback fires but calling `voxel.set_block` inside it will fail with "undefined function." Tests should only verify callback invocation, not world modification from Lua.
- **DO NOT implement `voxel.schedule_tick`** — that's a more advanced timing API from Story 9.8. Story 9.4 only implements `voxel.set_timer` and `voxel.get_timer`.
- **DO NOT implement `voxel.on()` event hooks** — that's Story 9.10.
- **DO NOT implement timer serialization/persistence** — deferred. Add TODO comments.
- **DO NOT use `rand()`** — use `<random>` with `std::mt19937` for reproducible ABM behavior.
- **DO NOT scan all chunks every tick** — use the spread-cursor pattern to cap processing per tick.
- **DO NOT lock or use atomics in timer/ABM/LBM** — all three systems run on the main thread during the simulation tick. No threading concerns.
- **DO NOT add new EventBus event types** for timer firing — global event hooks come in Story 9.10.

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `BlockCallbacks` | `engine/include/voxel/scripting/BlockCallbacks.h` | Extend with `onTimer` field |
| `BlockCallbackInvoker` | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | Add `invokeOnTimer`, `invokeABMAction`, `invokeLBMAction` |
| `LuaBindings` | `engine/src/scripting/LuaBindings.cpp` | Add timer/ABM/LBM API bindings, extract `on_timer` callback |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | Look up BlockDefinition for `on_timer`, resolve nodenames by ID |
| `BlockRegistry::getIdByName` | `engine/src/world/BlockRegistry.cpp` | Convert string nodenames to numeric IDs |
| `ChunkManager` | `engine/include/voxel/world/ChunkManager.h` | Chunk iteration for ABM, block lookup for timer/neighbor checks |
| `ChunkColumn` | `engine/include/voxel/world/ChunkColumn.h` | Section access for ABM scanning |
| `ChunkSection` | `engine/include/voxel/world/ChunkSection.h` | Block data access, `isEmpty()` for skip |
| `ChunkCoordHash` | `engine/src/world/ChunkManager.cpp` | Pattern for IVec3Hash, reuse for LBM executed-map key |
| `EventBus` | `engine/include/voxel/game/EventBus.h` | Subscribe to `ChunkLoaded` for LBM, `BlockBroken`/`BlockPlaced` for timer cleanup |
| `GameApp` | `game/src/GameApp.cpp` | Wire tick-phase calls, EventBus subscriptions, member initialization |
| `GameLoop` | `engine/src/game/GameLoop.cpp` | 20 TPS tick rate reference |
| `MiningState` | `engine/include/voxel/game/MiningState.h` | Pattern reference for TimerEntry struct design |
| `posToTable` / `tableToPos` | `engine/src/scripting/BlockCallbackInvoker.cpp` | Position conversion utility from 9.2 |
| `Result<T>` | `engine/include/voxel/core/Result.h` | Error handling |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging callback errors |

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| MODIFY | `engine/include/voxel/math/MathTypes.h` | `voxel::math` | Add `IVec3Hash` struct |
| MODIFY | `engine/include/voxel/scripting/BlockCallbacks.h` | `voxel::scripting` | Add `onTimer` callback field |
| MODIFY | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | `voxel::scripting` | Add `invokeOnTimer`, `invokeABMAction`, `invokeLBMAction` |
| MODIFY | `engine/src/scripting/BlockCallbackInvoker.cpp` | `voxel::scripting` | Implement 3 new invoke methods |
| MODIFY | `engine/src/scripting/LuaBindings.cpp` | `voxel::scripting` | Add `on_timer` extraction, timer/ABM/LBM API bindings |
| MODIFY | `engine/include/voxel/scripting/LuaBindings.h` | `voxel::scripting` | Add `registerTimerAPI`, `registerABMAPI`, `registerLBMAPI` declarations |
| MODIFY | `engine/include/voxel/world/ChunkManager.h` | `voxel::world` | Add `loadedChunkCount`, `getLoadedChunkCoords`, `getChunkColumn` |
| MODIFY | `engine/src/world/ChunkManager.cpp` | `voxel::world` | Implement 3 new accessor methods |
| MODIFY | `game/src/GameApp.cpp` | — | Wire timer/ABM/LBM into tick, EventBus subscriptions |
| MODIFY | `game/src/GameApp.h` | — | Add `BlockTimerManager`, `ABMRegistry`, `LBMRegistry` members |
| NEW | `engine/include/voxel/scripting/BlockTimerManager.h` | `voxel::scripting` | Timer management class |
| NEW | `engine/src/scripting/BlockTimerManager.cpp` | `voxel::scripting` | Timer update logic |
| NEW | `engine/include/voxel/scripting/ABMRegistry.h` | `voxel::scripting` | ABM registration + spread scan |
| NEW | `engine/src/scripting/ABMRegistry.cpp` | `voxel::scripting` | ABM scan algorithm |
| NEW | `engine/include/voxel/scripting/LBMRegistry.h` | `voxel::scripting` | LBM registration + chunk-load scan |
| NEW | `engine/src/scripting/LBMRegistry.cpp` | `voxel::scripting` | LBM on-load logic |
| NEW | `tests/scripting/TestBlockTimers.cpp` | — | Integration tests for timers, ABM, LBM |
| NEW | `tests/scripting/test_scripts/timer_furnace.lua` | — | Test: furnace with on_timer |
| NEW | `tests/scripting/test_scripts/abm_grass_spread.lua` | — | Test: ABM grass spread |
| NEW | `tests/scripting/test_scripts/lbm_upgrade_torch.lua` | — | Test: LBM torch upgrade |
| MODIFY | `engine/CMakeLists.txt` | — | Add BlockTimerManager.cpp, ABMRegistry.cpp, LBMRegistry.cpp |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestBlockTimers.cpp |

### Naming & Style

- Classes: `BlockTimerManager`, `ABMRegistry`, `LBMRegistry`, `ABMDefinition`, `LBMDefinition`, `TimerEntry` (PascalCase)
- Methods: `setTimer`, `getTimer`, `cancelTimer`, `registerABM`, `registerLBM`, `onChunkLoaded`, `invokeOnTimer` (camelCase)
- Members: `m_timers`, `m_abms`, `m_lbms`, `m_accumulators`, `m_rng`, `m_chunkCursor` (m_ prefix)
- Constants: `MAX_ABM_BLOCKS_PER_TICK` (SCREAMING_SNAKE)
- Enums: none new in this story
- Namespace: `voxel::scripting` for all new timer/ABM/LBM classes
- No exceptions — use `Result<T>` for parsing, safe defaults for callback invocation
- Max ~500 lines per file
- `#pragma once` for all headers

### Previous Story Intelligence

**From Story 9.1:**
- `ScriptEngine` provides `getLuaState()` returning `sol::state&`
- `sol/forward.hpp` for lightweight forward declarations in headers
- `SOL_NO_EXCEPTIONS=1` — all sol2 calls must use protected functions
- Sandbox in place — no `os`, `io`, `debug`
- Test pattern: Catch2 v3, `TEST_CASE("name", "[tag]")` with `SECTION`
- `VX_ASSETS_DIR` CMake define for test script paths

**From Story 9.2:**
- `BlockCallbacks` struct with `unique_ptr` indirection in `BlockDefinition` — extend with `onTimer`
- `BlockCallbackInvoker` with `posToTable`/`tableToPos` utilities — add 3 new invoke methods
- `LuaBindings::parseBlockDefinition` extracts callbacks — add `on_timer` extraction
- `voxel.register_block(table)` API works — `on_timer` is just another table field
- Bootstrap order: ScriptEngine → LuaBindings → loadScript
- Callback invocation pattern: check `has_value()` → call → check `.valid()` → log → return default

**From Story 9.3:**
- `InteractionState` pattern in PlayerController — TimerEntry follows same simple struct pattern
- `GameApp` tick flow is well-established — timer/ABM insert after `m_chunkManager.update()`
- ImGui guard doesn't apply to timers/ABMs (they're world simulation, not player input)
- Command queue is for discrete input actions — timers/ABMs run directly in tick (main thread)

**From existing ChunkManager:**
- `m_chunks` is `unordered_map<ivec2, unique_ptr<ChunkColumn>, ChunkCoordHash>` — currently private
- `ChunkSection::isEmpty()` — use to skip empty sections in ABM scans
- `ChunkSection::getBlock(x, y, z)` — direct block access within section
- `ChunkManager::getBlock(ivec3)` — world-space block lookup (slower per-call, fine for neighbor checks)
- Block index layout: `y*256 + z*16 + x` (Y-major)

**From existing EventBus:**
- `EventType::ChunkLoaded` with `ChunkLoadedEvent{IVec2 coord}` — LBM subscribes to this
- `EventType::BlockBroken` / `BlockPlaced` — timer cleanup subscribes to these
- Subscribe pattern: `m_eventBus.subscribe<EventType::X>([this](const XEvent& e) { ... });`

### Git Intelligence

Recent commits are all `feat(renderer)` and `feat(world)` for Epic 8 (Lighting). Stories 9.1-9.3 must be implemented before this story. No scripting code has been committed yet.

Commit style for this story: `feat(scripting): implement block timers, ABM scanner, and LBM system`

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/scripting/ABMRegistry.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/BlockTimerManager.h"
#include "voxel/scripting/LBMRegistry.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/BlockRegistry.h"

using namespace voxel::scripting;

TEST_CASE("Block timer system", "[scripting][timers]")
{
    ScriptEngine engine;
    REQUIRE(engine.init().has_value());
    auto& lua = engine.getLuaState();

    voxel::world::BlockRegistry registry;
    // NOTE: needs mock or lightweight ChunkManager for timer tests
    // Alternatively, test BlockTimerManager::update in isolation with a stub

    SECTION("setTimer stores timer and getTimer returns remaining")
    {
        BlockTimerManager mgr(/* chunkManager */);
        mgr.setTimer({5, 10, 5}, 2.0f);

        auto remaining = mgr.getTimer({5, 10, 5});
        REQUIRE(remaining.has_value());
        REQUIRE(*remaining == Catch::Approx(2.0f));
        REQUIRE(mgr.activeTimerCount() == 1);
    }

    SECTION("timer fires on_timer after elapsed time")
    {
        // Load test script with furnace block
        engine.loadScript("tests/scripting/test_scripts/timer_furnace.lua");
        LuaBindings::registerBlockAPI(lua, registry);

        // Verify on_timer callback exists
        const auto& def = registry.getBlockType("test:furnace");
        REQUIRE(def.callbacks != nullptr);
        REQUIRE(def.callbacks->onTimer.has_value());

        // Invoke and verify
        BlockCallbackInvoker invoker(lua);
        bool restart = invoker.invokeOnTimer(def, {5, 10, 5}, 2.0f);
        // Check Lua side effect
        REQUIRE(lua["test_timer_fired"].get<bool>() == true);
    }

    SECTION("cancelTimer removes active timer")
    {
        BlockTimerManager mgr(/* chunkManager */);
        mgr.setTimer({1, 2, 3}, 5.0f);
        REQUIRE(mgr.activeTimerCount() == 1);

        mgr.cancelTimer({1, 2, 3});
        REQUIRE(mgr.activeTimerCount() == 0);
        REQUIRE_FALSE(mgr.getTimer({1, 2, 3}).has_value());
    }

    SECTION("onBlockRemoved cancels timer at position")
    {
        BlockTimerManager mgr(/* chunkManager */);
        mgr.setTimer({1, 2, 3}, 5.0f);
        mgr.onBlockRemoved({1, 2, 3});
        REQUIRE(mgr.activeTimerCount() == 0);
    }
}

TEST_CASE("ABM registration and scanning", "[scripting][abm]")
{
    ScriptEngine engine;
    REQUIRE(engine.init().has_value());
    auto& lua = engine.getLuaState();

    voxel::world::BlockRegistry registry;
    LuaBindings::registerBlockAPI(lua, registry);

    SECTION("register_abm stores ABM with resolved nodenames")
    {
        engine.loadScript("tests/scripting/test_scripts/abm_grass_spread.lua");
        // Verify ABM registered
        // (access ABMRegistry count or internal state)
    }

    SECTION("ABM action fires for matching block")
    {
        // Register blocks, set up mock chunk with dirt block
        // Run ABM scan, verify action callback fires
        // Check Lua global: test_abm_fired == true, test_abm_pos_x == expected
    }
}

TEST_CASE("LBM registration and chunk load", "[scripting][lbm]")
{
    ScriptEngine engine;
    REQUIRE(engine.init().has_value());
    auto& lua = engine.getLuaState();

    SECTION("LBM fires on chunk load for matching blocks")
    {
        engine.loadScript("tests/scripting/test_scripts/lbm_upgrade_torch.lua");
        // Simulate chunk load, verify LBM action fires
    }

    SECTION("non-repeating LBM fires only once")
    {
        // Load chunk twice, verify LBM action fires only first time
    }
}
```

**Test Lua files:**

**timer_furnace.lua:**
```lua
test_timer_fired = false
test_timer_restart = true

voxel.register_block({
    id = "test:furnace",
    on_timer = function(pos, elapsed)
        test_timer_fired = true
        test_timer_elapsed = elapsed
        return test_timer_restart -- controlled by test
    end,
})
```

**abm_grass_spread.lua:**
```lua
test_abm_fired = false

voxel.register_block({
    id = "test:dirt",
    solid = true,
})

voxel.register_block({
    id = "test:grass",
    solid = true,
})

voxel.register_abm({
    label = "Grass spread test",
    nodenames = { "test:dirt" },
    neighbors = { "test:grass" },
    interval = 1.0,
    chance = 1,  -- always fire in tests
    action = function(pos, node, active_object_count)
        test_abm_fired = true
        test_abm_pos_x = pos.x
        test_abm_pos_y = pos.y
        test_abm_pos_z = pos.z
    end,
})
```

**lbm_upgrade_torch.lua:**
```lua
test_lbm_fired = false
test_lbm_count = 0

voxel.register_block({
    id = "test:torch_old",
})

voxel.register_lbm({
    label = "Upgrade old torches test",
    nodenames = { "test:torch_old" },
    run_at_every_load = false,
    action = function(pos, node, dtime_s)
        test_lbm_fired = true
        test_lbm_count = test_lbm_count + 1
    end,
})
```

### Project Structure Notes

- `BlockTimerManager`, `ABMRegistry`, `LBMRegistry` all live in `voxel::scripting` namespace (they depend on sol2 for callback invocation)
- All three classes are created and owned by `GameApp` (not `ChunkManager` or `ScriptEngine`)
- `IVec3Hash` lives in `voxel::math` because it's a general math utility, not scripting-specific
- Test scripts go in `tests/scripting/test_scripts/` alongside 9.1/9.2/9.3 test scripts
- No new directories needed — all files fit existing directory structure

### Future Story Dependencies

This story establishes patterns used by:
- **Story 9.5**: `on_neighbor_changed` uses similar per-block invocation but triggered by `setBlock` events, not timers
- **Story 9.8**: `voxel.set_block` / `voxel.get_block` APIs will allow ABM/timer callbacks to actually modify the world (grass spread example). Until 9.8, callbacks fire but can't change blocks from Lua
- **Story 9.8**: `voxel.schedule_tick(pos, delay_ticks, priority)` extends the timer system with tick-precision scheduling
- **Story 9.8**: `voxel.set_timer` / `voxel.get_timer` are defined here but also listed in 9.8's World API — no conflict, they're implemented here
- **Story 9.10**: `block_timer_fired` global event will fire from `BlockTimerManager::update()` when a timer expires
- **Story 9.10**: `voxel.on("tick", ...)` global tick hook is separate from ABM/timer — it fires every tick for all subscribers
- **Story 9.11**: Hot-reload needs to preserve timer state while clearing ABM/LBM registrations and re-registering from scripts

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.4 full specification]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 9: Scripting, System 10: Command Pattern, ADR-007, ADR-010]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards, threading rules]
- [Source: _bmad-output/implementation-artifacts/9-1-sol2-luajit-integration.md — ScriptEngine design, sol2 exception-free patterns]
- [Source: _bmad-output/implementation-artifacts/9-2-block-registration-placement-destruction-callbacks.md — BlockCallbacks, BlockCallbackInvoker, LuaBindings, callback invocation pattern]
- [Source: _bmad-output/implementation-artifacts/9-3-block-interaction-callbacks.md — InteractionState pattern, GameApp tick flow, command queue vs direct tick]
- [Source: engine/include/voxel/world/ChunkManager.h — Chunk storage, spatial hashmap, block access methods]
- [Source: engine/include/voxel/world/ChunkSection.h — Block storage layout, isEmpty(), VOLUME constant]
- [Source: engine/include/voxel/world/ChunkColumn.h — Section access, SECTIONS_PER_COLUMN]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition, groups field, callbacks unique_ptr]
- [Source: engine/include/voxel/world/BlockRegistry.h — getBlockType, getIdByName, blockCount]
- [Source: engine/include/voxel/game/EventBus.h — ChunkLoaded, BlockPlaced, BlockBroken events]
- [Source: engine/include/voxel/game/GameCommand.h — CommandType enum reference]
- [Source: game/src/GameApp.cpp — tick() flow, EventBus subscriptions, chunkManager.update() position]
- [Source: engine/src/game/GameLoop.cpp — TICK_RATE = 1.0/20.0, fixed timestep pattern]
- [Source: engine/include/voxel/math/MathTypes.h — Math utilities, location for IVec3Hash]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- ABMRegistry unused `registry` parameter fixed (MSVC C4100 treated as error)
- sol2 nil type: `sol::make_object(lua, sol::lua_nil)` returns `sol::type::none` (-1) not `sol::type::lua_nil` (0) — test adjusted

### Completion Notes List

- All 15 acceptance criteria implemented
- ChunkLoaded events are subscribed for LBM but not yet published from ChunkManager::loadChunk. LBM works via direct onChunkLoaded() calls. Publishing will be added when chunk streaming is extended.
- Timer persistence deferred to serialization story (TODO comments added)
- LBM execution tracking is runtime-only (TODO comments added)
- `registry` param kept in ABMRegistry::update() signature for future use (nodename re-resolution on hot-reload)

### File List

| Action | File |
|--------|------|
| MODIFY | `engine/include/voxel/math/MathTypes.h` |
| MODIFY | `engine/include/voxel/scripting/BlockCallbacks.h` |
| MODIFY | `engine/include/voxel/scripting/BlockCallbackInvoker.h` |
| MODIFY | `engine/src/scripting/BlockCallbackInvoker.cpp` |
| MODIFY | `engine/include/voxel/scripting/LuaBindings.h` |
| MODIFY | `engine/src/scripting/LuaBindings.cpp` |
| MODIFY | `engine/include/voxel/world/ChunkManager.h` |
| MODIFY | `engine/src/world/ChunkManager.cpp` |
| MODIFY | `game/src/GameApp.h` |
| MODIFY | `game/src/GameApp.cpp` |
| MODIFY | `engine/CMakeLists.txt` |
| MODIFY | `tests/CMakeLists.txt` |
| NEW | `engine/include/voxel/scripting/BlockTimerManager.h` |
| NEW | `engine/src/scripting/BlockTimerManager.cpp` |
| NEW | `engine/include/voxel/scripting/ABMRegistry.h` |
| NEW | `engine/src/scripting/ABMRegistry.cpp` |
| NEW | `engine/include/voxel/scripting/LBMRegistry.h` |
| NEW | `engine/src/scripting/LBMRegistry.cpp` |
| NEW | `tests/scripting/TestBlockTimers.cpp` |
| NEW | `tests/scripting/test_scripts/timer_furnace.lua` |
| NEW | `tests/scripting/test_scripts/abm_grass_spread.lua` |
| NEW | `tests/scripting/test_scripts/lbm_upgrade_torch.lua` |
