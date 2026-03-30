# Story 9.5: Neighbor Change + Physics Shape Callbacks

Status: ready-for-dev

## Story

As a developer,
I want blocks to react when adjacent blocks change and to provide dynamic collision/selection shapes,
so that mods can create blocks that depend on their surroundings (torches, doors, fences, rails).

## Acceptance Criteria

1. `on_neighbor_changed(pos, neighbor_pos, neighbor_node)` callback in `BlockCallbacks` — fires when any of the 6 adjacent blocks changes. Receives the changed block's position and its new block string ID.
2. `update_shape(pos, direction, neighbor_state) -> state_table|nil` callback — allows blocks to compute a new block state based on a neighbor change (for connected blocks like fences). Return nil = no change.
3. `can_survive(pos) -> bool` callback — checked after `on_neighbor_changed`. If returns false, the block is destroyed (calls `dig_block` internally, triggering full destruction chain).
4. When `ChunkManager::setBlock()` changes a block, all 6 adjacent blocks' `on_neighbor_changed` callbacks fire AFTER the block change and lighting update complete. Neighbor offsets: `{+1,0,0}`, `{-1,0,0}`, `{0,+1,0}`, `{0,-1,0}`, `{0,0,+1}`, `{0,0,-1}`.
5. After `on_neighbor_changed`, if the block has `can_survive` and it returns false, the block is destroyed. Cascade depth capped at 64 to prevent infinite recursion (e.g., chain of torches all losing support).
6. `get_collision_shape(pos) -> table of boxes` callback — returns a list of `{x1,y1,z1, x2,y2,z2}` AABB boxes used for physics collision. Replaces the default full-block AABB when defined.
7. `get_selection_shape(pos) -> table of boxes` callback — returns custom AABB boxes for DDA raycasting targeting. When defined, `Raycast.cpp` tests the ray against these boxes instead of the full-block AABB.
8. `can_attach_at(pos, face) -> bool` callback — used by other blocks (e.g., torches) to check if they can attach to a specific face of this block. Face is a string: `"top"`, `"bottom"`, `"north"`, `"south"`, `"east"`, `"west"`.
9. `is_pathfindable(pos, pathtype) -> bool` callback — stub for future AI pathfinding. Stored in callbacks but not wired to any system in V1.
10. Signal/power callbacks are **stubs only**: `on_powered(pos, power_level, source_pos)`, `get_comparator_output(pos) -> int`, `get_push_reaction(pos) -> string`. Registered and stored in `BlockCallbacks` but not fired by any engine system in V1. Callable from Lua scripts manually.
11. Dynamic shape caching: collision and selection shapes are cached per block position. Cache entry invalidated when the block itself changes OR any neighbor changes (dirty flag set during `on_neighbor_changed` dispatch).
12. Default shapes: if `get_collision_shape` is nil, solid blocks use full-block `{0,0,0, 1,1,1}`, air/non-collision blocks use empty. If `get_selection_shape` is nil, fall back to `get_collision_shape` result, then to full-block default.
13. Integration test: register torch with `can_survive` checking block below is solid, break support block, verify torch is also destroyed.
14. Integration test: register slab with `get_collision_shape` returning half-height box, verify collision system uses half-height AABB.
15. Integration test: register block with `on_neighbor_changed`, change an adjacent block, verify callback fires with correct positions.

## Tasks / Subtasks

