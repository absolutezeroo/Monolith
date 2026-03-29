# Story 9.3: Block Interaction Callbacks (Right-click, Punch, Multi-phase)

Status: ready-for-dev

## Story

As a developer,
I want blocks to react to player clicks and sustained interactions,
so that mods can create interactive blocks (doors, levers, furnaces, crafting tables, grindstones).

## Acceptance Criteria

1. `on_rightclick(pos, node, clicker, itemstack, pointed_thing) -> itemstack` — fires on right-click, returns modified itemstack.
2. `on_punch(pos, node, puncher, pointed_thing)` — fires on left-click (quick hit, not dig hold).
3. `on_secondary_use(itemstack, user, pointed_thing)` — fires when player right-clicks while not targeting a block.
4. `on_interact_start(pos, player) -> bool` — fires on right-click press if defined; return true to begin sustained interaction.
5. `on_interact_step(pos, player, elapsed_seconds) -> bool` — fires every simulation tick while player holds right-click; return false to end.
6. `on_interact_stop(pos, player, elapsed_seconds)` — fires when player releases right-click during sustained interaction.
7. `on_interact_cancel(pos, player, elapsed_seconds, reason) -> bool` — fires when interaction is interrupted (player moves >2 blocks, takes damage, opens menu).
8. Right-click priority: `on_interact_start` (if defined) > `on_rightclick` (if defined) > default placement behavior.
9. `on_punch` fires on LMB press frame, independently of mining (mining still proceeds on hold).
10. If ImGui wants mouse input (`io.WantCaptureMouse`), ALL block interaction callbacks are skipped.
11. New `InteractionState` struct in PlayerController tracks active sustained interaction (target block, start time, active flag).
12. Cancel conditions: player moves >2 blocks from interaction target, takes damage, presses Escape. Each calls `on_interact_cancel` with reason string.
13. Integration test: register a grindstone with 3-second hold interaction, verify `on_interact_step` fires each tick and stops after release.

## Tasks / Subtasks

- [ ] Task 1: Extend BlockCallbacks with interaction callbacks (AC: 1–7)
  - [ ] 1.1 Add 7 new `std::optional<sol::protected_function>` fields to `BlockCallbacks` struct in `engine/include/voxel/scripting/BlockCallbacks.h`
  - [ ] 1.2 Fields: `onRightclick`, `onPunch`, `onSecondaryUse`, `onInteractStart`, `onInteractStep`, `onInteractStop`, `onInteractCancel`
  - [ ] 1.3 Verify `BlockCallbacks` remains movable (no issues — sol::protected_function is movable)

- [ ] Task 2: Extract interaction callbacks in LuaBindings (AC: 1–7)
  - [ ] 2.1 In `parseBlockDefinition()` in `LuaBindings.cpp`, add extraction of 7 new callback fields from the Lua table
  - [ ] 2.2 Use `table.get<std::optional<sol::protected_function>>("on_rightclick")` pattern (same as 9.2 placement/destruction callbacks)
  - [ ] 2.3 Only store non-nil functions

- [ ] Task 3: Add interaction invokers to BlockCallbackInvoker (AC: 1–7, 8)
  - [ ] 3.1 Implement `invokeOnRightclick(def, pos, node, clicker, itemstack, pointedThing) -> sol::object`
  - [ ] 3.2 Implement `invokeOnPunch(def, pos, node, puncher, pointedThing)`
  - [ ] 3.3 Implement `invokeOnSecondaryUse(def, itemstack, user, pointedThing) -> sol::object`
  - [ ] 3.4 Implement `invokeOnInteractStart(def, pos, playerId) -> bool`
  - [ ] 3.5 Implement `invokeOnInteractStep(def, pos, playerId, elapsedSeconds) -> bool`
  - [ ] 3.6 Implement `invokeOnInteractStop(def, pos, playerId, elapsedSeconds)`
  - [ ] 3.7 Implement `invokeOnInteractCancel(def, pos, playerId, elapsedSeconds, reason) -> bool`
  - [ ] 3.8 All invokers: check `has_value()`, use `sol::protected_function_result`, check `.valid()`, log errors, return safe defaults

