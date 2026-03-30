# Story 9.10: Global Event Hooks

Status: ready-for-dev

## Story

As a developer,
I want Lua scripts to subscribe to engine-wide events beyond per-block callbacks,
so that mods can react to player actions, world events, and game lifecycle.

## Acceptance Criteria

1. `voxel.on(event_name, callback)` registers a Lua callback for any of the 41 named events across 7 categories (Player 12, Block 6, World 5, Time 3, Input 10, Engine 3, Mod 2).
2. Multiple callbacks per event, invoked in registration order; error in one does not prevent others (logged via `VX_LOG_WARN`).
3. Cancelable events (`player_interact`, `block_dig_start`, `key_pressed`, `key_double_tap`, `mouse_click`, `scroll_wheel`) return `false` from Lua to cancel the engine action. First `false` wins; subsequent callbacks still fire but the action is blocked.
4. `voxel.register_combo(name, keys, window, callback)` detects multi-key sequences within a time window.
5. `tick` callbacks that exceed 2 ms per invocation are logged as warnings (performance monitoring).
6. `player_move` is throttled to once per simulation tick (20 Hz), not per frame.
7. Event dispatch ordering: `player_interact` → per-block callbacks (9.2–9.7) → global `block_placed`/`block_broken`.
8. All events fire at correct engine integration points (game loop, command processing, chunk load/unload, etc.).
9. Integration test: register `player_interact` returning false for "break", verify player can't break any block. Register `block_placed` global hook, place a block, verify callback fires with correct pos/id.

## Tasks / Subtasks

- [ ] Task 1: GlobalEventRegistry class (AC: 1, 2, 3)
  - [ ] 1.1 Create `GlobalEventRegistry.h` — stores `std::unordered_map<std::string, std::vector<sol::protected_function>>` for named events
  - [ ] 1.2 Create `GlobalEventRegistry.cpp` — `registerCallback(name, fn)`, `fireEvent(name, args...)`, `fireCancelableEvent(name, args...) -> bool`
  - [ ] 1.3 Add `clearAll()` for future hot-reload (9.11)
  - [ ] 1.4 Validate event names against allowed set on registration (log warning for unknown events)
- [ ] Task 2: InputEventTracker class (AC: 1, 6)
  - [ ] 2.1 Create `InputEventTracker.h` — wraps InputManager to produce Lua event data: key press/release/held/double-tap, mouse click/release/held, scroll
  - [ ] 2.2 Create `InputEventTracker.cpp` — `update(InputManager&, dt)` polls InputManager state, computes hold durations, emits events via GlobalEventRegistry
  - [ ] 2.3 Translate GLFW key codes to human-readable strings ("w", "a", "space", "escape", etc.)
  - [ ] 2.4 `key_held` and `mouse_held` fire every tick (not every frame) with accumulated hold duration
- [ ] Task 3: ComboDetector class (AC: 4)
  - [ ] 3.1 Create `ComboDetector.h` — stores registered combos as `{name, keys[], window_sec, callback}`
  - [ ] 3.2 Create `ComboDetector.cpp` — ring buffer of recent key presses with timestamps; on each key press, check all combos for match within window
  - [ ] 3.3 Wire `voxel.register_combo()` in LuaBindings
- [ ] Task 4: Lua API registration (AC: 1, 4)
  - [ ] 4.1 Add `LuaBindings::registerGlobalEventAPI(lua, registry, comboDetector)` — binds `voxel.on()` and `voxel.register_combo()`
  - [ ] 4.2 Update `LuaBindings.h` with forward declarations and new method signature