- [ ] Task 1: Add neighbor + shape + signal callback fields to BlockCallbacks (AC: 1-3, 6-10)
  - [ ] 1.1 In `engine/include/voxel/scripting/BlockCallbacks.h`, add to `BlockCallbacks` struct:
    ```cpp
    // Neighbor change (3)
    std::optional<sol::protected_function> onNeighborChanged;     // (pos, neighbor_pos, neighbor_node)
    std::optional<sol::protected_function> updateShape;           // (pos, direction, neighbor_state) -> state|nil
    std::optional<sol::protected_function> canSurvive;            // (pos) -> bool

    // Physics/collision shape (4)
    std::optional<sol::protected_function> getCollisionShape;     // (pos) -> table of {x1,y1,z1,x2,y2,z2}
    std::optional<sol::protected_function> getSelectionShape;     // (pos) -> table of {x1,y1,z1,x2,y2,z2}
    std::optional<sol::protected_function> canAttachAt;           // (pos, face_string) -> bool
    std::optional<sol::protected_function> isPathfindable;        // (pos, pathtype) -> bool

    // Signal/power stubs (3)
    std::optional<sol::protected_function> onPowered;             // (pos, power_level, source_pos)
    std::optional<sol::protected_function> getComparatorOutput;   // (pos) -> int (0-15)
    std::optional<sol::protected_function> getPushReaction;       // (pos) -> string
    ```
  - [ ] 1.2 Verify `BlockCallbacks` remains movable (all sol::protected_function are movable)

- [ ] Task 2: Extract new callbacks in LuaBindings (AC: 1-3, 6-10)
  - [ ] 2.1 In `parseBlockDefinition()` in `LuaBindings.cpp`, add extraction for all 10 new callbacks using the same pattern as existing callbacks:
    ```cpp
    callbacks->onNeighborChanged = table.get<std::optional<sol::protected_function>>("on_neighbor_changed");
    callbacks->updateShape = table.get<std::optional<sol::protected_function>>("update_shape");
    callbacks->canSurvive = table.get<std::optional<sol::protected_function>>("can_survive");
    callbacks->getCollisionShape = table.get<std::optional<sol::protected_function>>("get_collision_shape");
    callbacks->getSelectionShape = table.get<std::optional<sol::protected_function>>("get_selection_shape");
    callbacks->canAttachAt = table.get<std::optional<sol::protected_function>>("can_attach_at");
    callbacks->isPathfindable = table.get<std::optional<sol::protected_function>>("is_pathfindable");
    callbacks->onPowered = table.get<std::optional<sol::protected_function>>("on_powered");
    callbacks->getComparatorOutput = table.get<std::optional<sol::protected_function>>("get_comparator_output");
    callbacks->getPushReaction = table.get<std::optional<sol::protected_function>>("get_push_reaction");
    ```

- [ ] Task 3: Add invoker methods to BlockCallbackInvoker (AC: 1-3, 6-9)
  - [ ] 3.1 Declare and implement in `BlockCallbackInvoker.h/.cpp`:
    - `void invokeOnNeighborChanged(const BlockDefinition& def, const glm::ivec3& pos, const glm::ivec3& neighborPos, const std::string& neighborNode)`
    - `std::optional<sol::object> invokeUpdateShape(const BlockDefinition& def, const glm::ivec3& pos, const std::string& direction, sol::object neighborState)`
    - `bool invokeCanSurvive(const BlockDefinition& def, const glm::ivec3& pos)` — default `true` if nil
    - `std::vector<math::AABB> invokeGetCollisionShape(const BlockDefinition& def, const glm::ivec3& pos)` — returns empty vector if nil (caller uses default)
    - `std::vector<math::AABB> invokeGetSelectionShape(const BlockDefinition& def, const glm::ivec3& pos)` — returns empty vector if nil
    - `bool invokeCanAttachAt(const BlockDefinition& def, const glm::ivec3& pos, const std::string& face)` — default `true` for solid blocks, `false` for non-solid
  - [ ] 3.2 Shape parsing helper: `std::vector<math::AABB> parseBoxList(const sol::table& result)` — iterates Lua table of `{x1,y1,z1,x2,y2,z2}` sub-tables, converts each 6-element table to `math::AABB{min, max}`
  - [ ] 3.3 Log all callback errors with block stringId: `VX_LOG_WARN("Lua <callback_name> error for '{}': {}", def.stringId, err.what())`
  - [ ] 3.4 Follow existing invocation pattern: check `def.callbacks && def.callbacks->X.has_value()` → call → check `.valid()` → parse return → default on error