- [ ] Task 4: Create InteractionState in PlayerController (AC: 11, 12)
  - [ ] 4.1 Define `InteractionState` struct: `isActive`, `targetBlockPos (glm::ivec3)`, `startTime (float)`, `elapsedTime (float)`, `targetBlockId (uint16_t)`
  - [ ] 4.2 Add `InteractionState m_interactionState` member to `PlayerController`
  - [ ] 4.3 Add `updateInteraction(float dt, ...) -> void` method to PlayerController
  - [ ] 4.4 Add `cancelInteraction(const std::string& reason)` method
  - [ ] 4.5 Add `isInteracting() const -> bool` accessor

- [ ] Task 5: Wire right-click interaction into GameApp (AC: 8, 10)
  - [ ] 5.1 On RMB press: check `io.WantCaptureMouse` — skip if true
  - [ ] 5.2 On RMB press with raycast hit: look up target block definition
  - [ ] 5.3 Priority check: if block has `on_interact_start` → call it, if returns true → enter sustained interaction mode (set InteractionState)
  - [ ] 5.4 Else if block has `on_rightclick` → call it, skip placement
  - [ ] 5.5 Else → proceed with existing placement logic
  - [ ] 5.6 On RMB press with no raycast hit: check held item for `on_secondary_use` callback, call if defined
  - [ ] 5.7 While RMB held + `InteractionState.isActive`: call `invokeOnInteractStep` each tick with accumulated elapsed time; if returns false → end interaction
  - [ ] 5.8 On RMB release while interacting: call `invokeOnInteractStop`, clear InteractionState
  - [ ] 5.9 While interacting: suppress placement commands entirely

- [ ] Task 6: Wire left-click punch into GameApp (AC: 9, 10)
  - [ ] 6.1 On LMB press frame (`wasMouseButtonPressed`): check `io.WantCaptureMouse` — skip if true
  - [ ] 6.2 If raycast hit: look up target block definition, call `invokeOnPunch`
  - [ ] 6.3 Do NOT block mining — punch and mining coexist (punch fires once on press, mining continues on hold)

- [ ] Task 7: Implement cancel conditions (AC: 12)
  - [ ] 7.1 Each tick while `InteractionState.isActive`: check player distance to `targetBlockPos` — if >2.0 blocks → call `cancelInteraction("moved_away")`
  - [ ] 7.2 Check if target block at `targetBlockPos` changed (different blockId) → call `cancelInteraction("block_changed")`
  - [ ] 7.3 On Escape press (cursor release) while interacting → call `cancelInteraction("menu_opened")`
  - [ ] 7.4 `cancelInteraction` calls `invokeOnInteractCancel` then clears InteractionState
  - [ ] 7.5 Future: hook into damage system when it exists to cancel on damage

- [ ] Task 8: Add new GameCommand type for interaction (AC: 8)
  - [ ] 8.1 Add `InteractBlock` to `CommandType` enum
  - [ ] 8.2 Create `InteractBlockPayload`: `position (IVec3)`, `action (enum: Rightclick|Punch|InteractStart|SecondaryUse)`
  - [ ] 8.3 Push InteractBlock command from input handling, process in command loop
  - [ ] 8.4 NOTE: sustained interaction (step/stop/cancel) runs directly from PlayerController update (not via command queue) because it's tick-driven continuous state, not discrete actions

- [ ] Task 9: Integration tests (AC: 13)
  - [ ] 9.1 Create `tests/scripting/TestBlockInteraction.cpp`
  - [ ] 9.2 Test: register block with `on_rightclick`, invoke callback, verify it receives correct pos/node args
  - [ ] 9.3 Test: register block with `on_punch`, invoke callback, verify fires correctly
  - [ ] 9.4 Test: register block with `on_interact_start` returning true, verify `invokeOnInteractStart` returns true
  - [ ] 9.5 Test: register block with `on_interact_step`, invoke with elapsed time, verify return value propagation
  - [ ] 9.6 Test: register block with `on_interact_cancel`, invoke with reason, verify reason string reaches Lua
  - [ ] 9.7 Test: register block with BOTH `on_interact_start` and `on_rightclick`, verify `on_interact_start` takes priority
  - [ ] 9.8 Test: register block with only `on_rightclick` (no `on_interact_start`), verify `on_rightclick` fires
  - [ ] 9.9 Test: register block with no interaction callbacks, verify default behavior (no crash, no callback)
  - [ ] 9.10 Create test Lua scripts in `tests/scripting/test_scripts/` for interaction tests

