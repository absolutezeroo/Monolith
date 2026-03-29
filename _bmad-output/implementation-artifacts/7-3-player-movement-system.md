# Story 7.3: Player Movement System

Status: review

## Story

As a developer,
I want a complete movement system with gravity, jumping, sprinting, sneaking, and block physics interactions,
so that locomotion feels like Minecraft.

## Acceptance Criteria

1. **Movement processed during simulation tick** (20 ticks/sec, dt = 0.05s) — not per-frame
2. **Walking speed**: 4.317 m/s — already exists as `WALK_SPEED` in PlayerController
3. **Sprint**: 5.612 m/s (1.3x walk) — toggled via Left Ctrl or `ToggleSprint` command
4. **Sneak**: 1.295 m/s (0.3x walk) — held via Left Shift, prevents falling off block edges
5. **Jump**: initial Y velocity ~8.0 m/s upward, only when `m_isOnGround == true`
6. **Gravity**: 28.0 m/s² downward (already implemented), terminal velocity ~78.4 m/s (already implemented)
7. **Air control**: reduced horizontal acceleration while airborne (0.02x ground acceleration)
8. **Sneak edge detection**: when sneaking, clamp position to prevent walking off block edges (check 0.3 blocks ahead for ground)
9. **Climbable blocks** (`isClimbable`): disable gravity, Space=up / Shift=down at sneak speed, WASD horizontal at walk speed, releasing keys = stay in place
10. **Move resistance** (`moveResistance`): multiply all velocities by `1.0 / (1.0 + maxResistance)`, gravity also reduced, use highest resistance when overlapping multiple blocks
11. **Damage blocks** (`damagePerSecond`): accumulate damage over ticks, log damage amount (no health system yet — V1)
12. **Command queue integration**: input handlers push `MovePlayer`, `Jump`, `ToggleSprint` commands — PlayerController consumes them during tick (ADR-010)
13. **Unit tests**: jump + fall, sprint/sneak speed modifiers, climbable behavior, move resistance reduction, sneak edge detection, air control reduction

## Tasks / Subtasks