- [ ] Task 4: Create NeighborNotifier system (AC: 4, 5)
  - [ ] 4.1 Create `engine/include/voxel/scripting/NeighborNotifier.h`
  - [ ] 4.2 Create `engine/src/scripting/NeighborNotifier.cpp`
  - [ ] 4.3 Class design:
    ```cpp
    namespace voxel::scripting
    {
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

    private:
        void notifySingleNeighbor(
            const glm::ivec3& neighborPos,
            const glm::ivec3& changedPos,
            int currentDepth);

        static constexpr int MAX_CASCADE_DEPTH = 64;

        world::ChunkManager& m_chunkManager;
        world::BlockRegistry& m_registry;
        BlockCallbackInvoker& m_invoker;
        int m_currentCascadeDepth = 0;
    };
    }
    ```
  - [ ] 4.4 `notifyNeighbors()` implementation:
    - Static array of 6 offsets: `{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}`
    - For each offset: compute `neighborPos = changedPos + offset`
    - Y-bounds check: skip if `neighborPos.y < 0` or `>= COLUMN_HEIGHT`
    - Get blockId at neighborPos via `ChunkManager::getBlock()`
    - If blockId != AIR, look up `BlockDefinition`
    - Get changedBlockId string for the block that changed: `registry.getBlockType(chunkManager.getBlock(changedPos)).stringId`
    - Call `invoker.invokeOnNeighborChanged(def, neighborPos, changedPos, changedBlockString)`
    - Call `invoker.invokeUpdateShape(def, neighborPos, ...)` if defined
    - Call `invoker.invokeCanSurvive(def, neighborPos)` — if false, destroy block via `digBlock()` helper
  - [ ] 4.5 Cascade protection: increment `m_currentCascadeDepth` before `digBlock()`, decrement after. If depth >= `MAX_CASCADE_DEPTH`, log warning and skip further destruction. Use RAII guard for depth counter.
  - [ ] 4.6 `digBlock()` helper: sets block to AIR via `ChunkManager::setBlock()`, fires destruction callbacks via invoker (same sequence as BreakBlock command: `onDestruct` → setBlock(AIR) → `afterDestruct`), then recursively calls `notifyNeighbors()` on the newly-air position. No player context (digger = nil).
  - [ ] 4.7 Direction string mapping for `update_shape`: offset `{1,0,0}` = `"east"`, `{-1,0,0}` = `"west"`, `{0,1,0}` = `"up"`, `{0,-1,0}` = `"down"`, `{0,0,1}` = `"south"`, `{0,0,-1}` = `"north"`