- [ ] Task 10: Build integration (AC: all)
  - [ ] 10.1 Add `TestBlockInteraction.cpp` to `tests/CMakeLists.txt`
  - [ ] 10.2 Build full project, verify zero warnings under `/W4 /WX`
  - [ ] 10.3 Run all tests (existing + new), verify zero regressions

## Dev Notes

### Right-Click Priority Logic (Critical)

The current right-click behavior (in `GameApp.cpp` ~line 290) is straightforward: press RMB → push PlaceBlock command. Story 9.3 changes this to a priority chain:

```
On RMB press:
  if (ImGui.io.WantCaptureMouse) → skip entirely
  if (raycast.hit):
    blockDef = blockRegistry.getBlockType(chunkManager.getBlock(raycast.blockPos))
    if (blockDef.callbacks && blockDef.callbacks->onInteractStart):
      result = invokeOnInteractStart(blockDef, raycast.blockPos, playerId)
      if (result == true):
        → Enter sustained interaction (set InteractionState)
        → Do NOT place block
        → Return
    if (blockDef.callbacks && blockDef.callbacks->onRightclick):
      invokeOnRightclick(blockDef, raycast.blockPos, ...)
      → Do NOT place block
      → Return
    → Fall through to existing placement logic
  else (no raycast hit):
    → Check held item for on_secondary_use, call if defined
    → Otherwise no-op (can't place in air)
```

### Left-Click Punch vs Mining Coexistence

`on_punch` fires once on the frame LMB is pressed. Mining (already implemented in `PlayerController::updateMining`) starts accumulating on the same press and continues while held. They are independent:

```
Frame N (LMB pressed):
  → on_punch fires (instant callback, one-shot)
  → Mining begins (MiningState starts accumulating progress)
Frame N+1..M (LMB held):
  → Mining continues (no more punch callbacks)
Frame M (mining completes OR LMB released):
  → Mining result processed as before
```

This matches Luanti behavior — punching a noteblock plays its sound, but holding LMB mines it.

### InteractionState Design

Add to `PlayerController.h`:

```cpp
struct InteractionState
{
    bool isActive = false;
    glm::ivec3 targetBlockPos{0};
    uint16_t targetBlockId = 0;
    float elapsedTime = 0.0f;
};
```

This is simpler than `MiningState` because:
- No progress bar or crack stage (those are block-specific, handled in Lua via `on_interact_step`)
- No break time calculation (duration is determined by the Lua callback returning false)
- Just tracks: "am I interacting?", "where?", "how long?"

Add these members/methods to `PlayerController`:

```cpp
// Member
InteractionState m_interactionState;

// Methods
void startInteraction(const glm::ivec3& pos, uint16_t blockId);
void updateInteraction(float dt);
void stopInteraction();       // RMB release
void cancelInteraction(const std::string& reason);
bool isInteracting() const { return m_interactionState.isActive; }
const InteractionState& getInteractionState() const { return m_interactionState; }
```

**Key constraint**: While `InteractionState.isActive`, suppress ALL other right-click actions (placement, rightclick callbacks on OTHER blocks). The player must release RMB or the interaction must end/cancel before they can interact with a different block.

### How to Wire Interaction Updates in GameApp

The sustained interaction update happens in the game's per-tick update, NOT via command queue. The flow in `GameApp::tick()` (or wherever the per-frame update runs):