- [ ] Task 5: Engine integration — fire events from game systems (AC: 5, 6, 7, 8)
  - [ ] 5.1 `GameApp::tick()` — fire `tick` event with dtime; add timing check for 2 ms warning
  - [ ] 5.2 `GameApp::tick()` — fire player movement events: `player_move` (throttled, only if position changed), `player_jump`, `player_land`, `player_sprint_toggle`, `player_sneak_toggle`
  - [ ] 5.3 `GameApp::handleBlockInteraction()` — fire `player_interact` (cancelable) BEFORE per-block callbacks; fire `block_dig_start` (cancelable) when mining begins
  - [ ] 5.4 After block place/break command execution — fire `block_placed`, `block_broken`, `block_changed`
  - [ ] 5.5 `GameApp::tick()` — fire input events via InputEventTracker (key/mouse/scroll)
  - [ ] 5.6 ChunkManager load/unload hooks — fire `chunk_loaded`, `chunk_unloaded`, `chunk_generated`
  - [ ] 5.7 BlockTimerManager — fire `block_timer_fired` after each timer callback
  - [ ] 5.8 Neighbor notifier — fire `block_neighbor_changed` after per-block neighbor callbacks
  - [ ] 5.9 Time system — fire `day_phase_changed` and `new_day` (stub: V1 has no day/night cycle advancing time, so these fire only when `voxel.set_time_of_day()` triggers a phase transition, or are deferred to post-9.8)
  - [ ] 5.10 Engine lifecycle — fire `shutdown` in GameApp destructor; `mod_loaded`/`all_mods_loaded` stubs for 9.11
  - [ ] 5.11 `hotbar_changed` — fire when hotbar slot changes in input handling
- [ ] Task 6: EventBus extensions (AC: 8)
  - [ ] 6.1 Add new EventType entries: `ChunkUnloaded`, `ChunkGenerated`, `BlockChanged`
  - [ ] 6.2 Add corresponding event structs
  - [ ] 6.3 Subscribe GlobalEventRegistry to EventBus where appropriate (bridge C++ events → Lua events)
- [ ] Task 7: Tests (AC: 9)
  - [ ] 7.1 Create `tests/scripting/TestGlobalEventHooks.cpp` with Catch2 integration tests
  - [ ] 7.2 Create Lua test scripts: `global_events_cancel.lua`, `global_events_block.lua`, `global_events_input.lua`
  - [ ] 7.3 Test cancelable event prevents action
  - [ ] 7.4 Test multiple callbacks fire in order
  - [ ] 7.5 Test error in callback doesn't prevent others
  - [ ] 7.6 Test combo detection fires after key sequence within window
  - [ ] 7.7 Test tick timing warning threshold
- [ ] Task 8: CMakeLists + GameApp wiring (AC: all)
  - [ ] 8.1 Add new source files to `engine/CMakeLists.txt`
  - [ ] 8.2 Add test files to `tests/CMakeLists.txt`
  - [ ] 8.3 Wire GlobalEventRegistry, InputEventTracker, ComboDetector into GameApp (member variables, init, shutdown order)

## Dev Notes

### Architecture Overview

This story introduces a **parallel event system** to the existing per-block callbacks. The existing `BlockCallbacks` + `BlockCallbackInvoker` handle events scoped to a specific block type. `GlobalEventRegistry` handles engine-wide events any mod can subscribe to via `voxel.on()`.

```
Input/Game Systems
       │
       ▼
┌──────────────────┐     ┌──────────────────────┐
│ player_interact   │────►│ Per-block callbacks   │
│ (cancelable)      │     │ (9.2-9.7 system)      │
└────────┬─────────┘     └──────────┬────────────┘
         │                          │
         ▼                          ▼
┌──────────────────┐     ┌──────────────────────┐
│ GlobalEventRegistry│◄──│ block_placed/broken   │
│ (this story)       │    │ (fires AFTER per-block)│
└────────────────────┘    └──────────────────────┘
```

### GlobalEventRegistry Design

```cpp
// engine/include/voxel/scripting/GlobalEventRegistry.h
namespace voxel::scripting {

class GlobalEventRegistry {
public:
    /// Register a callback for a named event.
    void registerCallback(const std::string& eventName, sol::protected_function callback);

    /// Fire a non-cancelable event. All callbacks run; errors logged.
    template <typename... Args>
    void fireEvent(const std::string& eventName, Args&&... args);

    /// Fire a cancelable event. Returns true if any callback returned false.
    template <typename... Args>
    [[nodiscard]] bool fireCancelableEvent(const std::string& eventName, Args&&... args);

    /// Remove all registered callbacks (for hot-reload).
    void clearAll();

    /// Number of callbacks registered for a given event.
    [[nodiscard]] size_t callbackCount(const std::string& eventName) const;

private:
    std::unordered_map<std::string, std::vector<sol::protected_function>> m_callbacks;

    /// Set of valid event names. Log warning on unknown event registration.
    static const std::unordered_set<std::string> VALID_EVENTS;
};

} // namespace voxel::scripting
```