- [ ] Task 5: Create ShapeCache for dynamic collision/selection shapes (AC: 6, 7, 11, 12)
  - [ ] 5.1 Create `engine/include/voxel/scripting/ShapeCache.h`
  - [ ] 5.2 Create `engine/src/scripting/ShapeCache.cpp`
  - [ ] 5.3 Class design:
    ```cpp
    namespace voxel::scripting
    {
    struct CachedShape
    {
        std::vector<math::AABB> collisionBoxes;
        std::vector<math::AABB> selectionBoxes;
        bool dirty = true;
    };

    class ShapeCache
    {
    public:
        ShapeCache(
            world::BlockRegistry& registry,
            BlockCallbackInvoker& invoker);

        /// Get collision boxes for block at pos. Queries Lua callback if dirty/missing.
        std::span<const math::AABB> getCollisionShape(
            const glm::ivec3& pos,
            uint16_t blockId);

        /// Get selection boxes for block at pos. Falls back to collision shape, then default.
        std::span<const math::AABB> getSelectionShape(
            const glm::ivec3& pos,
            uint16_t blockId);

        /// Invalidate cached shape at pos (call when block or neighbor changes).
        void invalidate(const glm::ivec3& pos);

        /// Invalidate all entries (e.g., on hot-reload).
        void clear();

    private:
        math::AABB makeDefaultBox(const glm::ivec3& pos) const;

        world::BlockRegistry& m_registry;
        BlockCallbackInvoker& m_invoker;
        std::unordered_map<glm::ivec3, CachedShape, math::IVec3Hash> m_cache;

        // Default full-block shape reusable buffer
        std::vector<math::AABB> m_defaultShape;
    };
    }
    ```
  - [ ] 5.4 `getCollisionShape()` logic:
    - Check cache at pos. If present and `!dirty`, return cached boxes.
    - Look up `BlockDefinition` via blockId. If no callbacks or no `getCollisionShape`, return default shape (full block for solid, empty for non-collision).
    - Call `invoker.invokeGetCollisionShape(def, pos)`. If returns non-empty, store in cache. If returns empty (callback returned nil or error), use default.
    - Mark `dirty = false`, return cached result.
  - [ ] 5.5 `getSelectionShape()` logic:
    - Same as collision but try `getSelectionShape` callback first.
    - If nil/empty, fall back to `getCollisionShape()` result for this position.
    - If that's also empty/default, use full-block default.
  - [ ] 5.6 `invalidate()`: if pos exists in cache, set `dirty = true`. Don't erase — avoids reallocation. Also invalidate the 6 neighbors (connected blocks may change shape based on neighbors).
  - [ ] 5.7 Cache eviction: optional — if cache grows beyond a threshold (e.g., 100,000 entries), remove oldest entries. For V1, simply clearing on chunk unload is sufficient.
  - [ ] 5.8 AABB coordinate system: cached boxes are in **block-local** coordinates (0,0,0 to 1,1,1). The consumer (collision system, raycast) must offset them to world coordinates: `worldBox = localBox + glm::vec3(blockPos)`.

- [ ] Task 6: Integrate dynamic shapes into collision system (AC: 6, 12)
  - [ ] 6.1 In `PlayerController.cpp`, modify `collectSolidBlocks()` to use `ShapeCache`:
    - Current code creates full-block `AABB{x, y, z, x+1, y+1, z+1}` for every solid block
    - New code: query `shapeCache.getCollisionShape(blockPos, blockId)` → iterate returned boxes → offset each to world coordinates → push to candidates
    - If `getCollisionShape` returns multiple boxes (e.g., stair has 2 boxes), push ALL boxes
  - [ ] 6.2 Pass `ShapeCache&` to `PlayerController` (constructor injection or method parameter)
  - [ ] 6.3 Signature change for `collectSolidBlocks()`:
    ```cpp
    static std::vector<math::AABB> collectSolidBlocks(
        const math::AABB& volume,
        world::ChunkManager& world,
        const world::BlockRegistry& registry,
        scripting::ShapeCache& shapeCache);
    ```
  - [ ] 6.4 Update `resolveCollisions()` and `resolveAxis()` to pass `ShapeCache&` through to `collectSolidBlocks()`
  - [ ] 6.5 For blocks WITHOUT `get_collision_shape` callback: behavior is unchanged (full-block AABB). Only blocks with the callback get custom shapes.

- [ ] Task 7: Integrate dynamic shapes into raycast system (AC: 7, 12)
  - [ ] 7.1 In `Raycast.cpp`, modify the hit-test logic to support custom selection shapes:
    - Current: checks `def.hasCollision` → returns hit at block boundary crossing
    - New: after entering a block with `hasCollision`, check if block has custom selection shape
    - If custom shape: test ray against each AABB in the selection shape list (ray-AABB intersection)
    - If any box is hit: return the closest hit point as the result
    - If no box is hit: continue DDA traversal (block has collision but the ray misses the custom shape)
  - [ ] 7.2 Ray-AABB intersection helper (if not already in `math/AABB.h`):
    ```cpp
    /// Test ray against an AABB. Returns true if hit, sets tMin to entry distance.
    bool rayIntersectsAABB(
        const glm::vec3& origin,
        const glm::vec3& invDirection,
        const math::AABB& box,
        float& tMin);
    ```
  - [ ] 7.3 Updated `raycast()` signature to accept `ShapeCache&`:
    ```cpp
    RaycastResult raycast(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        const world::ChunkManager& world,
        const world::BlockRegistry& registry,
        scripting::ShapeCache* shapeCache = nullptr); // nullptr = use default shapes (backward compat)
    ```
  - [ ] 7.4 Backward compatibility: if `shapeCache` is nullptr, use the existing full-block check. Callers that don't have a ShapeCache (e.g., tests) still work.
  - [ ] 7.5 Selection shapes are in block-local coordinates — offset to world coords before ray test.

