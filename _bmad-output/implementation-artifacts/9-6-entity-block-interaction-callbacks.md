# Story 9.6: Entity-Block Interaction Callbacks

Status: review

## Story

As a developer,
I want blocks to react when entities step on, fall on, or move inside them,
so that mods can create soul sand (slowness), cactus (damage), slime blocks (bounce), and pressure plates.

## Acceptance Criteria

1. `on_entity_inside(pos, entity)` — fires each simulation tick when the player AABB overlaps a block with this callback. Iterates all blocks within the player's AABB volume.
2. `on_entity_step_on(pos, entity)` — fires once when an entity is standing on top of a block (Y collision from above, `isOnGround` is true). Fires once per landing, not every tick.
3. `on_entity_fall_on(pos, entity, fall_distance) -> float` — fires when `isOnGround` transitions from false to true, with accumulated fall distance in blocks. Returns modified fall damage multiplier (0.0 = no damage, 1.0 = full).
4. `on_entity_collide(pos, entity, facing, velocity, is_impact)` — fires on any collision axis during AABB resolution. `facing` is the block face that was hit, `velocity` is the entity velocity before resolution, `is_impact` is true on the first tick of contact.
5. `on_projectile_hit(pos, projectile, hit_result)` — V1 stub: callback field exists in `BlockCallbacks` and is parsed from Lua tables, but never invoked (no projectile system yet). Integration test verifies the field is stored.
6. Minimal entity Lua wrapper passed to callbacks: `entity:damage(amount)`, `entity:get_velocity()`, `entity:get_position()`, `entity:set_velocity(vec3)`.
7. V1 scope: Only the player triggers these callbacks. Full entity support (mobs, dropped items) deferred to future epic.
8. Integration test: register a slime block with `on_entity_fall_on` returning 0 damage, fall on it, verify callback fires with correct fall distance and returns 0.

## Tasks / Subtasks

- [x] Task 1: Extend BlockCallbacks with 5 entity callback fields (AC: 1–5)
  - [x]1.1 Add 5 new `std::optional<sol::protected_function>` fields to `BlockCallbacks` in `engine/include/voxel/scripting/BlockCallbacks.h`: `onEntityInside`, `onEntityStepOn`, `onEntityFallOn`, `onEntityCollide`, `onProjectileHit`
  - [x]1.2 Update `categoryMask()` — add Bit 3 (0x08) for entity callback category
  - [x]1.3 Verify struct remains movable (sol::protected_function is movable — no issues expected)

- [x] Task 2: Extract entity callbacks in LuaBindings (AC: 1–5)
  - [x]2.1 In `parseBlockDefinition()` in `LuaBindings.cpp`, add extraction of 5 entity callback fields from Lua table
  - [x]2.2 Use `table.get<std::optional<sol::protected_function>>("on_entity_inside")` pattern (same as 9.2/9.3 callbacks)
  - [x]2.3 Only store non-nil functions

- [x] Task 3: Create entity Lua wrapper — `EntityHandle` (AC: 6)
  - [x]3.1 Create `engine/include/voxel/scripting/EntityHandle.h` — lightweight wrapper that exposes player state to Lua
  - [x]3.2 `EntityHandle` holds raw pointers to `PlayerController` and `BlockCallbackInvoker` (non-owning) — registered as a sol2 usertype
  - [x]3.3 Expose methods: `damage(float amount)`, `get_velocity() -> table {x,y,z}`, `get_position() -> table {x,y,z}`, `set_velocity(table {x,y,z})`
  - [x]3.4 `damage()`: V1 logs damage amount (no health system yet); future wires into health component
  - [x]3.5 `set_velocity()`: directly sets `PlayerController` velocity via new `setVelocity(glm::vec3)` method
  - [x]3.6 Register the usertype in `LuaBindings` init (alongside the block API setup)
  - [x]3.7 `EntityHandle` lives in `voxel::scripting` namespace

- [x] Task 4: Add entity invoke methods to BlockCallbackInvoker (AC: 1–4)
  - [x]4.1 Implement `invokeOnEntityInside(def, pos, entityHandle)` — returns void, fires each tick
  - [x]4.2 Implement `invokeOnEntityStepOn(def, pos, entityHandle)` — returns void, fires once on landing
  - [x]4.3 Implement `invokeOnEntityFallOn(def, pos, entityHandle, fallDistance) -> float` — returns modified damage multiplier (default 1.0)
  - [x]4.4 Implement `invokeOnEntityCollide(def, pos, entityHandle, facing, velocity, isImpact)` — returns void
  - [x]4.5 All invokers: check `has_value()`, use `sol::protected_function_result`, check `.valid()`, log errors, return safe defaults
  - [x]4.6 Pass `EntityHandle` as a sol2 usertype argument (Lua receives it as `entity` parameter)