```cpp
// In GameApp's per-tick update (after input polling, before command processing)

// 1. Handle interaction cancel conditions
if (m_player.isInteracting())
{
    const auto& state = m_player.getInteractionState();

    // Distance check
    float dist = glm::distance(
        glm::vec3(state.targetBlockPos) + glm::vec3(0.5f),
        glm::vec3(m_player.getPosition()));
    if (dist > 2.5f) // 2 blocks + half-block tolerance
    {
        // Look up block def, invoke on_interact_cancel
        const auto& def = m_blockRegistry.getBlockType(state.targetBlockId);
        m_callbackInvoker.invokeOnInteractCancel(
            def, state.targetBlockPos, 0, state.elapsedTime, "moved_away");
        m_player.cancelInteraction();
    }

    // Block changed check
    uint16_t currentId = m_chunkManager.getBlock(state.targetBlockPos);
    if (currentId != state.targetBlockId)
    {
        const auto& def = m_blockRegistry.getBlockType(state.targetBlockId);
        m_callbackInvoker.invokeOnInteractCancel(
            def, state.targetBlockPos, 0, state.elapsedTime, "block_changed");
        m_player.cancelInteraction();
    }
}

// 2. Update sustained interaction (tick-driven)
if (m_player.isInteracting())
{
    m_player.updateInteraction(dt); // Increments elapsedTime
    const auto& state = m_player.getInteractionState();
    const auto& def = m_blockRegistry.getBlockType(state.targetBlockId);

    bool shouldContinue = m_callbackInvoker.invokeOnInteractStep(
        def, state.targetBlockPos, 0, state.elapsedTime);

    if (!shouldContinue)
    {
        m_player.stopInteraction(); // Lua said we're done
    }
}

// 3. Handle RMB release during interaction
if (m_player.isInteracting() && m_input->wasMouseButtonReleased(GLFW_MOUSE_BUTTON_RIGHT))
{
    const auto& state = m_player.getInteractionState();
    const auto& def = m_blockRegistry.getBlockType(state.targetBlockId);
    m_callbackInvoker.invokeOnInteractStop(
        def, state.targetBlockPos, 0, state.elapsedTime);
    m_player.stopInteraction();
}
```

### Where to Hook RMB Press (Critical Modification)

The existing right-click handling in `GameApp.cpp` (~line 290):

```cpp
// CURRENT CODE (before this story):
bool placementAttempted = m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)
    && m_raycastResult.hit;
if (placementAttempted) {
    uint16_t blockId = resolveHotbarBlockId(m_hotbarSlot);
    if (blockId != BLOCK_AIR) {
        m_commandQueue.push(GameCommand{
            CommandType::PlaceBlock, 0, 0,
            PlaceBlockPayload{m_raycastResult.previousPos, blockId}
        });
    }
}
```

**After this story**, replace with the priority chain:

```cpp
if (m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)
    && !ImGui::GetIO().WantCaptureMouse
    && !m_player.isInteracting())
{
    if (m_raycastResult.hit)
    {
        uint16_t targetId = m_chunkManager.getBlock(m_raycastResult.blockPos);
        const auto& targetDef = m_blockRegistry.getBlockType(targetId);

        // Priority 1: Sustained interaction
        if (targetDef.callbacks && targetDef.callbacks->onInteractStart.has_value())
        {
            bool started = m_callbackInvoker.invokeOnInteractStart(
                targetDef, m_raycastResult.blockPos, 0);
            if (started)
            {
                m_player.startInteraction(m_raycastResult.blockPos, targetId);
                return; // or skip placement below
            }
        }

        // Priority 2: Instant rightclick
        if (targetDef.callbacks && targetDef.callbacks->onRightclick.has_value())
        {
            m_callbackInvoker.invokeOnRightclick(
                targetDef, m_raycastResult.blockPos, ...);
            return; // skip placement
        }

        // Priority 3: Default placement
        uint16_t blockId = resolveHotbarBlockId(m_hotbarSlot);
        if (blockId != BLOCK_AIR)
        {
            m_commandQueue.push(GameCommand{
                CommandType::PlaceBlock, 0, 0,
                PlaceBlockPayload{m_raycastResult.previousPos, blockId}
            });
        }
    }
    else
    {
        // No block targeted — check on_secondary_use for held item
        // V1: stub — items don't have callbacks yet (just log)
    }
}
```

### Where to Hook LMB Punch

Add punch invocation BEFORE the existing mining update call in GameApp:

```cpp
// On LMB press frame (one-shot)
if (m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)
    && !ImGui::GetIO().WantCaptureMouse
    && m_raycastResult.hit)
{
    uint16_t targetId = m_chunkManager.getBlock(m_raycastResult.blockPos);
    const auto& targetDef = m_blockRegistry.getBlockType(targetId);
    m_callbackInvoker.invokeOnPunch(
        targetDef, m_raycastResult.blockPos, targetId, 0);
}

// Mining continues as before (unchanged):
bool lmbDown = m_input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
bool miningCompleted = m_player.updateMining(dt, m_raycastResult, lmbDown, ...);
```