- [ ] Task 8: Wire NeighborNotifier into block change pipeline (AC: 4)
  - [ ] 8.1 Add `NeighborNotifier` member to `GameApp` (or wherever block commands are processed)
  - [ ] 8.2 After EVERY `ChunkManager::setBlock()` call + `updateLightAfterBlockChange()`, call `neighborNotifier.notifyNeighbors(changedPos)`
  - [ ] 8.3 This includes: PlaceBlock command, BreakBlock command, any Lua `voxel.set_block()` call, and cascade destructions from `can_survive` failures
  - [ ] 8.4 Wire `ShapeCache::invalidate(changedPos)` alongside neighbor notification — when a block changes, invalidate its shape and neighbors' shapes
  - [ ] 8.5 Also invalidate shapes from within `NeighborNotifier::notifySingleNeighbor()` — when notifying a neighbor, invalidate that neighbor's shape cache entry

- [ ] Task 9: Add `voxel.dig_block` and `voxel.swap_block` Lua API helpers (AC: 5)
  - [ ] 9.1 `voxel.dig_block(pos)` — triggers full destruction callback chain (same as BreakBlock command but with no player context). Used by `can_survive` cascade.
  - [ ] 9.2 `voxel.swap_block(pos, new_id)` — changes block WITHOUT triggering place/break callbacks. Used for state changes like door open/close. Fires `setBlock` + lighting + neighbor notify, but skips `onDestruct`/`onConstruct` chain.
  - [ ] 9.3 Bind both in `LuaBindings::registerWorldAPI(sol::state&, ...)`
  - [ ] 9.4 `dig_block` internally: look up block → `invokeOnDestruct` → `setBlock(AIR)` → `updateLight` → `invokeAfterDestruct` → `notifyNeighbors` → publish `BlockBroken` event
  - [ ] 9.5 `swap_block` internally: `setBlock(newId)` → `updateLight` → `notifyNeighbors` → publish `BlockChanged` event (NOT `BlockPlaced`/`BlockBroken`)

- [ ] Task 10: Integration tests (AC: 13, 14, 15)
  - [ ] 10.1 Create `tests/scripting/TestNeighborCallbacks.cpp`
  - [ ] 10.2 Test: register block with `on_neighbor_changed`, change adjacent block, verify callback fires with correct `pos`, `neighbor_pos`, `neighbor_node`
  - [ ] 10.3 Test: register torch with `can_survive` returning `voxel.get_block(pos.x, pos.y-1, pos.z).solid`, break block below, verify torch is destroyed
  - [ ] 10.4 Test: cascade limit — create chain of 70 blocks each with `can_survive` depending on the next one, break first, verify cascade stops at 64
  - [ ] 10.5 Test: `on_neighbor_changed` NOT fired for the block itself (only its 6 neighbors)
  - [ ] 10.6 Create `tests/scripting/TestShapeCallbacks.cpp`
  - [ ] 10.7 Test: register slab with `get_collision_shape` returning `{{0,0,0, 1,0.5,1}}`, verify `ShapeCache::getCollisionShape` returns half-height AABB
  - [ ] 10.8 Test: register block without shape callbacks, verify `ShapeCache` returns full-block default
  - [ ] 10.9 Test: `ShapeCache::invalidate` clears dirty flag, next query re-invokes callback
  - [ ] 10.10 Test: `get_selection_shape` nil → falls back to `get_collision_shape` result
  - [ ] 10.11 Test: `can_attach_at` returns correct value for specified face string
  - [ ] 10.12 Create test Lua scripts in `tests/scripting/test_scripts/`:
    - `test_neighbor_changed.lua` — registers block that records callback invocations
    - `test_can_survive.lua` — registers torch that checks support block
    - `test_collision_shape.lua` — registers slab with half-height collision
    - `test_selection_shape.lua` — registers block with custom selection boxes