**Key implementation rules:**
- `fireEvent` iterates all callbacks, wraps each call in `sol::protected_function_result`, checks `.valid()`, logs errors via `VX_LOG_WARN("Lua global event '{}' callback error: {}", eventName, err.what())`
- `fireCancelableEvent` does the same but also checks if any callback returned `false` (sol boolean). Accumulates a `bool cancelled = false` flag. All callbacks still fire regardless.
- Template implementations go in the `.h` file (or a `.inl` file) since they depend on variadic args.

### InputEventTracker Design

```cpp
// engine/include/voxel/scripting/InputEventTracker.h
namespace voxel::scripting {

class InputEventTracker {
public:
    /// Poll InputManager and fire input events via registry.
    void update(
        const input::InputManager& input,
        GlobalEventRegistry& registry,
        sol::state& lua,
        float dt);

private:
    /// Translate GLFW key code to Lua-friendly name.
    static std::string_view keyName(int glfwKey);
    /// Translate GLFW mouse button to Lua-friendly name.
    static std::string_view buttonName(int glfwButton);

    static constexpr int MAX_TRACKED_KEYS = 512;
    static constexpr int MAX_TRACKED_BUTTONS = 8;
};

} // namespace voxel::scripting
```

**Key name mapping**: Use a static `std::array` or `switch` mapping GLFW constants → lowercase strings. Common keys: `"w"`, `"a"`, `"s"`, `"d"`, `"space"`, `"left_shift"`, `"left_ctrl"`, `"escape"`, `"1"`-`"9"`, `"f1"`-`"f12"`, etc. Unknown keys → `"unknown_XXX"` with GLFW code.

**InputManager already tracks** (from `InputManager.h`):
- `wasKeyPressed(key)`, `wasKeyReleased(key)`, `isKeyDown(key)`, `getKeyHoldDuration(key)`, `wasKeyDoubleTapped(key)`
- `wasMouseButtonPressed(btn)`, `wasMouseButtonReleased(btn)`, `isMouseButtonDown(btn)`, `getMouseButtonHoldDuration(btn)`
- `getScrollDelta()`

This means InputEventTracker does NOT need to track state itself — it reads from InputManager each tick and fires the appropriate events. The loop structure:

```cpp
void InputEventTracker::update(...) {
    // Keys
    for (int k = 0; k < MAX_TRACKED_KEYS; ++k) {
        if (input.wasKeyPressed(k)) {
            bool cancelled = registry.fireCancelableEvent("key_pressed", playerHandle, keyName(k));
            // 'cancelled' can be used by caller to suppress further key processing
        }
        if (input.wasKeyReleased(k)) {
            registry.fireEvent("key_released", playerHandle, keyName(k), input.getKeyHoldDuration(k));
        }
        if (input.isKeyDown(k)) {
            registry.fireEvent("key_held", playerHandle, keyName(k), input.getKeyHoldDuration(k));
        }
        if (input.wasKeyDoubleTapped(k)) {
            registry.fireCancelableEvent("key_double_tap", playerHandle, keyName(k));
        }
    }
    // Mouse buttons (0-7)
    for (int b = 0; b < MAX_TRACKED_BUTTONS; ++b) {
        if (input.wasMouseButtonPressed(b)) {
            registry.fireCancelableEvent("mouse_click", playerHandle, buttonName(b), ...pos, blockId);
        }
        // ... similar for released, held
    }
    // Scroll
    float scroll = input.getScrollDelta();
    if (scroll != 0.0f) {
        int delta = scroll > 0 ? 1 : -1;
        registry.fireCancelableEvent("scroll_wheel", playerHandle, delta);
    }
}
```

**Performance note:** Iterating 512 keys each tick is cheap — `wasKeyPressed` etc. are just array lookups. Only keys that had events will trigger Lua calls.

### ComboDetector Design

