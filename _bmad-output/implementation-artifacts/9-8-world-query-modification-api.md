# Story 9.8: World Query & Modification API

Status: ready-for-dev

## Story

As a mod developer,
I want Lua scripts to read and modify the voxel world,
so that mods can create dynamic gameplay mechanics (structure detection, block transformations, world inspection).

## Acceptance Criteria

1. **Block Query Functions** — `voxel.get_block(pos)`, `voxel.get_block_info(string_id)`, `voxel.get_block_state(pos)` return correct data from loaded chunks; unloaded chunks return nil
2. **Block Modification Functions** — `voxel.set_block(pos, id)`, `voxel.set_block_state(pos, id, state)`, `voxel.dig_block(pos)`, `voxel.swap_block(pos, id)` modify world state with correct callback chains
3. **Area Search** — `voxel.find_blocks_in_area(p1, p2, filter)` and `voxel.count_blocks_in_area(p1, p2, filter)` support string ID and `"group:name"` matching
4. **Raycasting** — `voxel.raycast(ox, oy, oz, dx, dy, dz, max_dist)` returns hit info or nil
5. **Biome/Lighting/Time** — `voxel.get_biome(x, z)`, `voxel.get_light(pos)`, `voxel.get_time_of_day()`, `voxel.set_time_of_day(float)` work correctly
6. **Scheduled Ticks** — `voxel.schedule_tick(pos, delay_ticks, priority)` and `voxel.set_node_timer_active(pos, bool)` extend the existing timer system
7. **Multiblock Pattern Matching** — `voxel.check_pattern(pos, pattern)`, `voxel.check_box_pattern(p1, p2, id, opts)`, `voxel.check_ring(pos, y_offset, radius, filter)` detect block structures
8. **Settings API** — `voxel.get_setting(name)` / `voxel.set_setting(name, value)` with whitelisted writable settings
9. **Rate Limiting** — Per-tick per-mod counters prevent abuse: `set_block` 1000/tick, `raycast` 100/tick, `find_blocks_in_area` 10/tick, pattern checks 50/tick, `schedule_tick` 500/tick
10. **Coordinate Validation** — All functions return nil for out-of-bounds or unloaded chunk positions
11. **Integration Test** — Lua places a 5x5x5 cube, `find_blocks_in_area` returns 125 positions, beacon pyramid pattern check returns correct power level

## Tasks / Subtasks

- [ ] Task 1: Implement `WorldQueryAPI` class (AC: 1, 2, 10)
  - [ ] 1.1: Create `WorldQueryAPI.h/cpp` with static `registerWorldAPI(lua, chunkManager, blockRegistry)` method
  - [ ] 1.2: Implement `voxel.get_block(pos)` — resolve pos to ChunkManager::getBlock, return string ID via BlockRegistry
  - [ ] 1.3: Implement `voxel.get_block_info(string_id)` — return BlockDefinition fields as Lua table
  - [ ] 1.4: Implement `voxel.get_block_state(pos)` — return BlockRegistry::getStateValues as Lua table
  - [ ] 1.5: Implement `voxel.set_block(pos, block_id)` — resolve name to numeric ID, call ChunkManager::setBlock, fire callback chain (onDestruct old, onConstruct new, updateLightAfterBlockChange)
  - [ ] 1.6: Implement `voxel.set_block_state(pos, block_id, state_table)` — merge state via BlockRegistry::getStateId
  - [ ] 1.7: Implement `voxel.dig_block(pos)` — trigger destruction callbacks as if player broke it
  - [ ] 1.8: Implement `voxel.swap_block(pos, new_id)` — raw setBlock WITHOUT callbacks (for state changes like door open/close)

- [ ] Task 2: Implement area search and raycasting (AC: 3, 4)
  - [ ] 2.1: Implement `voxel.find_blocks_in_area(p1, p2, filter)` — iterate chunk sections in AABB, support string ID and `"group:name"` prefix matching
  - [ ] 2.2: Implement `voxel.count_blocks_in_area(p1, p2, filter)` — same iteration but count-only (no position table allocation)
  - [ ] 2.3: Implement `voxel.raycast(ox, oy, oz, dx, dy, dz, max_dist)` — wrap existing DDA raycast, return `{pos, face, block_id}` or nil