- [x] Task 1: Add movement constants and state to PlayerController (AC: #2, #3, #4, #5, #7)
  - [x] 1.1 Add constants: `SPRINT_SPEED = 5.612f`, `SNEAK_SPEED = 1.295f`, `JUMP_VELOCITY = 8.0f`, `AIR_CONTROL = 0.02f`
  - [x] 1.2 Add member variables: `m_isSprinting`, `m_isSneaking`, `m_isInClimbable`, `m_damageAccumulator`
  - [x] 1.3 Add `m_maxResistance` (uint8_t) for per-tick resistance tracking

- [x] Task 2: Implement jump mechanics (AC: #5, #6)
  - [x] 2.1 In `tickPhysics()`: if jump requested AND `m_isOnGround` → set `m_velocity.y = JUMP_VELOCITY`
  - [x] 2.2 Clear jump request flag after applying (one-shot per press)

- [x] Task 3: Implement sprint/sneak speed modifiers (AC: #3, #4, #7)
  - [x] 3.1 Compute effective speed: base `WALK_SPEED` × sprint multiplier (1.3) or sneak multiplier (0.3)
  - [x] 3.2 Sprint cancels when sneak activates; sneak cancels sprint
  - [x] 3.3 Air control: when `!m_isOnGround`, multiply horizontal acceleration by `AIR_CONTROL` (0.02)

- [x] Task 4: Implement sneak edge detection (AC: #8)
  - [x] 4.1 When sneaking on ground: before applying horizontal movement, check 0.3 blocks ahead for ground
  - [x] 4.2 If no ground block below the target position → clamp position to current block edge
  - [x] 4.3 Edge check: for each foot corner (±0.3 on X/Z), verify `hasCollision` block exists at `y - 1` below the target XZ

- [x] Task 5: Implement block physics scanning (AC: #9, #10, #11)
  - [x] 5.1 At start of `tickPhysics()`: scan all blocks overlapping player AABB
  - [x] 5.2 For each overlapping block, check `BlockDefinition` properties: `isClimbable`, `moveResistance`, `damagePerSecond`
  - [x] 5.3 Track: `m_isInClimbable` (any block), `m_maxResistance` (highest value), damage sum

- [x] Task 6: Implement climbable block behavior (AC: #9)
  - [x] 6.1 When `m_isInClimbable`: disable gravity (skip `applyGravity()`)
  - [x] 6.2 Space → move up at `SNEAK_SPEED`, Shift → move down at `SNEAK_SPEED`
  - [x] 6.3 WASD → horizontal movement at `WALK_SPEED` (not sprint)
  - [x] 6.4 No keys held → zero velocity (player stays in place, no sliding)
  - [x] 6.5 Leaving climbable block → resume normal gravity

- [x] Task 7: Implement move resistance (AC: #10)
  - [x] 7.1 Apply speed multiplier: `1.0f / (1.0f + maxResistance)` to all velocity components
  - [x] 7.2 Apply same multiplier to gravity (slow fall in cobweb)
  - [x] 7.3 Applied after movement computation, before collision resolution

- [x] Task 8: Implement damage block detection (AC: #11)
  - [x] 8.1 Accumulate `damagePerSecond` values from overlapping blocks each tick
  - [x] 8.2 When accumulated time >= 1.0s, log damage: `VX_LOG_INFO("Player takes {} damage", totalDamage)`
  - [x] 8.3 Reset accumulator after logging (V1: no health system, just event)

- [x] Task 9: Integrate command queue (AC: #12)
  - [x] 9.1 In `GameApp::tick()`: push `MovePlayer`, `Jump`, `ToggleSprint` commands from input
  - [x] 9.2 Drain command queue before `PlayerController::tickPhysics()`
  - [x] 9.3 Refactor `PlayerController` to use `MovementInput` struct instead of reading InputManager directly (removed `update()`)
  - [x] 9.4 `tickPhysics(dt, MovementInput, ...)` is the core physics entry point
  - [x] 9.5 Keep fly mode path unchanged (still reads input directly)

- [x] Task 10: Update debug overlay (AC: all)
  - [x] 10.1 Add sprint/sneak status to ImGui overlay: `"Sprint: YES/NO"`, `"Sneak: YES/NO"`
  - [x] 10.2 Add climbable/resistance info when active: `"Climbing: YES"`, `"Resistance: 7"`

- [x] Task 11: Unit tests (AC: #13)
  - [x] 11.1 Test: jump from ground → velocity.y = JUMP_VELOCITY, then falls back
  - [x] 11.2 Test: jump while airborne → no effect (velocity.y unchanged)
  - [x] 11.3 Test: sprint speed = WALK_SPEED × 1.3
  - [x] 11.4 Test: sneak speed = WALK_SPEED × 0.3
  - [x] 11.5 Test: sneak edge detection — sneaking player stops at edge, doesn't fall
  - [x] 11.6 Test: climbable block — gravity disabled, vertical movement at sneak speed
  - [x] 11.7 Test: move resistance — speed reduced by factor `1/(1+resistance)`
  - [x] 11.8 Test: air control — horizontal acceleration reduced while airborne
  - [x] 11.9 Test: leaving climbable block resumes gravity

## Dev Notes

### Architecture Compliance

- **ADR-004**: Chunks NOT in ECS — all block queries via `ChunkManager::getBlock(ivec3)` + `BlockRegistry::getBlockType(stateId)`. Never use ECS views for block data.
- **ADR-008**: Exceptions disabled — use `VX_ASSERT` for programmer errors. No `Result<T>` needed (pure in-memory physics, no fallible operations).
- **ADR-010**: Command Pattern — Story 7.3 integrates the command pipeline. Input handlers push `GameCommand` objects (MovePlayer, Jump, ToggleSprint). Simulation tick drains CommandQueue and dispatches to PlayerController. This replaces the direct InputManager reading from Story 7.2.
- **20 ticks/sec fixed timestep**: `dt = 0.05s` per tick. All velocity and gravity calculations use this dt. Render interpolates between ticks.

### Existing PlayerController API (from Story 7.2)

```cpp
namespace voxel::game {
class PlayerController {
public:
    static constexpr math::Vec3 HALF_EXTENTS{0.3f, 0.9f, 0.3f};
    static constexpr float EYE_HEIGHT = 1.62f;
    static constexpr float GRAVITY = 28.0f;
    static constexpr float TERMINAL_VELOCITY = 78.4f;
    static constexpr float WALK_SPEED = 4.317f;
    static constexpr float COLLISION_EPSILON = 0.001f;

    void init(const glm::dvec3& spawnPos, world::ChunkManager& world,
              const world::BlockRegistry& registry);
    void update(float dt, const input::InputManager& input,
                const renderer::Camera& camera, world::ChunkManager& world,
                const world::BlockRegistry& registry);
    void tickPhysics(float dt, const glm::vec3& wishDir,
                     world::ChunkManager& world, const world::BlockRegistry& registry);

    [[nodiscard]] glm::dvec3 getPosition() const;
    [[nodiscard]] glm::dvec3 getEyePosition() const;
    [[nodiscard]] glm::vec3 getVelocity() const;
    [[nodiscard]] bool isOnGround() const;
    [[nodiscard]] math::AABB getAABB() const;
    void setPosition(const glm::dvec3& pos);

private:
    glm::dvec3 m_position{0.0, 80.0, 0.0};
    glm::vec3 m_velocity{0.0f};
    bool m_isOnGround = false;

    void applyGravity(float dt);
    void resolveCollisions(float dt, world::ChunkManager& world,
                           const world::BlockRegistry& registry);
    void resolveAxis(int axis, float delta, world::ChunkManager& world,
                     const world::BlockRegistry& registry);
    bool tryStepUp(int axis, float delta, world::ChunkManager& world,
                   const world::BlockRegistry& registry);
    void ensureNotInsideBlock(world::ChunkManager& world,
                              const world::BlockRegistry& registry);
};
}
```

**Key**: `tickPhysics(dt, wishDir, ...)` is the testable physics entry point. The `wishDir` parameter is a normalized movement direction vector. Story 7.3 extends this to accept sprint/sneak/jump/climb state.

### Refactoring Strategy for `update()` and `tickPhysics()`

The current `update()` reads InputManager directly for WASD and builds a `wishDir`. Story 7.3 must:

1. **Add a `MovementInput` struct** to pass movement state cleanly:
   ```cpp
   struct MovementInput {
       glm::vec3 wishDir{0.0f};  // Normalized horizontal movement direction
       bool jump = false;
       bool sprint = false;
       bool sneak = false;
   };
   ```

2. **Refactor `tickPhysics()`** signature to: `tickPhysics(float dt, const MovementInput& input, ChunkManager&, BlockRegistry&)`
3. **Refactor `update()`** to build `MovementInput` from InputManager, then call `tickPhysics()`
4. **GameApp command path**: drain CommandQueue → build `MovementInput` from accumulated commands → call `tickPhysics()`

This keeps `tickPhysics()` fully testable without InputManager or GLFW dependencies.

### GameCommand Infrastructure (from Story 7.1)

Already defined in `engine/include/voxel/game/GameCommand.h`:

```cpp
enum class CommandType : core::uint8 {
    PlaceBlock, BreakBlock, MovePlayer, Jump, ToggleSprint
};

struct MovePlayerPayload {
    math::Vec3 direction;  // Normalized movement input vector
    bool isSprinting;
    bool isSneaking;
};

struct JumpPayload {};           // Empty — one-shot action
struct ToggleSprintPayload { bool enabled; };

struct GameCommand {
    CommandType type;
    core::uint32 playerId;
    core::uint32 tick;
    std::variant<PlaceBlockPayload, BreakBlockPayload,
                 MovePlayerPayload, JumpPayload, ToggleSprintPayload> payload;
};
```

**CommandQueue API** (`CommandQueue.h`): `push(cmd)`, `tryPop()`, `drain(handler)`, `empty()`, `size()`.

**Usage in GameApp::tick():**
```cpp
// Push commands from input
if (m_input->isKeyDown(GLFW_KEY_SPACE))
    m_commandQueue.push({CommandType::Jump, 0, m_tick, JumpPayload{}});

// Drain and process
m_commandQueue.drain([&](GameCommand cmd) {
    switch (cmd.type) {
        case CommandType::MovePlayer: /* extract payload, set movement state */ break;
        case CommandType::Jump: /* set jump flag */ break;
        case CommandType::ToggleSprint: /* toggle sprint */ break;
        default: break;
    }
});
```

### Block Physics Properties API

From `engine/include/voxel/world/Block.h` — `BlockDefinition` struct:

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `hasCollision` | `bool` | `true` | Whether block has collision geometry (use this, NOT `isSolid`) |
| `isClimbable` | `bool` | `false` | Ladders, vines — disables gravity when overlapping |
| `moveResistance` | `uint8_t` | `0` | Slowing effect (cobweb=7, water=10, honey=3) |
| `damagePerSecond` | `uint32_t` | `0` | Damage when entity inside (cactus, lava, magma) |
| `drowning` | `uint8_t` | `0` | Drowning/suffocation parameter |

**Query pattern:**
```cpp
uint16_t stateId = world.getBlock(glm::ivec3{x, y, z});
if (stateId == voxel::world::BLOCK_AIR) continue;
const auto& def = registry.getBlockType(stateId);
// Now read: def.isClimbable, def.moveResistance, def.damagePerSecond, def.hasCollision
```

**ChunkManager::getBlock()** returns `BLOCK_AIR` for unloaded chunks — player falls through. Acceptable for V1.

### Block Physics Scanning Algorithm

At the start of each `tickPhysics()`, before movement:

```cpp
void PlayerController::scanOverlappingBlocks(ChunkManager& world, const BlockRegistry& registry) {
    m_isInClimbable = false;
    m_maxResistance = 0;
    uint32_t frameDamage = 0;

    math::AABB playerBox = getAABB();
    glm::ivec3 minBlock = glm::ivec3(glm::floor(glm::vec3(playerBox.min)));
    glm::ivec3 maxBlock = glm::ivec3(glm::floor(glm::vec3(playerBox.max - 0.001)));

    for (int y = minBlock.y; y <= maxBlock.y; ++y)
    for (int x = minBlock.x; x <= maxBlock.x; ++x)
    for (int z = minBlock.z; z <= maxBlock.z; ++z) {
        uint16_t stateId = world.getBlock({x, y, z});
        if (stateId == world::BLOCK_AIR) continue;
        const auto& def = registry.getBlockType(stateId);

        if (def.isClimbable) m_isInClimbable = true;
        m_maxResistance = std::max(m_maxResistance, def.moveResistance);
        frameDamage += def.damagePerSecond;
    }

    // Accumulate damage over ticks (20 ticks = 1 second)
    if (frameDamage > 0) {
        m_damageAccumulator += dt;  // dt = 0.05
        if (m_damageAccumulator >= 1.0f) {
            VX_LOG_INFO("Player takes {} damage", frameDamage);
            m_damageAccumulator -= 1.0f;
        }
    } else {
        m_damageAccumulator = 0.0f;
    }
}
```

### Sneak Edge Detection Algorithm

When sneaking and on ground, before applying horizontal movement:

```cpp
void PlayerController::clampToEdge(const glm::vec3& proposedDelta, ChunkManager& world,
                                    const BlockRegistry& registry) {
    // Check each foot corner after proposed movement
    glm::dvec3 newPos = m_position + glm::dvec3(proposedDelta);
    float halfW = HALF_EXTENTS.x;  // 0.3

    // Four corners of the player footprint
    glm::dvec3 corners[4] = {
        {newPos.x - halfW, newPos.y - 1.0, newPos.z - halfW},
        {newPos.x + halfW, newPos.y - 1.0, newPos.z - halfW},
        {newPos.x - halfW, newPos.y - 1.0, newPos.z + halfW},
        {newPos.x + halfW, newPos.y - 1.0, newPos.z + halfW},
    };

    for (auto& corner : corners) {
        glm::ivec3 belowBlock = glm::ivec3(glm::floor(glm::vec3(corner)));
        uint16_t stateId = world.getBlock(belowBlock);
        if (stateId == world::BLOCK_AIR) {
            // No ground below this corner — clamp movement on that axis
            // Revert the position component that moved over the edge
        }
        const auto& def = registry.getBlockType(stateId);
        if (!def.hasCollision) {
            // Same — no solid ground below
        }
    }
}
```

### GameApp::tick() After Story 7.3

```cpp
void GameApp::tick(double dt) {
    float fdt = static_cast<float>(dt);
    auto mouseDelta = m_input->getMouseDelta();
    m_camera.processMouseDelta(mouseDelta.x, mouseDelta.y);

    if (m_input->wasKeyPressed(GLFW_KEY_F7)) {
        m_flyMode = !m_flyMode;
        if (!m_flyMode)
            m_player.init(m_camera.getPosition(), m_chunkManager, m_blockRegistry);
        else
            m_camera.setPosition(m_player.getEyePosition());
    }

    if (m_flyMode) {
        m_camera.update(fdt, W, S, A, D, Space, Shift);
    } else {
        // Build movement commands from input
        glm::vec3 dir{0.0f};
        if (m_input->isKeyDown(GLFW_KEY_W)) dir += m_camera.getForward();
        if (m_input->isKeyDown(GLFW_KEY_S)) dir -= m_camera.getForward();
        if (m_input->isKeyDown(GLFW_KEY_A)) dir -= m_camera.getRight();
        if (m_input->isKeyDown(GLFW_KEY_D)) dir += m_camera.getRight();
        dir.y = 0.0f;
        if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);

        bool sneak = m_input->isKeyDown(GLFW_KEY_LEFT_SHIFT);
        if (m_input->wasKeyPressed(GLFW_KEY_LEFT_CONTROL))
            m_isSprinting = !m_isSprinting;
        if (sneak) m_isSprinting = false;

        bool jump = m_input->isKeyDown(GLFW_KEY_SPACE);

        MovementInput moveInput{dir, jump, m_isSprinting, sneak};
        m_player.tickPhysics(fdt, moveInput, m_chunkManager, m_blockRegistry);
        m_camera.setPosition(m_player.getEyePosition());
    }

    m_chunkManager.update(m_camera.getPosition());
    m_input->update(fdt);
}
```

### Movement Physics Flow (per tick)

```
1. Scan overlapping blocks → set m_isInClimbable, m_maxResistance, accumulate damage
2. Determine effective speed:
   - If climbable: WALK_SPEED horizontal, SNEAK_SPEED vertical
   - If sprinting: SPRINT_SPEED
   - If sneaking: SNEAK_SPEED
   - Default: WALK_SPEED
3. Apply resistance multiplier: speed *= 1.0 / (1.0 + m_maxResistance)
4. Compute wish velocity from direction × effective speed
5. If airborne: multiply horizontal acceleration by AIR_CONTROL (0.02)
6. If climbable:
   - Skip gravity
   - Space → velocity.y = +SNEAK_SPEED, Shift → velocity.y = -SNEAK_SPEED
   - No keys → velocity = {0, 0, 0}
7. Else: apply gravity (velocity.y -= GRAVITY * dt), cap at TERMINAL_VELOCITY
   - If resistance > 0: gravity also reduced by resistance multiplier
8. If jump requested AND m_isOnGround AND NOT climbable:
   - velocity.y = JUMP_VELOCITY
   - m_isOnGround = false
9. If sneaking AND on ground: apply edge detection clamping
10. Resolve collisions: Y → X → Z (existing swept AABB — unchanged)
11. Step-up check (existing — unchanged)
```

### Player Dimensions (unchanged from Story 7.2)

| Parameter | Value |
|-----------|-------|
| Full AABB | 0.6 x 1.8 x 0.6 blocks |
| Half-extents | 0.3 x 0.9 x 0.3 |
| Eye height | 1.62 blocks above feet |
| Position semantics | Center-bottom of AABB (feet) |
| Position type | `glm::dvec3` (double precision) |
| Velocity type | `glm::vec3` (float) |

### Edge Cases

1. **Climbable + resistance overlap**: if a block is both climbable and has resistance, apply resistance multiplier to climb speed
2. **Sprint + sneak**: mutually exclusive — sneak cancels sprint, sprint cannot activate while sneaking
3. **Jump while in climbable**: Space moves up (climb behavior), not jump. Jump only in non-climbable context
4. **Sprint in air**: maintain sprint speed if sprinting when leaving ground (no sprint toggle while airborne)
5. **Sneak edge at chunk boundary**: `ChunkManager::getBlock()` returns BLOCK_AIR for unloaded chunks — sneak edge detection treats unloaded as no ground (player stops at edge). This is correct behavior.
6. **Resistance reduces gravity**: at cobweb resistance=7, gravity is `28.0 / 8.0 = 3.5 m/s²` — slow fall effect
7. **Damage accumulation**: damage is logged per-second, not per-tick. Accumulate `dt` and fire when >= 1.0s. If player leaves damage block, reset accumulator.
8. **Zero-length wishDir**: when no movement keys held (and not in climbable), horizontal velocity should decay to zero (instant stop on ground, maintain momentum in air)

### Testing Approach

Use existing test infrastructure from Story 7.2: `makeTestRegistry()` and `setupFlatGround()` helpers.

For block physics tests, extend `makeTestRegistry()` to register additional block types:
```cpp
BlockRegistry makeTestRegistry() {
    BlockRegistry reg;
    // ID 0 = air (implicit)
    reg.registerBlock({.stringId = "base:stone", .numericId = 1, .hasCollision = true});
    reg.registerBlock({.stringId = "base:ladder", .numericId = 2, .hasCollision = false,
                       .isClimbable = true});
    reg.registerBlock({.stringId = "base:cobweb", .numericId = 3, .hasCollision = false,
                       .moveResistance = 7});
    reg.registerBlock({.stringId = "base:cactus", .numericId = 4, .hasCollision = true,
                       .damagePerSecond = 1});
    return reg;
}
```

Test `tickPhysics()` with the new `MovementInput` struct — no GLFW or InputManager needed.

### What This Story Does NOT Include

- No DDA raycasting or block targeting (Story 7.4)
- No block place/break (Story 7.5)
- No health system — damage is logged only (future epic)
- No swimming mechanics — `moveResistance` covers water slowing; buoyancy is future work
- No fall damage calculation (future)
- No ECS entity components for player — standalone class (future multiplayer epic)
- No render interpolation between ticks — direct camera position (fine for V1, interpolation can be added later)

### Previous Story Intelligence (7.2)

**Patterns established:**
- `tickPhysics()` is the testable physics entry point — tests call it directly without InputManager
- Collision resolution is Y→X→Z with 0.001 epsilon gap
- Step-up checks all block positions the player footprint overlaps on perpendicular axis
- `ensureNotInsideBlock()` handles fractional Y positions correctly
- Logger null guards needed in test context (use `if (Log::getLogger())` before logging)
- Tests use `makeTestRegistry()` + `setupFlatGround()` helpers

**Code review findings from 7.2:**
- `tryStepUp` must check all blocks the player footprint overlaps, not just center block
- `ensureNotInsideBlock` must compute actual block range from player height, not hardcoded 2-block check
- All 191 tests pass (489,378 assertions) — zero regressions after 7.2

**Files created/modified in 7.2:**
- `engine/include/voxel/game/PlayerController.h` (NEW — 93 lines)
- `engine/src/game/PlayerController.cpp` (NEW — 431 lines)
- `tests/game/TestPlayerController.cpp` (NEW — 443 lines)
- `game/src/GameApp.h` (MODIFIED — PlayerController member, flyMode flag)
- `game/src/GameApp.cpp` (MODIFIED — F7 toggle, physics/fly conditional)

**Git commit style:** `feat(game): ...`, `refactor(game): ...`

### Project Structure Notes

All modifications to existing files — no new files needed:
```
engine/
  include/voxel/game/
    PlayerController.h        ← MODIFIED (add constants, MovementInput, new members)
  src/game/
    PlayerController.cpp      ← MODIFIED (add jump, sprint, sneak, block physics, edge detection)
game/
  src/
    GameApp.h                 ← MODIFIED (add CommandQueue member, sprint state)
    GameApp.cpp               ← MODIFIED (command queue integration, updated tick(), debug overlay)
tests/
  game/
    TestPlayerController.cpp  ← MODIFIED (add ~9 new test cases for 7.3 mechanics)
```

### References

- [Source: epics/epic-07-player-interaction.md#Story 7.3]
- [Source: architecture.md#System 7 — Physics]
- [Source: architecture.md#ADR-010 — Command Pattern]
- [Source: ux-spec.md#Section 3 — Movement Feel]
- [Source: project-context.md#Critical Implementation Rules]
- [Source: 7-2-aabb-swept-collision.md#Dev Notes]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Implementation Plan

- Added `MovementInput` struct to decouple physics from InputManager (replaces old `update()` method entirely)
- Implemented `scanOverlappingBlocks()` at start of each tick to detect climbable, resistance, and damage blocks
- Implemented `clampToEdge()` for sneak edge detection — checks all 4 foot corners per axis independently
- Extended `tickPhysics()` with full movement physics flow: scan blocks → compute speed → apply resistance → handle climbable/normal → sneak edge clamp → resolve collisions
- Command queue integration in GameApp: input → push commands → drain → build MovementInput → tickPhysics
- Added Y bounds clamping in `scanOverlappingBlocks()` and `clampToEdge()` to prevent assertion failures when player is above COLUMN_HEIGHT
- Extended `makeTestRegistry()` with ladder, cobweb, and cactus block types for physics tests
- All 9 existing Story 7.2 tests updated to use `MovementInput` API
- 9 new tests added covering all AC #13 requirements

### Debug Log References

- Fixed Y out-of-bounds assertion: `scanOverlappingBlocks()` called `world.getBlock()` with y > COLUMN_HEIGHT when player was at high altitude. Added `std::max/min` clamping to WORLD_MIN_Y/WORLD_MAX_Y.
- Fixed move resistance test: player wasn't settled on ground before walking through cobweb, causing air control factor (0.02x) to reduce speed. Fixed by calling `settleOnGround()` first.

### Completion Notes List

- All 11 tasks and 42 subtasks completed
- 200 tests pass (489,468 assertions) — zero regressions
- 9 new test cases added for Story 7.3 mechanics
- All 13 acceptance criteria satisfied
- `update()` method removed from PlayerController (replaced by `MovementInput` struct + `tickPhysics()`)
- Command queue integration working: GameApp pushes MovePlayer/Jump commands, drains before physics tick
- Debug overlay shows sprint/sneak/climbable/resistance status

### File List

- `engine/include/voxel/game/PlayerController.h` — MODIFIED: Added MovementInput struct, new constants (SPRINT_SPEED, SNEAK_SPEED, JUMP_VELOCITY, AIR_CONTROL), new member variables, new accessors, removed update() and old tickPhysics signature
- `engine/src/game/PlayerController.cpp` — MODIFIED: Added scanOverlappingBlocks(), clampToEdge(), refactored tickPhysics() with full movement physics, added Y bounds clamping
- `game/src/GameApp.h` — MODIFIED: Added CommandQueue member, m_isSprinting state
- `game/src/GameApp.cpp` — MODIFIED: Command queue integration in tick(), debug overlay sprint/sneak/climbable info
- `tests/game/TestPlayerController.cpp` — MODIFIED: Extended makeTestRegistry() with ladder/cobweb/cactus, updated all tests to use MovementInput, added 9 new test cases

### Change Log

- 2026-03-29: Implemented Story 7.3 — Player Movement System with jump, sprint, sneak, climbable blocks, move resistance, damage blocks, sneak edge detection, air control, and command queue integration. 9 new tests added, all 200 tests pass.