```cpp
// engine/include/voxel/scripting/ComboDetector.h
namespace voxel::scripting {

struct ComboRegistration {
    std::string name;
    std::vector<std::string> keys;  // Sequence of key names
    float windowSeconds;            // Max time for entire sequence
    sol::protected_function callback;
};

class ComboDetector {
public:
    void registerCombo(const std::string& name, std::vector<std::string> keys,
                       float windowSec, sol::protected_function callback);

    /// Called on each key press. Checks all combos for completion.
    void onKeyPress(const std::string& keyName, float currentTime, sol::object playerHandle);

    void clearAll();

private:
    struct KeyEvent {
        std::string key;
        float timestamp;
    };

    std::vector<ComboRegistration> m_combos;
    std::vector<KeyEvent> m_recentKeys;  // Ring buffer of recent presses
    static constexpr size_t MAX_RECENT = 16;
};

} // namespace voxel::scripting
```

### Event Dispatch Integration Points

Each event must fire from a specific place in the engine. Here is the exact integration map:

| Event | Fire Location | Cancelable | Notes |
|-------|---------------|------------|-------|
| **Player Events** | | | |
| `player_join` | `GameApp::init()` after player spawn | No | V1: single player, fires once |
| `player_leave` | `GameApp::~GameApp()` | No | V1: fires on shutdown |
| `player_respawn` | Stub — no respawn system yet | No | Deferred |
| `player_damage` | Stub — no health system yet | No | Deferred |
| `player_death` | Stub — no health system yet | No | Deferred |
| `player_move` | `GameApp::tick()` after `tickPhysics()` — compare prev/current pos | No | Throttled to tick rate (already is) |
| `player_jump` | `GameApp::tick()` when Jump command processed | No | |
| `player_land` | `GameApp::tick()` when `m_player.justLanded()` | No | Use `consumeFallDistance()` |
| `player_sprint_toggle` | `GameApp::tick()` when sprint state changes | No | Track prev sprint state |
| `player_sneak_toggle` | `GameApp::tick()` when sneak state changes | No | Track prev sneak state |
| `player_interact` | `GameApp::handleBlockInteraction()` BEFORE per-block callbacks | **Yes** | Return false → skip block callbacks |
| `player_hotbar_changed` | `GameApp::handleInputToggles()` when slot changes | No | `m_prevHotbarSlot` already tracked |
| **Block Events** | | | |
| `block_placed` | After PlaceBlock command executes successfully | No | Fires after per-block `afterPlace` |
| `block_broken` | After BreakBlock command executes successfully | No | Fires after per-block `afterDestruct` |
| `block_changed` | `ChunkManager::setBlock()` or via EventBus bridge | No | Fires for ANY block change |
| `block_neighbor_changed` | `NeighborNotifier` after per-block `onNeighborChanged` | No | |
| `block_dig_start` | `GameApp::handleBlockInteraction()` when mining starts | **Yes** | Return false → prevent mining |
| `block_timer_fired` | `BlockTimerManager::update()` after `onTimer` fires | No | |
| **World Events** | | | |
| `chunk_loaded` | Subscribe to EventBus `ChunkLoaded` | No | Add `fromDisk` flag to event struct |
| `chunk_unloaded` | New EventBus event `ChunkUnloaded` | No | Fire when column removed from manager |
| `chunk_generated` | New EventBus event `ChunkGenerated` | No | First-time generation only |
| `world_saved` | After chunk serialization flush | No | V1: stub until explicit save |
| `section_meshed` | After mesh task completes | No | V1: stub or low-priority |
| **Time Events** | | | |
| `tick` | `GameApp::tick()` — first thing after input | No | Measure callback duration |
| `day_phase_changed` | When `voxel.set_time_of_day()` crosses a phase boundary | No | V1: fires only on script-driven time changes |
| `new_day` | When day counter increments | No | V1: stub |
| **Input Events** | | | |
| `key_pressed` | `InputEventTracker::update()` | **Yes** | Return false → consume key |
| `key_released` | `InputEventTracker::update()` | No | |
| `key_held` | `InputEventTracker::update()` | No | Every tick while held |
| `key_double_tap` | `InputEventTracker::update()` | **Yes** | Return false → consume |
| `mouse_click` | `InputEventTracker::update()` | **Yes** | Return false → consume |
| `mouse_released` | `InputEventTracker::update()` | No | |
| `mouse_held` | `InputEventTracker::update()` | No | Every tick while held |
| `scroll_wheel` | `InputEventTracker::update()` | **Yes** | Return false → prevent hotbar scroll |
| **Engine Events** | | | |
| `shutdown` | `GameApp::~GameApp()` before Lua state teardown | No | |
| `hot_reload_start` | Stub for 9.11 | No | |
| `hot_reload_complete` | Stub for 9.11 | No | |
| **Mod Events** | | | |
| `mod_loaded` | Stub for 9.11 | No | |
| `all_mods_loaded` | After `loadScript()` in `GameApp::init()` | No | V1: fires after base script loads |