- [ ] Task 3: Implement biome/lighting/time queries (AC: 5)
  - [ ] 3.1: Implement `voxel.get_biome(x, z)` — if WorldGenerator exposes biome data, wrap it; else return stub "unknown"
  - [ ] 3.2: Implement `voxel.get_light(pos)` — read sky and block light from ChunkSection light data
  - [ ] 3.3: Implement `voxel.get_time_of_day()` / `voxel.set_time_of_day(float)` — read/write from game time state

- [ ] Task 4: Extend timer system with scheduled ticks (AC: 6)
  - [ ] 4.1: Add `scheduleTick(pos, delayTicks, priority)` to BlockTimerManager — priority queue ordered by (targetTick, priority)
  - [ ] 4.2: Add `setTimerActive(pos, bool)` to BlockTimerManager — pause/resume without resetting
  - [ ] 4.3: Register `voxel.schedule_tick` and `voxel.set_node_timer_active` in WorldQueryAPI

- [ ] Task 5: Implement multiblock pattern matching (AC: 7)
  - [ ] 5.1: Implement `voxel.check_pattern(pos, entries)` — relative offset matching with early exit
  - [ ] 5.2: Implement `voxel.check_box_pattern(p1, p2, filter, opts)` — volumetric check with `allow_mixed` option
  - [ ] 5.3: Implement `voxel.check_ring(pos, y_offset, radius, filter)` — perimeter check at given Y offset

- [ ] Task 6: Implement settings API and rate limiting (AC: 8, 9)
  - [ ] 6.1: Implement `voxel.get_setting(name)` / `voxel.set_setting(name, value)` with whitelist
  - [ ] 6.2: Create `RateLimiter` struct — per-mod counters, reset on tick, configurable limits per function category
  - [ ] 6.3: Wrap all expensive functions with rate limit checks; exceed returns nil + logs warning

- [ ] Task 7: Wire into GameApp and write tests (AC: 11)
  - [ ] 7.1: Call `WorldQueryAPI::registerWorldAPI` in GameApp after ScriptEngine init
  - [ ] 7.2: Write integration test: Lua places 5x5x5 cube, area search returns 125 positions
  - [ ] 7.3: Write integration test: beacon pyramid pattern detection
  - [ ] 7.4: Write unit test: rate limiter enforcement
  - [ ] 7.5: Write unit test: coordinate validation (out-of-bounds, unloaded chunks)

## Dev Notes

### Architecture Compliance

- **Command Pattern**: `voxel.set_block` and `voxel.dig_block` MUST trigger the full callback chain via `BlockCallbackInvoker` (onDestruct on old block → setBlock → onConstruct on new block → updateLightAfterBlockChange). `voxel.swap_block` is the exception — raw setBlock + light update only, no callbacks.
- **Chunks NOT in ECS** (ADR-004): All world access goes through `ChunkManager`, never ECS queries.
- **No exceptions** (project rule): All Lua-facing functions use `sol::protected_function` pattern and return nil on error. C++ uses `Result<T>` where appropriate.
- **Tick-based simulation** (ADR-010): `schedule_tick` uses game ticks (20/sec), not wall-clock. Rate limits reset per game tick.
- **Rate limiting per mod**: V1 has a single global mod context. Implement rate limiter as a single counter set (not per-mod map). When multi-mod support arrives (9.11), promote to per-mod tracking.

### Existing C++ APIs to Wrap (DO NOT REIMPLEMENT)

| Lua Function | C++ Implementation | File |
|---|---|---|
| `get_block(pos)` | `ChunkManager::getBlock(ivec3)` → returns `uint16_t`, resolve via `BlockRegistry` | `ChunkManager.h:120` |
| `set_block(pos, id)` | `ChunkManager::setBlock(ivec3, uint16_t)` + `updateLightAfterBlockChange` | `ChunkManager.h:124,130` |
| `get_block_info(id)` | `BlockRegistry::getBlockType(uint16_t)` / `getIdByName(string_view)` | `BlockRegistry.h:29,44` |
| `get_block_state(pos)` | `BlockRegistry::getStateValues(uint16_t)` | `BlockRegistry.h:32` |
| `set_block_state` | `BlockRegistry::getStateId(uint16_t, StateMap)` → `ChunkManager::setBlock` | `BlockRegistry.h:35` |
| `raycast` | Existing DDA raycast in `physics/` | `engine/include/voxel/physics/` |
| `get_light(pos)` | `ChunkSection::getBlockLight` / `getSkylightLevel` | `ChunkColumn.h` / `ChunkSection.h` |

### Callback Chain for `voxel.set_block(pos, block_id)`

