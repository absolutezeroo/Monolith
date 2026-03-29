# Story 7.1: Command Queue + Event Bus

Status: review

## Story

As a developer,
I want a command queue for game actions and an event bus for inter-system communication,
so that all state mutation goes through a serializable pipeline (network-ready).

## Acceptance Criteria

1. `GameCommand` struct with `Type` enum (`PlaceBlock`, `BreakBlock`, `MovePlayer`, `Jump`, `ToggleSprint`), `playerId` (`uint32`), `tick` (`uint32`), and `std::variant` payload.
2. `CommandQueue` class with thread-safe `push()` / `tryPop()` (SPSC sufficient for singleplayer).
3. Input handlers push commands — never mutate game state directly.
4. `EventBus` class with typed publish/subscribe using `EventType` enum.
5. Events: `BlockPlacedEvent{pos, blockId}`, `BlockBrokenEvent{pos, previousBlockId}`, `ChunkLoadedEvent{coord}`.
6. Simulation tick consumes command queue, processes each, publishes events.
7. Unit tests: push/pop ordering, event callback invocation.

## Tasks / Subtasks

- [x] Task 1: Create `GameCommand.h` (AC: #1)
  - [x] Define `enum class CommandType : uint8` with all command types
  - [x] Define payload structs: `PlaceBlockPayload`, `BreakBlockPayload`, `MovePlayerPayload`, `JumpPayload`, `ToggleSprintPayload`
  - [x] Define `GameCommand` struct with `type`, `playerId`, `tick`, `std::variant<...> payload`
- [x] Task 2: Create `CommandQueue.h` (AC: #2)
  - [x] Implement `CommandQueue` wrapping `voxel::core::ConcurrentQueue<GameCommand>`
  - [x] Provide `push(GameCommand)`, `tryPop() -> std::optional<GameCommand>`, `size()`, `empty()`, `drain(callback)` convenience
- [x] Task 3: Create `EventBus.h` + `EventBus.cpp` (AC: #4, #5)
  - [x] Define `enum class EventType : uint8` with event types
  - [x] Define event structs: `BlockPlacedEvent`, `BlockBrokenEvent`, `ChunkLoadedEvent`
  - [x] Implement typed `subscribe(EventType, callback)` and `publish(EventType, event)` using type-erased storage
  - [x] Provide `SubscriptionId` handle for future unsubscribe support
- [x] Task 4: Unit tests (AC: #7)
  - [x] `tests/game/TestCommandQueue.cpp` — push/pop FIFO ordering, empty queue returns nullopt, drain consumes all
  - [x] `tests/game/TestEventBus.cpp` — subscribe+publish invokes callback, multiple subscribers, unsubscribe, no callback for wrong event type
  - [x] Add test files to `tests/CMakeLists.txt`
- [x] Task 5: Wire into `GameApp` (AC: #3, #6) — **DO NOT IMPLEMENT YET**
  - [x] This task is deferred to Story 7.2/7.3 when PlayerController exists
  - [x] For now, `CommandQueue` and `EventBus` are infrastructure — tested standalone
  - [x] Document in the headers how `GameApp::tick()` will consume commands and publish events

## Dev Notes

### Architecture Compliance

**Command Pattern (ADR-010):** This story implements the core of ADR-010 — all game actions as serializable `GameCommand` objects. The architecture mandates:
- Every game action = serializable `GameCommand` in a queue
- Tick-based simulation: 20 ticks/sec (50ms), fixed timestep
- No direct state mutation from input
- Event Bus for inter-system communication (block placed -> light update -> remesh -> Lua hooks)

**Namespace:** `voxel::game` — matches existing `GameLoop.h` and `Window.h` in `engine/include/voxel/game/`.

**Error handling:** These are pure in-memory data structures with no fallible operations. No `Result<T>` needed — use `VX_ASSERT` for programmer errors only (e.g., invalid event type).

**Exceptions disabled:** No `try`/`catch`. `std::function` allocations that fail will crash (acceptable per ADR-008).

### Existing Infrastructure — REUSE, DO NOT REINVENT

**`voxel::core::ConcurrentQueue<T>`** already exists at `engine/include/voxel/core/ConcurrentQueue.h`:
```cpp
template <typename T>
class ConcurrentQueue {
    void push(T&& item);
    std::optional<T> tryPop();
    size_t size() const;
    bool empty() const;
private:
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
};
```
Used by `ChunkManager` for mesh results. `CommandQueue` MUST wrap this — do NOT create a duplicate queue implementation. The MPSC guarantee is a superset of SPSC, so it satisfies the story requirement.

**`voxel::core::Types.h`** provides `uint8`, `uint16`, `uint32`, `uint64`, `int32`, etc. Use these instead of `std::uint32_t`.

**`voxel::math::MathTypes.h`** provides `IVec3 = glm::ivec3`, `IVec2 = glm::ivec2`. Use these for block positions and chunk coordinates.

### File Locations

```
engine/include/voxel/game/GameCommand.h    # NEW — command types + payloads
engine/include/voxel/game/CommandQueue.h   # NEW — wraps ConcurrentQueue<GameCommand>
engine/include/voxel/game/EventBus.h       # NEW — typed pub/sub
engine/src/game/EventBus.cpp               # NEW — only if non-trivial out-of-line code needed
tests/game/TestCommandQueue.cpp            # NEW — unit tests
tests/game/TestEventBus.cpp                # NEW — unit tests
```

**Header-only guidance:** `GameCommand.h` and `CommandQueue.h` should be header-only (trivial inline code, thin wrapper). `EventBus.h` may need a `.cpp` if the implementation is non-trivial (type-erased subscriber storage, callback maps).

**CMakeLists.txt changes:**
- `engine/CMakeLists.txt`: Add `src/game/EventBus.cpp` if it exists. `GameCommand.h` and `CommandQueue.h` are header-only.
- `tests/CMakeLists.txt`: Add `game/TestCommandQueue.cpp` and `game/TestEventBus.cpp`.

### GameCommand Design

```cpp
// engine/include/voxel/game/GameCommand.h
namespace voxel::game
{

enum class CommandType : core::uint8
{
    PlaceBlock,
    BreakBlock,
    MovePlayer,
    Jump,
    ToggleSprint
};

struct PlaceBlockPayload
{
    math::IVec3 position;
    core::uint16 blockId;
};

struct BreakBlockPayload
{
    math::IVec3 position;
};

struct MovePlayerPayload
{
    math::Vec3 direction;  // Normalized movement input vector
    bool isSprinting;
    bool isSneaking;
};

struct JumpPayload {};      // No data needed

struct ToggleSprintPayload
{
    bool enabled;
};

struct GameCommand
{
    CommandType type;
    core::uint32 playerId;
    core::uint32 tick;
    std::variant<
        PlaceBlockPayload,
        BreakBlockPayload,
        MovePlayerPayload,
        JumpPayload,
        ToggleSprintPayload
    > payload;
};

} // namespace voxel::game
```

### EventBus Design

```cpp
// engine/include/voxel/game/EventBus.h
namespace voxel::game
{

enum class EventType : core::uint8
{
    BlockPlaced,
    BlockBroken,
    ChunkLoaded
};

struct BlockPlacedEvent
{
    math::IVec3 position;
    core::uint16 blockId;
};

struct BlockBrokenEvent
{
    math::IVec3 position;
    core::uint16 previousBlockId;
};

struct ChunkLoadedEvent
{
    math::IVec2 coord;
};

// Type-erased subscriber using std::function
using SubscriptionId = core::uint32;

class EventBus
{
public:
    template <typename TEvent>
    SubscriptionId subscribe(EventType type, std::function<void(const TEvent&)> callback);

    void unsubscribe(EventType type, SubscriptionId id);

    template <typename TEvent>
    void publish(EventType type, const TEvent& event);

private:
    // Type-erased: store std::function<void(const void*)> internally
    // The subscribe template wraps the typed callback into a void* cast
    struct Subscriber
    {
        SubscriptionId id;
        std::function<void(const void*)> callback;
    };

    std::unordered_map<EventType, std::vector<Subscriber>> m_subscribers;
    SubscriptionId m_nextId = 1;
};

} // namespace voxel::game
```

**Alternative (simpler) design:** If type-erased void* feels too heavy, use `std::any` or a simple `std::function<void(const Event&)>` with a tagged union `Event` struct. However, the templated approach is more type-safe and follows modern C++ patterns.

**Thread safety:** The EventBus is called only from the main thread (simulation tick). No mutex needed. If future stories require multi-threaded publish, add a mutex then.

### CommandQueue Design

```cpp
// engine/include/voxel/game/CommandQueue.h
namespace voxel::game
{

class CommandQueue
{
public:
    void push(GameCommand cmd);
    std::optional<GameCommand> tryPop();
    bool empty() const;
    core::usize size() const;

    // Drain all commands, invoking callback for each
    template <typename Func>
    void drain(Func&& handler)
    {
        while (auto cmd = tryPop())
        {
            handler(std::move(*cmd));
        }
    }

private:
    core::ConcurrentQueue<GameCommand> m_queue;
};

} // namespace voxel::game
```

### How GameApp Will Use These (Story 7.2+)

Currently `GameApp::tick()` (in `game/src/GameApp.cpp`) does:
1. `handleInputToggles()` — reads `InputManager`, mutates overlay state directly
2. `m_camera.processMouseDelta(dx, dy)` — direct camera mutation
3. `m_camera.update(dt, W, S, A, D, Space, Shift)` — direct camera mutation
4. `m_chunkManager.update(playerPos)` — chunk streaming
5. `m_input->update(fdt)` — clear edge flags

After Story 7.2+ refactor, the tick will become:
```cpp
void GameApp::tick(double dt) {
    // 1. Input → Commands (never mutate state directly)
    gatherInputCommands();  // pushes to m_commandQueue

    // 2. Process command queue
    m_commandQueue.drain([this](GameCommand cmd) {
        processCommand(std::move(cmd));  // mutates state + publishes events
    });

    // 3. Systems that react to events
    m_chunkManager.update(playerPos);
    m_input->update(fdt);
}
```

This is NOT done in Story 7.1 — just providing context for the dev agent so the API design is forward-compatible.

### Testing Strategy

**Test file convention:** Follow existing pattern in `tests/core/TestConcurrentQueue.cpp`:
- Test case name: `"ClassName: behavior description"`
- Tags: `[game][command]` and `[game][event]`
- Use `SECTION()` for sub-cases
- `using namespace voxel::game;` at file scope

**CommandQueue tests:**
```cpp
TEST_CASE("CommandQueue: push and tryPop return commands in FIFO order", "[game][command]")
TEST_CASE("CommandQueue: tryPop on empty queue returns nullopt", "[game][command]")
TEST_CASE("CommandQueue: drain consumes all queued commands", "[game][command]")
TEST_CASE("CommandQueue: variant payload preserves data", "[game][command]")
```

**EventBus tests:**
```cpp
TEST_CASE("EventBus: subscribe and publish invokes callback", "[game][event]")
TEST_CASE("EventBus: multiple subscribers all invoked", "[game][event]")
TEST_CASE("EventBus: callback not invoked for different event type", "[game][event]")
TEST_CASE("EventBus: unsubscribe prevents future callbacks", "[game][event]")
TEST_CASE("EventBus: event data passed correctly to subscriber", "[game][event]")
```

### Naming Convention Compliance

| Element | Convention | Example in this story |
|---------|-----------|----------------------|
| Classes | PascalCase | `CommandQueue`, `EventBus`, `GameCommand` |
| Enums | `enum class PascalCase { PascalCase }` | `CommandType::PlaceBlock`, `EventType::BlockPlaced` |
| Methods | camelCase | `tryPop()`, `subscribe()`, `publish()` |
| Members | `m_` prefix | `m_queue`, `m_subscribers`, `m_nextId` |
| Files | PascalCase | `GameCommand.h`, `CommandQueue.h`, `EventBus.h` |
| Namespace | lowercase | `voxel::game` |
| Constants | SCREAMING_SNAKE | (none needed for this story) |

### Project Structure Notes

- New files go in `engine/include/voxel/game/` and `engine/src/game/` — matching existing `GameLoop.h/cpp` and `Window.h/cpp`
- Test files go in `tests/game/` — this directory may need to be created (currently tests has `core/`, `world/`, `renderer/`, `math/`)
- No conflicts with existing code — purely additive

### What NOT to Do

- Do NOT refactor `GameApp::tick()` yet — Story 7.2 will do this when `PlayerController` is introduced
- Do NOT add network serialization — just ensure `GameCommand` is plain data (serializable in principle)
- Do NOT add Lua event hooks — Story 9.x handles scripting integration
- Do NOT use `std::shared_ptr` for subscribers — raw function objects in vectors are sufficient
- Do NOT add error codes to `EngineError` for command/event operations — these are programmer errors, use `VX_ASSERT`
- Do NOT lock `EventBus` with a mutex — it runs on the main thread only

### References

- [Source: _bmad-output/planning-artifacts/architecture.md#System 10: Network-Readiness] — Command Pattern and Event Bus architecture
- [Source: _bmad-output/planning-artifacts/architecture.md#ADR-010] — Command Pattern + Tick-Based Simulation decision
- [Source: _bmad-output/planning-artifacts/epics/epic-07-player-interaction.md#Story 7.1] — Full acceptance criteria
- [Source: _bmad-output/planning-artifacts/ux-spec.md#2. Control Scheme] — Input bindings that will map to commands
- [Source: _bmad-output/project-context.md#Mandatory Patterns] — Command Pattern and Event Bus requirements
- [Source: engine/include/voxel/core/ConcurrentQueue.h] — Existing MPSC queue to reuse
- [Source: engine/include/voxel/game/GameLoop.h] — Base class with virtual tick()/render()
- [Source: game/src/GameApp.cpp] — Current tick() implementation that will consume commands in future stories

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

None — clean implementation, all tests passed on first run.

### Completion Notes List

- **GameCommand.h** — Header-only. Defines `CommandType` enum (5 types), 5 payload structs, and `GameCommand` struct with `std::variant` payload. Uses `voxel::core` types and `voxel::math` vectors per project convention.
- **CommandQueue.h** — Header-only thin wrapper around `voxel::core::ConcurrentQueue<GameCommand>`. Provides `push()`, `tryPop()`, `size()`, `empty()`, and templated `drain()`. Includes Doxygen usage example showing future `GameApp::tick()` integration pattern.
- **EventBus.h/.cpp** — Type-erased pub/sub system. `subscribe<TEvent>()` wraps typed callback into `void*`-based storage. `publish<TEvent>()` dispatches to all subscribers of given `EventType`. `unsubscribe()` by `SubscriptionId`. No mutex (main-thread only per ADR).
- **Tests** — 13 test cases, 64 assertions. CommandQueue: FIFO ordering, empty-returns-nullopt, drain, variant payload preservation, size/empty. EventBus: subscribe+publish, multiple subscribers, wrong-type filtering, unsubscribe, data correctness, subscriberCount, invalid-id no-op, no-subscribers no-op.
- **Full regression** — 180 test cases, 489138 assertions, all passing.

### File List

- `engine/include/voxel/game/GameCommand.h` (NEW)
- `engine/include/voxel/game/CommandQueue.h` (NEW)
- `engine/include/voxel/game/EventBus.h` (NEW)
- `engine/src/game/EventBus.cpp` (NEW)
- `tests/game/TestCommandQueue.cpp` (NEW)
- `tests/game/TestEventBus.cpp` (NEW)
- `engine/CMakeLists.txt` (MODIFIED — added EventBus.cpp)
- `tests/CMakeLists.txt` (MODIFIED — added game test files)

### Change Log

- 2026-03-29: Implemented Story 7.1 — Command Queue + Event Bus infrastructure. All 5 tasks completed, 13 new test cases passing, zero regressions across 180 total tests.