### Suppressing Placement During Interaction

When `m_player.isInteracting()` is true:
- **Suppress placement commands** — do not push PlaceBlock to command queue
- **Suppress rightclick callbacks** on other blocks — the player is committed to the interaction
- **Do NOT suppress mining** — player can still mine with LMB while holding RMB for interaction (this matches real behavior in Luanti)
- **Do NOT suppress movement** — player can walk (but walking >2 blocks triggers cancel)

### ImGui Guard

The existing codebase already uses `ImGui::GetIO().WantCaptureMouse` for cursor capture toggling. Apply the same guard to all block interaction:

```cpp
const auto& imguiIO = ImGui::GetIO();
if (imguiIO.WantCaptureMouse)
{
    // Skip ALL block interaction: no punch, no rightclick, no placement
    // Mining should also be suppressed (already handled?)
}
```

Check the existing ImGui guard in GameApp to ensure consistency. The guard should be at the top of the interaction handling block, not duplicated per-action.

### Escape Key During Interaction

When the player presses Escape (cursor release), if an interaction is active, cancel it:

```cpp
if (m_input->wasKeyPressed(GLFW_KEY_ESCAPE) && m_player.isInteracting())
{
    const auto& state = m_player.getInteractionState();
    const auto& def = m_blockRegistry.getBlockType(state.targetBlockId);
    m_callbackInvoker.invokeOnInteractCancel(
        def, state.targetBlockPos, 0, state.elapsedTime, "menu_opened");
    m_player.cancelInteraction();
}
```

### Callback Invocation Pattern (Same as 9.2)

Follow the exact same exception-free pattern established in Story 9.2:

```cpp
bool BlockCallbackInvoker::invokeOnInteractStart(
    const BlockDefinition& def,
    const glm::ivec3& pos,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->onInteractStart.has_value())
        return false; // Default: no sustained interaction

    auto posTable = posToTable(m_lua, pos);
    sol::protected_function_result result =
        (*def.callbacks->onInteractStart)(posTable, playerId);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua on_interact_start error for '{}': {}",
            def.stringId, err.what());
        return false; // Default on error: don't start interaction
    }

    return result.get_type() == sol::type::boolean
        ? result.get<bool>()
        : false;
}
```