### Cancelable Event Wiring Pattern

For `player_interact` (the most critical cancelable event):

```cpp
// In GameApp::handleBlockInteraction():
void GameApp::handleBlockInteraction(float dt) {
    // ... existing raycast check ...

    if (m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) && m_raycastResult.hit) {
        // Fire global cancelable event FIRST
        std::string action = "break";
        bool cancelled = m_globalEvents->fireCancelableEvent(
            "player_interact",
            entityHandle, action, posTable, blockStringId);

        if (cancelled) return; // Skip all per-block callbacks and command

        // Existing per-block callback chain (canDig, onDig, etc.)
        // ... existing code ...
    }
}
```

### Tick Event Timing Pattern

```cpp
// In GameApp::tick():
{
    auto start = std::chrono::steady_clock::now();
    m_globalEvents->fireEvent("tick", dt);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
    if (ms > 2.0) {
        VX_LOG_WARN("Global 'tick' callbacks took {:.2f}ms (threshold: 2ms)", ms);
    }
}
```

### Files to Create (6)

| File | Purpose | Est. Lines |
|------|---------|------------|
| `engine/include/voxel/scripting/GlobalEventRegistry.h` | Event callback storage + dispatch | ~80 |
| `engine/src/scripting/GlobalEventRegistry.cpp` | Implementation + VALID_EVENTS set | ~120 |
| `engine/include/voxel/scripting/InputEventTracker.h` | Input → Lua event bridge | ~50 |
| `engine/src/scripting/InputEventTracker.cpp` | GLFW key mapping + event firing | ~200 |
| `engine/include/voxel/scripting/ComboDetector.h` | Combo registration + detection | ~50 |
| `engine/src/scripting/ComboDetector.cpp` | Ring buffer matching logic | ~100 |
| `tests/scripting/TestGlobalEventHooks.cpp` | Integration tests | ~300 |
| `tests/scripting/test_scripts/global_events_cancel.lua` | Cancel event test | ~30 |
| `tests/scripting/test_scripts/global_events_block.lua` | Block event test | ~20 |
| `tests/scripting/test_scripts/global_events_input.lua` | Input event test | ~30 |

### Files to Modify (7)

| File | Changes |
|------|---------|
| `engine/include/voxel/scripting/LuaBindings.h` | Add `registerGlobalEventAPI()` method, forward declarations for GlobalEventRegistry and ComboDetector |
| `engine/src/scripting/LuaBindings.cpp` | Implement `registerGlobalEventAPI()` — bind `voxel.on()` and `voxel.register_combo()` |
| `engine/include/voxel/game/EventBus.h` | Add `ChunkUnloaded`, `ChunkGenerated`, `BlockChanged` event types + structs + traits |
| `game/src/GameApp.h` | Add members: `m_globalEvents`, `m_inputEventTracker`, `m_comboDetector`; track prev position/sprint/sneak state |
| `game/src/GameApp.cpp` | Wire event firing in `tick()`, `handleBlockInteraction()`, `handleInputToggles()`; init/shutdown new systems |
| `engine/CMakeLists.txt` | Add 4 new source files (GlobalEventRegistry, InputEventTracker, ComboDetector) |
| `tests/CMakeLists.txt` | Add `TestGlobalEventHooks.cpp` |

### Callback Invocation Pattern (Reuse from 9.2–9.9)

All Lua calls follow the established error-handling pattern:

```cpp
sol::protected_function_result result = callback(arg1, arg2, ...);
if (!result.valid()) {
    sol::error err = result;
    VX_LOG_WARN("Lua global event '{}' error: {}", eventName, err.what());
    // Continue to next callback — do NOT abort
}
```