- [ ] Task 11: Build integration (AC: all)
  - [ ] 11.1 Add `NeighborNotifier.cpp`, `ShapeCache.cpp` to `engine/CMakeLists.txt`
  - [ ] 11.2 Add `TestNeighborCallbacks.cpp`, `TestShapeCallbacks.cpp` to `tests/CMakeLists.txt`
  - [ ] 11.3 Build full project, verify zero warnings under `/W4 /WX`
  - [ ] 11.4 Run all tests (existing + new), verify zero regressions

## Dev Notes

### Architecture Compliance

- **Exceptions disabled** — all callback invocations use `sol::protected_function` + `.valid()` check. Never `sol::function`.
- **Result<T> pattern** — use `std::expected<T, EngineError>` for fallible operations, not exceptions.
- **No direct state mutation** — `dig_block` and `swap_block` go through proper command/event pipelines.
- **One class per file** — `NeighborNotifier`, `ShapeCache` each get their own `.h/.cpp` pair.
- **Naming conventions** — classes PascalCase, methods camelCase, members `m_` prefix, constants SCREAMING_SNAKE.
- **Chunks NOT in ECS** — all block queries go through `ChunkManager`, never through EnTT.

### Critical Design Decisions

**Neighbor notifications fire AFTER setBlock + lighting, not during:**
The sequence for any block change must be:
1. `setBlock()` — block data changes
2. `updateLightAfterBlockChange()` — lighting propagates
3. `neighborNotifier.notifyNeighbors()` — Lua callbacks fire

This ensures that when a neighbor callback queries the world (e.g., `voxel.get_block()`), it sees the correct post-change state including updated lighting.

**Cascade depth limit is per-notification-chain, not global:**
Use a depth counter on `NeighborNotifier` that increments/decrements as destructions cascade. This allows independent chains to each use up to 64, but a single chain can't exceed 64. Reset depth to 0 when the top-level `notifyNeighbors()` returns.

**ShapeCache uses block-local coordinates (0 to 1):**
All cached AABBs are in block-local space. The consumer (collision, raycast) offsets to world space. This keeps the cache position-independent for blocks with the same shape regardless of location (future optimization: cache by blockId + state instead of position).

**Raycast backward compatibility:**
The `shapeCache` parameter is a nullable pointer with default nullptr. This preserves all existing raycast call sites unchanged. Only the GameApp/PlayerController path passes a real ShapeCache.

### Existing Code Integration Points

**`collectSolidBlocks()` in `engine/src/game/PlayerController.cpp:362-393`:**
Currently builds full-block AABBs. Lines 390-392 create `AABB{float(x), float(y), float(z), float(x+1), float(y+1), float(z+1)}`. Replace this with ShapeCache query. The rest of the axis-clipping algorithm in `resolveAxis()` (lines 409-490) works unchanged — it operates on `std::vector<math::AABB>` regardless of where they came from.

**`raycast()` in `engine/src/physics/Raycast.cpp:16-80`:**
Currently checks `def.hasCollision` at line 74 and returns hit at block boundary. For custom selection shapes, after the DDA enters a block, test the ray against each selection box. If no box is hit, continue traversal. This requires computing ray-AABB intersection within the block.