- [x] Task 5: Add fall tracking to PlayerController (AC: 3, 8)
  - [x]5.1 Add `float m_fallDistance = 0.0f` member to `PlayerController`
  - [x]5.2 Add `bool m_wasOnGround = false` member for ground transition detection
  - [x]5.3 In `tickPhysics()`: when airborne and velocity.y < 0, accumulate `m_fallDistance += abs(velocity.y * dt)`
  - [x]5.4 In `tickPhysics()`: detect `isOnGround` transition (was false → now true) = landing event
  - [x]5.5 Add `[[nodiscard]] float consumeFallDistance()` — returns accumulated fall distance and resets to 0. Called by GameApp on landing.
  - [x]5.6 Add `[[nodiscard]] bool justLanded() const` — returns true on the tick where ground transition happened
  - [x]5.7 Add `void setVelocity(const glm::vec3& v)` public method (for EntityHandle::set_velocity)

- [x] Task 6: Wire entity callbacks into PlayerController tick loop (AC: 1–4)
  - [x]6.1 In `scanOverlappingBlocks()`: for each overlapping block, if block has `onEntityInside` callback, record it in a list of `(blockPos, blockId)` pairs to invoke
  - [x]6.2 Return (or store) the list from `scanOverlappingBlocks()` — actual Lua invocation happens in GameApp (not PlayerController, which doesn't know about scripting)
  - [x]6.3 In GameApp tick: after `tickPhysics()`, iterate the overlap list and call `invokeOnEntityInside` for each
  - [x]6.4 In GameApp tick: on landing event (`justLanded()`), look up the block directly below player feet, call `invokeOnEntityFallOn` with fall distance, apply damage modifier
  - [x]6.5 In GameApp tick: on landing event, also call `invokeOnEntityStepOn` for the block below
  - [x]6.6 `on_entity_step_on` fires ONCE on landing, not every tick while standing. Do NOT re-fire while `isOnGround` remains true.
  - [x]6.7 For `on_entity_collide`: during axis resolution in `resolveAxis()`, when movement is clipped (collision detected), record the collision info `(blockPos, face, preVelocity, isImpact)` — pass to GameApp for Lua invocation

- [x] Task 7: Collision data passing from PlayerController to GameApp (AC: 4)
  - [x]7.1 Define `struct EntityBlockCollision { glm::ivec3 blockPos; uint16_t blockId; std::string face; glm::vec3 velocity; bool isImpact; }` in PlayerController.h (or a new small header)
  - [x]7.2 Add `std::vector<EntityBlockCollision> m_frameCollisions` to PlayerController, cleared each tick start
  - [x]7.3 In `resolveAxis()`: when clipped, push collision info to `m_frameCollisions`
  - [x]7.4 Add `[[nodiscard]] const std::vector<EntityBlockCollision>& getFrameCollisions() const`
  - [x]7.5 In GameApp tick: iterate `getFrameCollisions()`, for each with an `onEntityCollide` callback, call `invokeOnEntityCollide`
  - [x]7.6 `isImpact` is true only on the first tick of contact with a specific block position (track previous tick's collision set)
  - [x]7.7 Face string mapping: axis 0 positive = "east", negative = "west"; axis 1 positive = "up", negative = "down"; axis 2 positive = "south", negative = "north"

- [x] Task 8: Overlap block list for `on_entity_inside` (AC: 1)
  - [x]8.1 Define `struct EntityBlockOverlap { glm::ivec3 blockPos; uint16_t blockId; }` (same header as 7.1)
  - [x]8.2 Add `std::vector<EntityBlockOverlap> m_frameOverlaps` to PlayerController, cleared each tick
  - [x]8.3 In `scanOverlappingBlocks()`: for each non-air overlapping block, push `{pos, blockId}` to `m_frameOverlaps`
  - [x]8.4 Add `[[nodiscard]] const std::vector<EntityBlockOverlap>& getFrameOverlaps() const`
  - [x]8.5 In GameApp tick: iterate `getFrameOverlaps()`, for each block with `onEntityInside` callback, call `invokeOnEntityInside`

- [x] Task 9: Integration tests (AC: 8)
  - [x]9.1 Create `tests/scripting/TestEntityBlockCallbacks.cpp`
  - [x]9.2 Test: register block with `on_entity_inside`, invoke callback, verify entity methods accessible from Lua (`entity:get_position()`, `entity:get_velocity()`)
  - [x]9.3 Test: register block with `on_entity_fall_on` returning 0.0, invoke callback with fall distance, verify return value is 0.0
  - [x]9.4 Test: register block with `on_entity_fall_on` returning 1.0 (full damage), verify return value
  - [x]9.5 Test: register block with `on_entity_step_on`, invoke callback, verify pos received correctly
  - [x]9.6 Test: register block with `on_entity_collide`, invoke with facing and velocity, verify Lua receives correct args
  - [x]9.7 Test: register block with `on_projectile_hit` (stub), verify callback field is stored in BlockCallbacks but NOT invoked
  - [x]9.8 Test: EntityHandle `damage()` calls log (no crash), `set_velocity()` modifies player velocity
  - [x]9.9 Test: block with no entity callbacks, verify invocation returns safe defaults (no crash)
  - [x]9.10 Create Lua test scripts: `entity_inside.lua`, `entity_fall_on.lua`, `entity_step_on.lua`, `entity_collide.lua`, `entity_projectile_stub.lua`

- [x] Task 10: Build integration (AC: all)
  - [x]10.1 Add `TestEntityBlockCallbacks.cpp` to `tests/CMakeLists.txt`
  - [x]10.2 Build full project, verify zero warnings under `/W4 /WX`
  - [x]10.3 Run all tests (existing + new), verify zero regressions

## Dev Notes

### Entity Callback Dispatch Flow

The critical design decision: `PlayerController` does NOT know about scripting. It collects physics data (overlaps, collisions, landing events), and `GameApp` dispatches the Lua callbacks.

```
PlayerController::tickPhysics(dt)
  ├── scanOverlappingBlocks()    → fills m_frameOverlaps (block positions AABB overlaps)
  ├── applyGravity()             → tracks fall distance when airborne
  ├── resolveCollisions()        → fills m_frameCollisions (axis collision events)
  └── detect landing             → sets m_justLanded, stores fall distance

GameApp::tick(dt)  [after tickPhysics]
  ├── if justLanded():
  │     block = getBlock(below player feet)
  │     invokeOnEntityFallOn(block, pos, entity, fallDistance)
  │     invokeOnEntityStepOn(block, pos, entity)
  ├── for each in getFrameOverlaps():
  │     if block has onEntityInside → invokeOnEntityInside(block, pos, entity)
  └── for each in getFrameCollisions():
        if block has onEntityCollide → invokeOnEntityCollide(block, pos, entity, ...)
```

### EntityHandle Design (Critical — Minimal Wrapper)

`EntityHandle` is a lightweight usertype passed to Lua callbacks as the `entity` parameter. For V1, it wraps only the player.

```cpp
// engine/include/voxel/scripting/EntityHandle.h
#pragma once

#include <glm/vec3.hpp>

namespace voxel::game { class PlayerController; }

namespace voxel::scripting
{

/// Lightweight handle passed to entity-block callbacks in Lua.
/// V1: wraps PlayerController only. Future: wraps any ECS entity.
class EntityHandle
{
public:
    explicit EntityHandle(game::PlayerController& player);

    void damage(float amount);                    // V1: logs, no health system
    [[nodiscard]] glm::vec3 getVelocity() const;
    [[nodiscard]] glm::dvec3 getPosition() const;
    void setVelocity(const glm::vec3& vel);

private:
    game::PlayerController& m_player;
};

} // namespace voxel::scripting
```

Register as sol2 usertype in `LuaBindings`:

```cpp
lua.new_usertype<EntityHandle>("EntityHandle",
    "damage", &EntityHandle::damage,
    "get_velocity", [](EntityHandle& e, sol::this_state s) {
        sol::state_view lua(s);
        auto v = e.getVelocity();
        sol::table t = lua.create_table();
        t["x"] = v.x; t["y"] = v.y; t["z"] = v.z;
        return t;
    },
    "get_position", [](EntityHandle& e, sol::this_state s) {
        sol::state_view lua(s);
        auto p = e.getPosition();
        sol::table t = lua.create_table();
        t["x"] = p.x; t["y"] = p.y; t["z"] = p.z;
        return t;
    },
    "set_velocity", [](EntityHandle& e, const sol::table& t) {
        float x = t.get_or("x", 0.0f);
        float y = t.get_or("y", 0.0f);
        float z = t.get_or("z", 0.0f);
        e.setVelocity({x, y, z});
    }
);
```

### Fall Distance Tracking in PlayerController

Add to private members:

```cpp
float m_fallDistance = 0.0f;
bool m_wasOnGround = false;  // Previous tick's ground state
bool m_justLanded = false;   // True on the tick of landing
```

In `tickPhysics()`, after `resolveCollisions()`:

```cpp
// Detect landing: was airborne, now on ground
m_justLanded = (!m_wasOnGround && m_isOnGround);

// Track fall distance while falling
if (!m_isOnGround && m_velocity.y < 0.0f)
{
    m_fallDistance += std::abs(m_velocity.y * dt);
}

// Reset fall distance when on ground and NOT just landed
// (justLanded needs the value for one tick)
if (m_isOnGround && !m_justLanded)
{
    m_fallDistance = 0.0f;
}

m_wasOnGround = m_isOnGround;
```

`consumeFallDistance()`:

```cpp
float PlayerController::consumeFallDistance()
{
    float dist = m_fallDistance;
    m_fallDistance = 0.0f;
    return dist;
}
```

### Collision Data Collection in resolveAxis

In `resolveAxis()`, when `wasClipped` is true, collect collision info:

```cpp
if (wasClipped)
{
    // Existing code: set velocity to 0, detect ground...

    // NEW: record collision for entity callbacks
    // Find the block that caused the clip
    // The blocking block is at the edge of the player AABB on the clipped axis
    glm::ivec3 collisionBlockPos;
    if (delta > 0.0f)
    {
        collisionBlockPos[axis] = static_cast<int>(std::floor(
            m_position[axis] + (axis == 1 ? HALF_EXTENTS.y * 2.0f : HALF_EXTENTS[axis]) + COLLISION_EPSILON));
    }
    else
    {
        collisionBlockPos[axis] = static_cast<int>(std::floor(
            m_position[axis] - (axis == 1 ? 0.0f : HALF_EXTENTS[axis]) - COLLISION_EPSILON));
    }
    // Perpendicular axes use player center
    int ax1 = (axis + 1) % 3;
    int ax2 = (axis + 2) % 3;
    collisionBlockPos[ax1] = static_cast<int>(std::floor(m_position[ax1]));
    collisionBlockPos[ax2] = static_cast<int>(std::floor(m_position[ax2]));

    // Determine face name
    static constexpr const char* FACE_NAMES[6] = {"east", "up", "south", "west", "down", "north"};
    int faceIndex = axis + (delta > 0.0f ? 0 : 3);
    // axis 0+: east, axis 0-: west, axis 1+: up, axis 1-: down, axis 2+: south, axis 2-: north

    m_frameCollisions.push_back({
        collisionBlockPos,
        0, // blockId filled by caller or looked up in GameApp
        FACE_NAMES[faceIndex],
        m_velocity, // velocity BEFORE clipping
        false       // isImpact determined in GameApp by comparing with previous tick
    });
}
```

**Note**: Keep the collision recording lightweight. `resolveAxis` is called 3× per tick. The `m_frameCollisions` vector is cleared at the start of each `tickPhysics()` call.

### Block Below Player for Landing Events

When `justLanded()` is true, find the block the player landed on:

```cpp
// In GameApp, after tickPhysics:
if (m_player.justLanded())
{
    float fallDist = m_player.consumeFallDistance();

    // Block directly below player feet
    glm::ivec3 feetPos = glm::ivec3(
        static_cast<int>(std::floor(m_player.getPosition().x)),
        static_cast<int>(std::floor(m_player.getPosition().y)) - 1,
        static_cast<int>(std::floor(m_player.getPosition().z)));

    uint16_t blockId = m_chunkManager.getBlock(feetPos);
    if (blockId != world::BLOCK_AIR)
    {
        const auto& def = m_blockRegistry.getBlockType(blockId);
        if (def.callbacks)
        {
            EntityHandle entity(m_player);

            if (def.callbacks->onEntityFallOn.has_value())
            {
                float damageMul = m_callbackInvoker.invokeOnEntityFallOn(
                    def, feetPos, entity, fallDist);
                // V1: log damage. Future: apply fallDist * damageMul to health
                if (fallDist > 3.0f && damageMul > 0.0f)
                {
                    VX_LOG_INFO("Fall damage: {} blocks * {} = {} HP",
                        fallDist, damageMul, fallDist * damageMul);
                }
            }

            if (def.callbacks->onEntityStepOn.has_value())
            {
                m_callbackInvoker.invokeOnEntityStepOn(def, feetPos, entity);
            }
        }
    }
}
```

### Callback Invocation Pattern (Same as 9.2/9.3)

Follow the exact same exception-free pattern:

```cpp
float BlockCallbackInvoker::invokeOnEntityFallOn(
    const BlockDefinition& def,
    const glm::ivec3& pos,
    EntityHandle& entity,
    float fallDistance)
{
    if (!def.callbacks || !def.callbacks->onEntityFallOn.has_value())
        return 1.0f; // Default: full fall damage

    auto posTable = posToTable(m_lua, pos);
    sol::protected_function_result result =
        (*def.callbacks->onEntityFallOn)(posTable, entity, fallDistance);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_entity_fall_on error for '{}': {}",
            def.stringId, err.what());
        return 1.0f; // Default on error: full fall damage
    }

    return result.get_type() == sol::type::number
        ? result.get<float>()
        : 1.0f;
}
```

**Default return values per callback:**

| Callback | Default (nil/missing) | Default (error) |
|----------|----------------------|-----------------|
| `on_entity_inside` | no-op | no-op |
| `on_entity_step_on` | no-op | no-op |
| `on_entity_fall_on` | `1.0f` (full damage) | `1.0f` |
| `on_entity_collide` | no-op | no-op |
| `on_projectile_hit` | (never invoked V1) | (never invoked V1) |

### What NOT to Do

- **DO NOT add sol2 headers to PlayerController.h** — PlayerController remains pure C++. EntityHandle and callbacks are only in scripting layer.
- **DO NOT modify ScriptEngine** — unchanged since Story 9.1.
- **DO NOT implement mob/item entity callbacks** — V1 is player-only. Future entity system will extend `EntityHandle` to wrap any ECS entity.
- **DO NOT implement `on_timer`, ABM, LBM** — that's Story 9.4.
- **DO NOT implement `on_neighbor_changed` or shape callbacks** — that's Story 9.5.
- **DO NOT implement metadata/inventory APIs** — that's Story 9.7.
- **DO NOT implement `voxel.set_block`/`voxel.get_block` world APIs** — that's Story 9.8. Entity callbacks that want to modify blocks won't work until 9.8.
- **DO NOT implement global events** — that's Story 9.10.
- **DO NOT implement a health/damage system** — `entity:damage()` logs only. Health comes in a future epic.
- **DO NOT add projectile invocation** — `on_projectile_hit` is a field stub only. No projectile system exists.
- **DO NOT make PlayerController depend on ScriptEngine, sol2, or LuaBindings** — the callback dispatch must be in GameApp, not PlayerController.
- **DO NOT store sol::function in PlayerController** — keep scripting concerns out of the game layer.

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `BlockCallbacks` | `engine/include/voxel/scripting/BlockCallbacks.h` | Extend with 5 entity callback fields |
| `BlockCallbackInvoker` | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | Add 4 active invoke methods (projectileHit is stub) |
| `BlockCallbackInvoker.cpp` | `engine/src/scripting/BlockCallbackInvoker.cpp` | `posToTable()` utility, invocation pattern |
| `LuaBindings` | `engine/src/scripting/LuaBindings.cpp` | Add callback extraction + EntityHandle usertype registration |
| `PlayerController` | `engine/include/voxel/game/PlayerController.h` | Add fall tracking, collision data collection, setVelocity |
| `PlayerController::scanOverlappingBlocks` | `engine/src/game/PlayerController.cpp` | Already iterates blocks within player AABB — extend to record overlaps |
| `PlayerController::resolveAxis` | `engine/src/game/PlayerController.cpp` | Already detects collision clips — extend to record collision events |
| `GameApp` | `game/src/GameApp.cpp` | Wire entity callback dispatch after tickPhysics |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | Look up BlockDefinition for callback access |
| `ChunkManager` | `engine/src/world/ChunkManager.cpp` | `getBlock(pos)` for block-below lookups |
| `Result<T>` | `engine/include/voxel/core/Result.h` | Error handling pattern |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging callback errors |
| `math::AABB` | `engine/include/voxel/math/AABB.h` | Player bounding box |

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| MODIFY | `engine/include/voxel/scripting/BlockCallbacks.h` | `voxel::scripting` | Add 5 entity callback fields + categoryMask bit 3 |
| MODIFY | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | `voxel::scripting` | Add 4 invoke method declarations |
| MODIFY | `engine/src/scripting/BlockCallbackInvoker.cpp` | `voxel::scripting` | Implement 4 invoke methods |
| MODIFY | `engine/src/scripting/LuaBindings.cpp` | `voxel::scripting` | Extract 5 entity callbacks + register EntityHandle usertype |
| NEW | `engine/include/voxel/scripting/EntityHandle.h` | `voxel::scripting` | Lightweight entity wrapper for Lua |
| NEW | `engine/src/scripting/EntityHandle.cpp` | `voxel::scripting` | EntityHandle implementation (damage log, velocity get/set) |
| MODIFY | `engine/include/voxel/game/PlayerController.h` | `voxel::game` | Add fall tracking, collision/overlap structs, setVelocity |
| MODIFY | `engine/src/game/PlayerController.cpp` | `voxel::game` | Fall distance accumulation, collision recording, overlap recording |
| MODIFY | `game/src/GameApp.cpp` | — | Wire entity callback dispatch after tickPhysics |
| MODIFY | `engine/CMakeLists.txt` | — | Add EntityHandle.cpp to sources |
| NEW | `tests/scripting/TestEntityBlockCallbacks.cpp` | — | Integration tests for entity callbacks |
| NEW | `tests/scripting/test_scripts/entity_inside.lua` | — | Test: on_entity_inside callback |
| NEW | `tests/scripting/test_scripts/entity_fall_on.lua` | — | Test: on_entity_fall_on returning damage modifier |
| NEW | `tests/scripting/test_scripts/entity_step_on.lua` | — | Test: on_entity_step_on callback |
| NEW | `tests/scripting/test_scripts/entity_collide.lua` | — | Test: on_entity_collide callback |
| NEW | `tests/scripting/test_scripts/entity_projectile_stub.lua` | — | Test: on_projectile_hit field stored |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestEntityBlockCallbacks.cpp |

### Naming & Style

- Classes: `EntityHandle`, `EntityBlockCollision`, `EntityBlockOverlap` (PascalCase)
- Methods: `invokeOnEntityInside`, `invokeOnEntityFallOn`, `consumeFallDistance`, `justLanded` (camelCase)
- Members: `m_fallDistance`, `m_wasOnGround`, `m_justLanded`, `m_frameCollisions`, `m_frameOverlaps` (m_ prefix)
- Constants: `FACE_NAMES` (SCREAMING_SNAKE)
- Namespace: `voxel::scripting` for EntityHandle/callbacks, `voxel::game` for PlayerController additions
- No exceptions — `Result<T>` for parsing, safe defaults for callback invocation
- Max ~500 lines per file
- `#pragma once` for all headers
- `[[nodiscard]]` on all query methods

### Previous Story Intelligence

**From Story 9.3 (most recent):**
- InteractionState pattern: struct with isActive flag, target pos, elapsed time — EntityBlockCollision follows same pattern
- Callback dispatch in GameApp (not PlayerController) — entity callbacks follow same separation
- `BlockCallbackInvoker` signature: `(const BlockDefinition& def, const glm::ivec3& pos, ...)` — entity invokers add `EntityHandle&` parameter
- `posToTable(m_lua, pos)` utility already exists in BlockCallbackInvoker.cpp — reuse it
- `categoryMask()` currently uses bits 0–2 (placement, destruction, interaction) — entity = bit 3 (0x08)
- Invocation pattern: `has_value()` → `protected_function_result` → `.valid()` → log error → return default
- Tests: Catch2 v3, `[scripting]` tag, real Lua scripts (not inline strings)
- Code review from 9.3: removed dead InteractBlock command code — keep commands clean
- Code review from 9.3: added ImGui mouse capture cancellation — entity callbacks don't need ImGui guards (physics-driven, not input-driven)

**From Story 9.2:**
- `LuaBindings::parseBlockDefinition()` extracts callbacks with `table.get<std::optional<sol::protected_function>>("field_name")`
- Bootstrap: ScriptEngine → LuaBindings::registerBlockAPI → loadScript → register_block calls
- `BlockCallbacksPtr` = `unique_ptr<BlockCallbacks, BlockCallbacksDeleter>` — defined in Block.h

**From existing PlayerController:**
- `scanOverlappingBlocks()` at line 111 already iterates blocks within player AABB — perfect hook point for `on_entity_inside` data collection
- `resolveAxis()` at line 409 already detects clipped movement — perfect hook point for `on_entity_collide` data collection
- `m_isOnGround` set to true in `resolveAxis()` when Y axis delta < 0 is clipped (line 492–495)
- `m_damageAccumulator` pattern: accumulates per-tick, fires once per second — similar to fall distance accumulation
- Player AABB: center-bottom at `m_position`, half-extents 0.3×0.9×0.3, eye height 1.62

### Git Intelligence

Recent scripting commits:
```
e98e6ec feat(scripting): implement block interaction callbacks and sustained interactions
24efe9d feat(scripting): add Lua-based block callbacks and block registration API
effe7b4 feat(scripting): implement neighbor/shape/physics callbacks in BlockCallbacks
8ce6142 feat(scripting): implement sol2/LuaJIT-based ScriptEngine with sandbox and integration tests
```

Commit message for this story: `feat(scripting): implement entity-block interaction callbacks with fall/step/overlap/collide`

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/EntityHandle.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/game/PlayerController.h"

using namespace voxel::scripting;

TEST_CASE("Entity-block callbacks", "[scripting][entity]")
{
    ScriptEngine engine;
    REQUIRE(engine.init().has_value());
    auto& lua = engine.getLuaState();

    voxel::world::BlockRegistry registry;
    LuaBindings::registerBlockAPI(lua, registry);

    // Create a minimal PlayerController for EntityHandle
    voxel::game::PlayerController player;
    // Note: player.init() requires ChunkManager — for unit tests, just use
    // the default-constructed state (position at 0,80,0, velocity 0)

    SECTION("on_entity_fall_on returns 0 for slime block")
    {
        engine.loadScript("tests/scripting/test_scripts/entity_fall_on.lua");
        const auto& def = registry.getBlockType("test:slime_block");
        REQUIRE(def.callbacks != nullptr);
        REQUIRE(def.callbacks->onEntityFallOn.has_value());

        BlockCallbackInvoker invoker(lua, registry);
        EntityHandle entity(player);
        float damageMul = invoker.invokeOnEntityFallOn(def, {5, 64, 5}, entity, 10.0f);
        REQUIRE(damageMul == 0.0f);
    }

    SECTION("on_entity_inside fires with entity access")
    {
        engine.loadScript("tests/scripting/test_scripts/entity_inside.lua");
        const auto& def = registry.getBlockType("test:cactus");
        REQUIRE(def.callbacks->onEntityInside.has_value());

        BlockCallbackInvoker invoker(lua, registry);
        EntityHandle entity(player);
        invoker.invokeOnEntityInside(def, {3, 65, 3}, entity);
        // Verify Lua could call entity:get_position()
        REQUIRE(lua["test_entity_pos_set"].get<bool>() == true);
    }

    SECTION("missing entity callbacks return safe defaults")
    {
        engine.loadScript("tests/scripting/test_scripts/register_block.lua");
        const auto& def = registry.getBlockType("test:plain_block");
        BlockCallbackInvoker invoker(lua, registry);
        EntityHandle entity(player);
        float result = invoker.invokeOnEntityFallOn(def, {0, 0, 0}, entity, 5.0f);
        REQUIRE(result == 1.0f); // Default: full fall damage
    }
}
```

Test Lua files:

**entity_fall_on.lua:**
```lua
voxel.register_block({
    id = "test:slime_block",
    on_entity_fall_on = function(pos, entity, fall_distance)
        test_fall_distance = fall_distance
        -- Bounce: negate and reduce velocity
        local vel = entity:get_velocity()
        entity:set_velocity({x = vel.x, y = -vel.y * 0.8, z = vel.z})
        return 0.0 -- No fall damage
    end,
})
```

**entity_inside.lua:**
```lua
voxel.register_block({
    id = "test:cactus",
    on_entity_inside = function(pos, entity)
        local epos = entity:get_position()
        test_entity_pos_set = true
        test_entity_x = epos.x
        entity:damage(0.5)
    end,
})
```

**entity_step_on.lua:**
```lua
voxel.register_block({
    id = "test:pressure_plate",
    on_entity_step_on = function(pos, entity)
        test_stepped_on = true
        test_step_pos_x = pos.x
        test_step_pos_y = pos.y
        test_step_pos_z = pos.z
    end,
})
```

**entity_collide.lua:**
```lua
voxel.register_block({
    id = "test:bumper",
    on_entity_collide = function(pos, entity, facing, velocity, is_impact)
        test_collide_facing = facing
        test_collide_is_impact = is_impact
        test_collide_vel_y = velocity.y
    end,
})
```

**entity_projectile_stub.lua:**
```lua
voxel.register_block({
    id = "test:target_block",
    on_projectile_hit = function(pos, projectile, hit_result)
        -- This should never be called in V1
        test_projectile_hit = true
    end,
})
```

### Project Structure Notes

- New file `EntityHandle.h/cpp` — follows one-class-per-file rule, lives in `voxel::scripting`
- `EntityBlockCollision` and `EntityBlockOverlap` structs are small POD types — define them in `PlayerController.h` (they're tightly coupled to PlayerController's collision loop)
- If `PlayerController.h` approaches 500 lines, extract collision/overlap structs to `engine/include/voxel/game/EntityCollisionData.h`
- Test scripts in `tests/scripting/test_scripts/` alongside existing 9.1–9.3 test scripts

### Future Story Dependencies

This story establishes patterns used by:
- **Story 9.7**: `on_entity_inside` pattern reused for blocks that detect entity presence (pressure plates with metadata)
- **Story 9.8**: `voxel.set_block` API will allow entity callbacks to modify the world (e.g., pressure plate activation)
- **Story 9.9**: `on_animate_tick` follows similar per-tick dispatch pattern from GameApp
- **Story 9.10**: `player_land` / `player_move` global events overlap with entity-block callbacks but are independent systems
- **Future ECS entity epic**: `EntityHandle` extended to wrap any entity via EnTT registry lookup

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.6 full specification]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 3: ECS, System 9: Scripting, ADR-007, ADR-010]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards]
- [Source: _bmad-output/implementation-artifacts/9-3-block-interaction-callbacks.md — InteractionState pattern, callback dispatch in GameApp]
- [Source: _bmad-output/implementation-artifacts/9-2-block-registration-placement-destruction-callbacks.md — BlockCallbacks, LuaBindings, callback invocation pattern]
- [Source: engine/include/voxel/scripting/BlockCallbacks.h — Current callback fields (24 callbacks across 3 categories)]
- [Source: engine/include/voxel/scripting/BlockCallbackInvoker.h — Current invoke method signatures]
- [Source: engine/include/voxel/game/PlayerController.h — Player physics, AABB, ground detection, scanOverlappingBlocks]
- [Source: engine/src/game/PlayerController.cpp — resolveAxis collision clipping, scanOverlappingBlocks iteration]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition with BlockCallbacksPtr]
- [Source: engine/include/voxel/physics/Raycast.h — RaycastResult for reference]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
None — clean build, all tests passed on first run.

### Completion Notes List
- Task 1: Added 5 entity callback fields to BlockCallbacks (onEntityInside, onEntityStepOn, onEntityFallOn, onEntityCollide, onProjectileHit). Added categoryMask bit 7 (0x80) for entity category.
- Task 2: Extended parseBlockDefinition() in LuaBindings to extract 5 entity callback fields from Lua tables using the same checkAndStore pattern.
- Task 3: Created EntityHandle class (header + cpp) as lightweight wrapper around PlayerController. Registered as sol2 usertype with damage(), get_velocity(), get_position(), set_velocity() methods. Added registerEntityAPI() to LuaBindings.
- Task 4: Implemented 4 active invoke methods in BlockCallbackInvoker (invokeOnEntityInside, invokeOnEntityStepOn, invokeOnEntityFallOn, invokeOnEntityCollide). All follow the existing exception-free pattern with safe defaults.
- Task 5: Added fall tracking to PlayerController — m_fallDistance, m_wasOnGround, m_justLanded members. consumeFallDistance() and justLanded() public methods. setVelocity() for EntityHandle.
- Task 6: Wired entity callback dispatch in GameApp::tick() after tickPhysics — landing events (fall_on + step_on), overlap callbacks (entity_inside), collision callbacks (entity_collide).
- Task 7: Added EntityBlockCollision struct with blockPos, blockId, face, velocity, isImpact. Collision recording in resolveAxis with face name mapping and isImpact detection via previous tick comparison.
- Task 8: Added EntityBlockOverlap struct. Overlap recording in scanOverlappingBlocks for all non-air blocks within player AABB.
- Task 9: Created 10 integration tests (44 assertions) covering all 5 entity callbacks, EntityHandle methods, safe defaults, and categoryMask.
- Task 10: Added EntityHandle.cpp to engine CMakeLists.txt, TestEntityBlockCallbacks.cpp to tests CMakeLists.txt. Full build with /W4 /WX — zero warnings. Full test suite: 310 tests, 490,502 assertions, all passed.

### Change Log
- 2026-03-30: Implemented entity-block interaction callbacks (Story 9.6) — 5 callback fields, EntityHandle wrapper, fall/collision/overlap tracking, GameApp dispatch, 10 integration tests.

### File List
- engine/include/voxel/scripting/BlockCallbacks.h (MODIFIED) — 5 entity callback fields + categoryMask bit 7
- engine/include/voxel/scripting/BlockCallbackInvoker.h (MODIFIED) — 4 entity invoke method declarations + EntityHandle forward decl
- engine/src/scripting/BlockCallbackInvoker.cpp (MODIFIED) — 4 entity invoke implementations
- engine/include/voxel/scripting/LuaBindings.h (MODIFIED) — registerEntityAPI declaration
- engine/src/scripting/LuaBindings.cpp (MODIFIED) — 5 entity callback extraction + registerEntityAPI implementation
- engine/include/voxel/scripting/EntityHandle.h (NEW) — lightweight entity wrapper for Lua
- engine/src/scripting/EntityHandle.cpp (NEW) — EntityHandle implementation
- engine/include/voxel/game/PlayerController.h (MODIFIED) — fall tracking, collision/overlap structs, setVelocity
- engine/src/game/PlayerController.cpp (MODIFIED) — fall distance tracking, collision recording, overlap recording
- game/src/GameApp.cpp (MODIFIED) — entity callback dispatch after tickPhysics
- game/src/GameApp.h (NOT MODIFIED) — no changes needed
- engine/CMakeLists.txt (MODIFIED) — added EntityHandle.cpp
- tests/CMakeLists.txt (MODIFIED) — added TestEntityBlockCallbacks.cpp
- tests/scripting/TestEntityBlockCallbacks.cpp (NEW) — 10 integration tests
- tests/scripting/test_scripts/entity_inside.lua (NEW) — test: on_entity_inside
- tests/scripting/test_scripts/entity_fall_on.lua (NEW) — test: on_entity_fall_on
- tests/scripting/test_scripts/entity_step_on.lua (NEW) — test: on_entity_step_on
- tests/scripting/test_scripts/entity_collide.lua (NEW) — test: on_entity_collide
- tests/scripting/test_scripts/entity_projectile_stub.lua (NEW) — test: on_projectile_hit stub