```
1. Resolve block_id string → numeric ID via BlockRegistry::getIdByName
2. If unloaded chunk → return nil
3. Get old block ID at pos via ChunkManager::getBlock
4. If old block has callbacks → invokeOnDestruct(oldDef, pos)
5. ChunkManager::setBlock(pos, newId)  // marks dirty, triggers remesh
6. ChunkManager::updateLightAfterBlockChange(pos, &oldDef, &newDef)
7. If new block has callbacks → invokeOnConstruct(newDef, pos)
8. Return true
```

### Callback Chain for `voxel.dig_block(pos)`

```
1. Get current block at pos
2. If air or unloaded → return false
3. invokeCanDig(def, pos, 0)  // playerId 0 = scripted
4. invokeOnDestruct(def, pos)
5. ChunkManager::setBlock(pos, BLOCK_AIR)
6. updateLightAfterBlockChange(pos, &def, nullptr)
7. invokeAfterDestruct(def, pos, oldId)
8. Return true
```

### Pattern Matching Algorithms

**`check_pattern`**: O(N) where N = pattern entries. For each entry: worldPos + offset → getBlock → compare. Early exit on first mismatch.

**`check_box_pattern`**: Iterate all positions in AABB. For each: getBlock → compare against filter. Early exit on mismatch (unless `allow_mixed`). Clamp to loaded chunks — skip unloaded sections.

**`check_ring`**: Iterate the 4 edges of a square ring at `(pos.x ± radius, pos.y + y_offset, pos.z ± radius)`. Count matches and check completeness.

**Group matching**: Filter strings starting with `"group:"` → extract group name → check `BlockDefinition::groups` map. All other filters are exact string ID match via `BlockRegistry::getIdByName`.

### Rate Limiter Design

```cpp
struct RateLimiter {
    static constexpr int SET_BLOCK_LIMIT = 1000;
    static constexpr int RAYCAST_LIMIT = 100;
    static constexpr int FIND_AREA_LIMIT = 10;
    static constexpr int PATTERN_LIMIT = 50;
    static constexpr int SCHEDULE_LIMIT = 500;

    int setBlockCount = 0;
    int raycastCount = 0;
    int findAreaCount = 0;
    int patternCount = 0;
    int scheduleCount = 0;

    void resetTick() { setBlockCount = raycastCount = findAreaCount = patternCount = scheduleCount = 0; }
    bool checkSetBlock() { return ++setBlockCount <= SET_BLOCK_LIMIT; }
    // ... same pattern for each category
};
```

Reset `RateLimiter::resetTick()` at the start of each game tick in GameApp, before timer/ABM/LBM updates.

### Scheduled Tick Extension to BlockTimerManager

Add a separate priority queue alongside the existing `m_timers` map:

```cpp
struct ScheduledTick {
    glm::ivec3 pos;
    uint64_t targetTick;  // absolute game tick number
    int priority;         // lower = higher priority (executed first)
    uint16_t blockId;
};
// Ordered by (targetTick ASC, priority ASC)
std::vector<ScheduledTick> m_scheduledTicks; // kept sorted, or use std::priority_queue
```

`update()` checks `m_scheduledTicks` front: if `currentTick >= targetTick`, pop and invoke `on_timer`. This is separate from wall-clock timers — it fires on exact game ticks for deterministic behavior.

### File Structure

**New files (4):**
- `engine/include/voxel/scripting/WorldQueryAPI.h`
- `engine/src/scripting/WorldQueryAPI.cpp`
- `tests/scripting/TestWorldQueryAPI.cpp`
- `tests/scripting/test_scripts/world_query_test.lua`

**Modified files (4):**
- `engine/include/voxel/scripting/BlockTimerManager.h` — add `scheduleTick`, `setTimerActive`, scheduled tick queue
- `engine/src/scripting/BlockTimerManager.cpp` — implement scheduled tick logic
- `engine/include/voxel/scripting/LuaBindings.h` — forward declare WorldQueryAPI (or keep separate)
- `game/src/GameApp.cpp` — call `WorldQueryAPI::registerWorldAPI`, reset rate limiter per tick

### Naming Conventions (from project-context.md)

- Classes: `WorldQueryAPI`, `RateLimiter`, `ScheduledTick` (PascalCase)
- Methods: `registerWorldAPI`, `resetTick`, `checkSetBlock` (camelCase)
- Members: `m_scheduledTicks`, `m_rateLimiter` (m_ prefix)
- Constants: `SET_BLOCK_LIMIT`, `RAYCAST_LIMIT` (SCREAMING_SNAKE)
- Namespace: `voxel::scripting`
- Files: `WorldQueryAPI.h` / `WorldQueryAPI.cpp` (PascalCase)