**`ChunkManager::setBlock()` in `engine/src/world/ChunkManager.cpp:47-111`:**
Already marks neighbor sections dirty for meshing. The neighbor callback notification is a NEW call that happens AFTER this function and `updateLightAfterBlockChange()`. Do NOT modify `setBlock()` itself — add the notification call at the call site (wherever `setBlock` is invoked: command processor, dig_block, swap_block, etc.).

**`BlockCallbackInvoker` pattern from Story 9.2:**
All invoker methods follow this exact pattern:
```cpp
bool BlockCallbackInvoker::invokeCanSurvive(
    const BlockDefinition& def,
    const glm::ivec3& pos)
{
    if (!def.callbacks || !def.callbacks->canSurvive.has_value())
        return true; // Default: can survive

    sol::protected_function_result result = (*def.callbacks->canSurvive)(posToTable(pos));

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_survive error for '{}': {}", def.stringId, err.what());
        return true; // Default on error: don't destroy
    }

    return result.get_type() == sol::type::boolean
        ? result.get<bool>()
        : true;
}
```

**`IVec3Hash` from Story 9.4:**
Already defined in `engine/include/voxel/math/MathTypes.h` — reuse for `ShapeCache`'s `unordered_map`.

### Box List Parsing Pattern

The Lua callback returns a table of tables. Each inner table has 6 numeric elements:

```lua
get_collision_shape = function(pos)
    return {
        { 0, 0, 0, 1, 0.5, 1 },       -- Bottom half slab
    }
end
```

C++ parsing:
```cpp
std::vector<math::AABB> BlockCallbackInvoker::parseBoxList(const sol::table& table)
{
    std::vector<math::AABB> boxes;
    for (auto& [key, value] : table)
    {
        if (value.get_type() != sol::type::table)
            continue;

        sol::table box = value.as<sol::table>();
        if (box.size() < 6)
            continue;

        math::AABB aabb;
        aabb.min.x = box[1].get_or(0.0f);
        aabb.min.y = box[2].get_or(0.0f);
        aabb.min.z = box[3].get_or(0.0f);
        aabb.max.x = box[4].get_or(1.0f);
        aabb.max.y = box[5].get_or(1.0f);
        aabb.max.z = box[6].get_or(1.0f);
        boxes.push_back(aabb);
    }
    return boxes;
}
```

Note: Lua tables are 1-indexed. Use `box[1]` through `box[6]`, NOT `box[0]` through `box[5]`.

### Ray-AABB Intersection

Standard slab method for ray vs axis-aligned box:

```cpp
bool rayIntersectsAABB(
    const glm::vec3& origin,
    const glm::vec3& invDir,
    const math::AABB& box,
    float& tMin)
{
    float t1 = (box.min.x - origin.x) * invDir.x;
    float t2 = (box.max.x - origin.x) * invDir.x;
    float t3 = (box.min.y - origin.y) * invDir.y;
    float t4 = (box.max.y - origin.y) * invDir.y;
    float t5 = (box.min.z - origin.z) * invDir.z;
    float t6 = (box.max.z - origin.z) * invDir.z;

    float tNear = std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
    float tFar = std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

    if (tNear > tFar || tFar < 0.0f)
        return false;

    tMin = tNear >= 0.0f ? tNear : tFar;
    return true;
}
```

Place this in `engine/include/voxel/math/AABB.h` or `engine/src/physics/Raycast.cpp` as a static helper.

### File Structure

New files:
```
engine/include/voxel/scripting/NeighborNotifier.h   (NEW)
engine/src/scripting/NeighborNotifier.cpp            (NEW)
engine/include/voxel/scripting/ShapeCache.h          (NEW)
engine/src/scripting/ShapeCache.cpp                  (NEW)
tests/scripting/TestNeighborCallbacks.cpp            (NEW)
tests/scripting/TestShapeCallbacks.cpp               (NEW)
tests/scripting/test_scripts/test_neighbor_changed.lua  (NEW)
tests/scripting/test_scripts/test_can_survive.lua       (NEW)
tests/scripting/test_scripts/test_collision_shape.lua   (NEW)
tests/scripting/test_scripts/test_selection_shape.lua   (NEW)
```