**Default return values per callback:**
| Callback | Default (nil/missing) | Default (error) |
|----------|----------------------|-----------------|
| `on_rightclick` | no-op (fall through to placement) | no-op (fall through) |
| `on_punch` | no-op | no-op |
| `on_secondary_use` | no-op | no-op |
| `on_interact_start` | `false` (don't start) | `false` |
| `on_interact_step` | `false` (end interaction) | `false` |
| `on_interact_stop` | no-op | no-op |
| `on_interact_cancel` | `true` (allow cancel) | `true` |

### GameCommand Extension

Add to `GameCommand.h`:

```cpp
enum class InteractAction : uint8_t
{
    Rightclick,
    Punch,
    SecondaryUse
};

struct InteractBlockPayload
{
    math::IVec3 position;
    InteractAction action;
};
```

Add `InteractBlock` to the `CommandType` enum and `InteractBlockPayload` to the variant.

**Note**: Only discrete actions go through the command queue (rightclick, punch, secondary_use). Sustained interaction (start/step/stop/cancel) is managed directly by PlayerController because it's continuous tick-driven state, not discrete commands. This matches the existing mining pattern where `updateMining` runs every tick directly, and only the final BreakBlock goes through the command queue.

### What NOT to Do

- **DO NOT add sol2 headers to PlayerController.h** — the interaction state is pure C++ (no sol types). The `BlockCallbackInvoker` (which uses sol2) is only accessed in the `.cpp` files.
- **DO NOT modify ScriptEngine** — it's unchanged since Story 9.1.
- **DO NOT implement `on_timer`, ABM, or LBM** — that's Story 9.4.
- **DO NOT implement `on_neighbor_changed`** — that's Story 9.5.
- **DO NOT implement `on_entity_inside` or entity callbacks** — that's Story 9.6.
- **DO NOT implement metadata or inventory APIs** — that's Story 9.7.
- **DO NOT implement `voxel.get_block`, `voxel.set_block` world APIs** — that's Story 9.8. The `on_interact_step` callback in the epic example calls `voxel.set_block` but that API won't exist yet. The callback still fires — it just can't modify the world from Lua until 9.8. Tests should only verify callback invocation, not world modification from Lua.
- **DO NOT implement `voxel.on()` event hooks** — that's Story 9.10.
- **DO NOT implement item callbacks** — `on_secondary_use` is defined as a BLOCK callback (on the held block type). Item-specific callbacks come in a future story.
- **DO NOT implement damage-based cancel** — no damage system exists yet. Add a TODO comment for wiring it when health/damage is implemented.
- **DO NOT add new EventBus event types** for interaction — global event hooks come in Story 9.10. For now, the callbacks are per-block only.
- **DO NOT change the mining system** — `PlayerController::updateMining` is unchanged. Punch is an additional one-shot callback, not a replacement for mining.

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `BlockCallbacks` | `engine/include/voxel/scripting/BlockCallbacks.h` | Extend with 7 interaction callback fields |
| `BlockCallbackInvoker` | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | Add 7 new invoke methods |
| `LuaBindings` | `engine/src/scripting/LuaBindings.cpp` | Add callback extraction in `parseBlockDefinition` |
| `PlayerController` | `engine/include/voxel/game/PlayerController.h` | Add InteractionState, interaction methods |
| `PlayerController::updateMining` | `engine/src/game/PlayerController.cpp` | Reference pattern for interaction update |
| `GameApp` | `game/src/GameApp.cpp` | Wire interaction priority chain into input handling |
| `GameCommand` | `engine/include/voxel/game/GameCommand.h` | Add InteractBlock command type |
| `InputManager` | `engine/include/voxel/input/InputManager.h` | `wasMouseButtonPressed`, `wasMouseButtonReleased`, `isMouseButtonDown` |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | Look up BlockDefinition for callback access |
| `ChunkManager` | `engine/src/world/ChunkManager.cpp` | `getBlock(pos)` to verify target block hasn't changed |
| `Raycast` | `engine/include/voxel/physics/Raycast.h` | `RaycastResult` for block targeting |
| `Result<T>` | `engine/include/voxel/core/Result.h` | Error handling |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging callback errors |
| `posToTable` / `tableToPos` | `engine/src/scripting/BlockCallbackInvoker.cpp` | Position conversion utility from 9.2 |

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| MODIFY | `engine/include/voxel/scripting/BlockCallbacks.h` | `voxel::scripting` | Add 7 interaction callback fields |
| MODIFY | `engine/src/scripting/LuaBindings.cpp` | `voxel::scripting` | Extract interaction callbacks from Lua table |
| MODIFY | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | `voxel::scripting` | Add 7 invoke method declarations |
| MODIFY | `engine/src/scripting/BlockCallbackInvoker.cpp` | `voxel::scripting` | Implement 7 invoke methods |
| MODIFY | `engine/include/voxel/game/PlayerController.h` | `voxel::game` | Add InteractionState struct + interaction methods |
| MODIFY | `engine/src/game/PlayerController.cpp` | `voxel::game` | Implement interaction state management |
| MODIFY | `engine/include/voxel/game/GameCommand.h` | `voxel::game` | Add InteractBlock command type + payload |
| MODIFY | `game/src/GameApp.cpp` | — | Wire interaction priority chain, punch, sustained updates |
| NEW | `tests/scripting/TestBlockInteraction.cpp` | — | Integration tests for interaction callbacks |
| NEW | `tests/scripting/test_scripts/interaction_rightclick.lua` | — | Test: block with on_rightclick |
| NEW | `tests/scripting/test_scripts/interaction_sustained.lua` | — | Test: block with multi-phase interaction |
| NEW | `tests/scripting/test_scripts/interaction_punch.lua` | — | Test: block with on_punch |
| NEW | `tests/scripting/test_scripts/interaction_priority.lua` | — | Test: block with both on_interact_start and on_rightclick |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestBlockInteraction.cpp |

### Naming & Style

- Structs: `InteractionState`, `InteractBlockPayload` (PascalCase)
- Methods: `startInteraction`, `updateInteraction`, `cancelInteraction`, `invokeOnRightclick` (camelCase)
- Members: `m_interactionState`, `m_isActive` (m_ prefix)
- Enums: `enum class InteractAction { Rightclick, Punch, SecondaryUse }` (PascalCase)
- Namespace: `voxel::game` for InteractionState/PlayerController, `voxel::scripting` for callback invokers
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

**From Story 9.2:**
- `BlockCallbacks` struct with `unique_ptr` indirection in `BlockDefinition` — extend this struct
- `BlockCallbackInvoker` with `posToTable`/`tableToPos` utilities — add new invoke methods here
- `LuaBindings::parseBlockDefinition` extracts callbacks from Lua table — add 7 more fields
- Callback invocation pattern: check `has_value()` → call → check `.valid()` → log error → return default
- `voxel.register_block(table)` API already works — interaction callbacks are just additional table fields
- Bootstrap order: ScriptEngine → LuaBindings → loadScript

**From existing PlayerController:**
- `MiningState` pattern: struct with active flag, target pos, elapsed time — InteractionState follows same pattern
- `updateMining(dt, raycastResult, lmbDown, ...)` called per-tick — `updateInteraction(dt)` follows same tick-driven approach
- Player AABB: 0.6 x 1.8, eye height 1.62
- `m_position` is `glm::dvec3` — use `glm::vec3` cast for distance calculations

**From existing GameApp:**
- RMB handling at ~line 290, LMB mining at ~line 276, command processing at ~line 306
- ImGui check pattern: `ImGui::GetIO().WantCaptureMouse`
- Raycast result stored as `m_raycastResult` member, updated every frame
- Command queue: `m_commandQueue.push(GameCommand{...})`

### Git Intelligence

Recent commits are `feat(renderer)` and `feat(world)` for Epic 8 (Lighting). No scripting code has been committed yet. Stories 9.1 and 9.2 must be implemented before this story.

Commit style for this story: `feat(scripting): implement block interaction and multi-phase hold callbacks`

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/BlockCallbacks.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"
#include "voxel/world/BlockRegistry.h"

using namespace voxel::scripting;

TEST_CASE("Block interaction callbacks", "[scripting][interaction]")
{
    ScriptEngine engine;
    REQUIRE(engine.init().has_value());
    auto& lua = engine.getLuaState();

    // Set up block registry and bindings (same as 9.2 tests)
    voxel::world::BlockRegistry registry;
    LuaBindings::registerBlockAPI(lua, registry);

    SECTION("on_rightclick fires and receives position")
    {
        engine.loadScript("tests/scripting/test_scripts/interaction_rightclick.lua");
        // Verify block registered with on_rightclick callback
        const auto& def = registry.getBlockType("test:interactive_block");
        REQUIRE(def.callbacks != nullptr);
        REQUIRE(def.callbacks->onRightclick.has_value());

        // Invoke and verify
        BlockCallbackInvoker invoker(lua);
        invoker.invokeOnRightclick(def, {1, 2, 3}, ...);
        // Check Lua side effects (e.g., global variable set)
        REQUIRE(lua["test_rightclick_pos_x"].get<int>() == 1);
    }

    SECTION("on_interact_start returns bool")
    {
        engine.loadScript("tests/scripting/test_scripts/interaction_sustained.lua");
        const auto& def = registry.getBlockType("test:grindstone");
        REQUIRE(def.callbacks->onInteractStart.has_value());

        BlockCallbackInvoker invoker(lua);
        bool started = invoker.invokeOnInteractStart(def, {5, 10, 5}, 0);
        REQUIRE(started == true);
    }

    SECTION("on_interact_start takes priority over on_rightclick")
    {
        engine.loadScript("tests/scripting/test_scripts/interaction_priority.lua");
        const auto& def = registry.getBlockType("test:priority_block");
        REQUIRE(def.callbacks->onInteractStart.has_value());
        REQUIRE(def.callbacks->onRightclick.has_value());

        // When on_interact_start exists, it should be checked first
        // The test verifies the priority chain logic
    }

    SECTION("missing callbacks return safe defaults")
    {
        engine.loadScript("tests/scripting/test_scripts/register_block.lua");
        const auto& def = registry.getBlockType("test:plain_block");
        // No interaction callbacks → invokers return defaults
        BlockCallbackInvoker invoker(lua);
        REQUIRE(invoker.invokeOnInteractStart(def, {0, 0, 0}, 0) == false);
    }
}
```

Test Lua files:

**interaction_rightclick.lua:**
```lua
voxel.register_block({
    id = "test:interactive_block",
    on_rightclick = function(pos, node, clicker, itemstack, pointed_thing)
        test_rightclick_pos_x = pos.x
        test_rightclick_pos_y = pos.y
        test_rightclick_pos_z = pos.z
        return itemstack
    end,
})
```

**interaction_sustained.lua:**
```lua
voxel.register_block({
    id = "test:grindstone",
    on_interact_start = function(pos, player)
        test_interact_started = true
        return true
    end,
    on_interact_step = function(pos, player, elapsed)
        test_interact_elapsed = elapsed
        if elapsed >= 3.0 then
            return false -- done
        end
        return true -- continue
    end,
    on_interact_stop = function(pos, player, elapsed)
        test_interact_stopped = true
    end,
    on_interact_cancel = function(pos, player, elapsed, reason)
        test_cancel_reason = reason
        return true
    end,
})
```

**interaction_priority.lua:**
```lua
voxel.register_block({
    id = "test:priority_block",
    on_interact_start = function(pos, player)
        test_which_fired = "interact_start"
        return true
    end,
    on_rightclick = function(pos, node, clicker, itemstack, pointed_thing)
        test_which_fired = "rightclick"
        return itemstack
    end,
})
```

### Project Structure Notes

- No new directories created — all files extend existing `scripting/`, `game/`, and `tests/scripting/` directories
- `InteractionState` is defined inline in `PlayerController.h` (not a separate file) — it's a simple POD struct tightly coupled to PlayerController
- Test scripts go in `tests/scripting/test_scripts/` alongside 9.1 and 9.2 test scripts

### Future Story Dependencies

This story establishes patterns used by:
- **Story 9.4**: `on_timer` callback follows the same invoke pattern; ABM/LBM tick similarly to sustained interaction
- **Story 9.6**: `on_entity_step_on` / `on_entity_inside` will need similar per-tick invocation as `on_interact_step`
- **Story 9.8**: `voxel.set_block` API will allow `on_interact_step` to actually modify the world (grindstone example)
- **Story 9.10**: `voxel.on("player_interact", ...)` global hook fires BEFORE per-block callbacks from this story
- **Story 9.10**: Input events (`mouse_click`, `mouse_held`, `mouse_released`) relate to but don't replace these per-block callbacks

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.3 full specification]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 9: Scripting, ADR-007, ADR-010: Command Pattern]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards, threading rules]
- [Source: _bmad-output/implementation-artifacts/9-1-sol2-luajit-integration.md — ScriptEngine design, sol2 exception-free patterns]
- [Source: _bmad-output/implementation-artifacts/9-2-block-registration-placement-destruction-callbacks.md — BlockCallbacks, BlockCallbackInvoker, LuaBindings, callback invocation pattern]
- [Source: engine/include/voxel/input/InputManager.h — Mouse button hold tracking, wasMouseButtonPressed/Released]
- [Source: engine/include/voxel/game/PlayerController.h — MiningState pattern, updateMining, player position]
- [Source: engine/include/voxel/game/GameCommand.h — CommandType, PlaceBlockPayload, BreakBlockPayload]
- [Source: engine/include/voxel/game/EventBus.h — BlockPlacedEvent, BlockBrokenEvent]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition, callbacks unique_ptr]
- [Source: engine/include/voxel/physics/Raycast.h — RaycastResult, MAX_REACH]
- [Source: game/src/GameApp.cpp — Command processing, RMB placement, LMB mining, ImGui guard]
- [Source: _bmad-output/planning-artifacts/ux-spec.md — Control scheme, block interaction behavior]

## Dev Agent Record

### Agent Model Used

{{agent_model_name_version}}

### Debug Log References

### Completion Notes List

### File List