For cancelable events, also check:
```cpp
if (result.valid() && result.get_type() == sol::type::boolean) {
    if (result.get<bool>() == false) {
        cancelled = true;
    }
}
```

### GameApp Member Destruction Order

Add new members after existing scripting members:

```cpp
// In GameApp.h private section, after m_shapeCache:
std::unique_ptr<voxel::scripting::GlobalEventRegistry> m_globalEvents;
std::unique_ptr<voxel::scripting::InputEventTracker> m_inputEventTracker;
std::unique_ptr<voxel::scripting::ComboDetector> m_comboDetector;

// Tracking state for movement events
glm::dvec3 m_prevPlayerPos{0.0};
bool m_prevSprinting = false;
bool m_prevSneaking = false;
```

Destruction order: ComboDetector → InputEventTracker → GlobalEventRegistry → (existing) ShapeCache → NeighborNotifier → etc.

### EntityHandle Reuse

Story 9.6 created `EntityHandle` wrapping PlayerController for Lua. Reuse it for all player events:

```cpp
auto playerHandle = scripting::EntityHandle(&m_player);
m_globalEvents->fireEvent("player_jump", playerHandle);
```

### Posix/Lua Table Helpers

Reuse the existing `posToTable(lua, ivec3)` helper from prior stories (defined in LuaBindings.cpp) for all position arguments passed to Lua callbacks.

### V1 Scope Limits

- **Player health/damage/death/respawn**: Stubs only — no health system yet. Register the event names in VALID_EVENTS so mods can subscribe, but never fire them in V1.
- **Day/night cycle**: `day_phase_changed` and `new_day` only fire if scripts call `voxel.set_time_of_day()` across a phase boundary. No automatic cycle in V1.
- **world_saved**: Stub — fires at shutdown if chunks are serialized.
- **section_meshed**: Low-priority stub — fires from mesh completion callback if wired.
- **hot_reload_start/complete, mod_loaded**: Registration slots exist but fire is deferred to 9.11.
- **Input event consumption**: When `key_pressed`/`mouse_click` return false (cancelled), the engine should NOT process the key for its normal function (e.g., movement, block interaction). This requires the `InputEventTracker::update()` return value to be checked before processing input in `GameApp::tick()`.

### Performance Considerations

- `key_held` and `mouse_held` fire every tick (20 Hz) per held key/button. Keep Lua callbacks lightweight.
- `player_move` throttled to tick rate automatically (fires in `tick()`, not `render()`).
- `block_changed` can fire many times per tick (batch world modifications). If performance is an issue, consider deferring to end-of-tick batch notification (optimization for later).
- VALID_EVENTS lookup is `O(1)` unordered_set. Event dispatch is `O(n)` per registered callback.

### Project Structure Notes

- All new files follow the established `engine/include/voxel/scripting/` + `engine/src/scripting/` pattern.
- One class per file, PascalCase filenames.
- Tests follow `tests/scripting/TestClassName.cpp` pattern with `[scripting][global-events]` Catch2 tags.
- Lua test scripts go in `tests/scripting/test_scripts/` matching prior story patterns.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md#Story 9.10]
- [Source: _bmad-output/planning-artifacts/architecture.md#System 9: Scripting]
- [Source: _bmad-output/planning-artifacts/architecture.md#ADR-007: Lua/sol2/LuaJIT]
- [Source: _bmad-output/planning-artifacts/architecture.md#ADR-010: Command Pattern + Tick-Based Simulation]
- [Source: engine/include/voxel/game/EventBus.h — existing typed event bus]
- [Source: engine/include/voxel/input/InputManager.h — key/mouse state tracking with hold durations and double-tap]
- [Source: engine/include/voxel/scripting/BlockCallbacks.h — per-block callback structure]
- [Source: engine/include/voxel/scripting/BlockCallbackInvoker.h — callback invocation pattern]
- [Source: engine/include/voxel/game/PlayerController.h — fall tracking, interaction state, entity collision data]
- [Source: engine/include/voxel/scripting/EntityHandle.h — player Lua wrapper from 9.6]
- [Source: _bmad-output/implementation-artifacts/9-9-visual-client-callbacks.md — previous story patterns]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