Modified files:
```
engine/include/voxel/scripting/BlockCallbacks.h      (ADD 10 callback fields)
engine/src/scripting/LuaBindings.cpp                 (ADD extraction for 10 callbacks + dig_block/swap_block APIs)
engine/include/voxel/scripting/BlockCallbackInvoker.h (ADD 6 invoke methods + parseBoxList)
engine/src/scripting/BlockCallbackInvoker.cpp        (ADD implementations)
engine/src/game/PlayerController.cpp                 (MODIFY collectSolidBlocks to use ShapeCache)
engine/include/voxel/game/PlayerController.h         (ADD ShapeCache& member/parameter)
engine/src/physics/Raycast.cpp                       (ADD custom selection shape support)
engine/include/voxel/physics/Raycast.h               (ADD optional ShapeCache* parameter)
engine/CMakeLists.txt                                (ADD new .cpp files)
tests/CMakeLists.txt                                 (ADD new test files)
```

### Dependencies on Prior Stories

This story depends on Stories 9.1-9.4 being complete:
- **9.1**: `ScriptEngine` with sol2 + LuaJIT (DONE)
- **9.2**: `BlockCallbacks` struct, `LuaBindings`, `BlockCallbackInvoker`, `voxel.register_block` (must be done first)
- **9.3**: Interaction callbacks, `InteractionState` (independent — no overlap with 9.5)
- **9.4**: `BlockTimerManager`, `ABMRegistry`, `IVec3Hash` in MathTypes.h (must be done first for IVec3Hash)

If 9.2 and 9.4 are not yet implemented when this story starts, the dev agent must implement the prerequisite infrastructure (BlockCallbacks struct, BlockCallbackInvoker, IVec3Hash) as part of this story, following patterns from their specs.

### Testing Standards

- Use Catch2 v3 with BDD-style `GIVEN/WHEN/THEN` or `SECTION` blocks
- Each test creates a minimal `sol::state`, registers test blocks via Lua script, and verifies callback behavior
- For collision/raycast tests: create a mock ChunkManager or use the real one with a small test world
- No GPU tests — all tests are CPU-side logic only
- Pattern from `tests/scripting/TestScriptEngine.cpp`: create `ScriptEngine`, load test script, verify behavior

### Project Structure Notes

- All scripting headers in `engine/include/voxel/scripting/`
- All scripting implementations in `engine/src/scripting/`
- All scripting tests in `tests/scripting/`
- Test Lua scripts in `tests/scripting/test_scripts/`
- Follows existing project structure exactly — no deviations

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md#Story 9.5] — Acceptance criteria, callback signatures, C++ engine support requirements
- [Source: _bmad-output/planning-artifacts/architecture.md#System 7: Physics] — AABB swept collision, DDA raycasting
- [Source: _bmad-output/planning-artifacts/architecture.md#System 9: Scripting] — sol2 architecture, sandbox, rate limiting
- [Source: _bmad-output/planning-artifacts/architecture.md#ADR-008] — Exceptions disabled, Result<T> pattern
- [Source: _bmad-output/project-context.md#Critical Implementation Rules] — Naming, error handling, threading rules
- [Source: _bmad-output/implementation-artifacts/9-2-block-registration-placement-destruction-callbacks.md] — BlockCallbacks pattern, invoker pattern, sol2 table parsing
- [Source: _bmad-output/implementation-artifacts/9-4-block-tick-update-system.md] — IVec3Hash, BlockTimerManager design, ABM scanning patterns
- [Source: engine/src/game/PlayerController.cpp:362-393] — `collectSolidBlocks()` current implementation
- [Source: engine/src/physics/Raycast.cpp:16-80] — DDA raycast current implementation
- [Source: engine/src/world/ChunkManager.cpp:47-111] — `setBlock()` with neighbor dirty marking

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