### What NOT To Do

- Do NOT put sol2 headers in ChunkManager.h or BlockRegistry.h — keep sol2 isolated to scripting/
- Do NOT implement chunk loading/unloading from Lua (not in scope)
- Do NOT implement entity queries (future story)
- Do NOT implement inventory/metadata queries here (done in 9.7)
- Do NOT create new ECS components for any of this
- Do NOT use `std::shared_ptr` for RateLimiter (simple value type, owned by WorldQueryAPI or GameApp)
- Do NOT implement formspec/UI (not in scope)
- Do NOT modify ScriptEngine — all new bindings go through WorldQueryAPI static methods
- Do NOT re-implement DDA raycasting — wrap the existing physics implementation

### Dependencies

- **Story 9.2** (done): Block registration, BlockCallbackInvoker, callback chain
- **Story 9.4** (in-progress): BlockTimerManager — extend with scheduled ticks
- **Story 9.5** (ready-for-dev): Neighbor callbacks — `set_block` should eventually trigger neighbor change notifications
- **Epic 3** (done): ChunkManager, BlockRegistry, ChunkColumn, block state system

### Previous Story Intelligence

**From 9.7 (Metadata & Inventory):**
- Established pattern: separate API class with static `registerXxxAPI(lua, ...)` method
- ChunkColumn sparse storage for per-block data (metadata/inventory maps)
- BinaryIO extracted from ChunkSerializer for shared use
- CategoryMask bits: 0x01=placement, 0x02=destruction, 0x04=interaction, 0x08=timer, 0x10=inventory

**From 9.6 (Entity Callbacks):**
- Callback invocation pattern: `has_value()` → `protected_function_result` → `.valid()` → log + safe default
- `posToTable(lua, pos)` utility in BlockCallbackInvoker for converting ivec3 → Lua table
- EntityHandle as lightweight usertype wrapper — same pattern can apply for future world handle

**From 9.4 (Timers):**
- BlockTimerManager owns timer state, separate from ChunkColumn
- Pending timer queue for re-entrant timer sets during update iteration
- Timer fire: check block still exists at pos, invoke on_timer, restart if returns true

### Git Intelligence

Recent commits follow pattern: `feat(scripting): <description>`. Commit scope is always `scripting` for Epic 9 work. Each story is a single commit with all new + modified files.

### Testing Strategy

**Integration tests** (Catch2 v3, `[scripting][world-query]` tags):
1. Block query roundtrip: register block → set_block via Lua → get_block returns correct ID
2. Area search: place 5x5x5 cube → find_blocks_in_area returns 125 positions
3. Count shortcut: count_blocks_in_area returns 125 without position allocation
4. Beacon pattern: build 4-layer pyramid → check_ring returns correct completeness per layer
5. Swap block: swap_block does NOT fire callbacks (verify via callback counter)
6. Dig block: dig_block fires destruction chain (verify via callback counter)
7. Rate limiting: exceed set_block limit → returns nil after 1000 calls
8. Out-of-bounds: get_block at unloaded chunk pos → returns nil
9. State query: set_block_state with facing=north → get_block_state returns {facing="north"}

**Test Lua scripts** in `tests/scripting/test_scripts/`:
- `world_query_test.lua` — exercises all API functions with assertions

### Project Structure Notes

All new code follows existing structure:
- Public headers: `engine/include/voxel/scripting/`
- Implementation: `engine/src/scripting/`
- Tests: `tests/scripting/`
- Test scripts: `tests/scripting/test_scripts/`

WorldQueryAPI follows the same static registration pattern as LuaBindings::registerBlockAPI, LuaBindings::registerTimerAPI, etc.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.8]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 9: Scripting, ADR-004, ADR-007, ADR-010]
- [Source: _bmad-output/project-context.md — Critical Implementation Rules]
- [Source: engine/include/voxel/world/ChunkManager.h — getBlock/setBlock/updateLightAfterBlockChange API]
- [Source: engine/include/voxel/world/BlockRegistry.h — getIdByName/getBlockType/getStateValues/withProperty API]
- [Source: engine/include/voxel/scripting/BlockCallbackInvoker.h — invoke pattern]
- [Source: engine/include/voxel/scripting/BlockTimerManager.h — timer management pattern]
- [Source: _bmad-output/implementation-artifacts/9-7-metadata-inventory-callbacks.md — previous story patterns]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
