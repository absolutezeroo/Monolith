# Epic 7 — Player Interaction

**Priority**: P0
**Dependencies**: Epic 3, Epic 6
**Goal**: Player can walk, jump, collide with the world, look around, and place/break blocks. Full FPS gameplay loop.

---

## Story 7.1: Command Queue + Event Bus

**As a** developer,
**I want** a command queue for game actions and an event bus for inter-system communication,
**so that** all state mutation goes through a serializable pipeline (network-ready).

**Acceptance Criteria:**
- `GameCommand` struct: `Type` enum (PlaceBlock, BreakBlock, MovePlayer, Jump, ToggleSprint), `playerId`, `tick`, `std::variant` payload
- `CommandQueue` class: thread-safe push/tryPop (SPSC is sufficient for singleplayer)
- Input handlers push commands — never mutate game state directly
- `EventBus` class: typed publish/subscribe with `EventType` enum
- Events: `BlockPlacedEvent{pos, blockId}`, `BlockBrokenEvent{pos, previousBlockId}`, `ChunkLoadedEvent{coord}`
- Simulation tick consumes command queue, processes each, publishes events
- Unit tests: push/pop ordering, event callback invocation

---

## Story 7.2: AABB Swept Collision

**As a** developer,
**I want** AABB collision that prevents the player from entering solid blocks,
**so that** movement feels correct and the player walks on surfaces.

**Acceptance Criteria:**

**PlayerController (prerequisite — the Camera currently IS the player):**
The current codebase has `Camera` handling both view rotation AND WASD movement (Camera.h line 19: `update(dt, forward, backward, left, right, up, down)`). Adding gravity and collision directly to Camera would violate single responsibility and couple the renderer to physics.

- Create `voxel::game::PlayerController` in `engine/include/voxel/game/PlayerController.h`
- Owns: position (`glm::dvec3`), velocity (`glm::vec3`), AABB half-extents (0.3 × 0.9 × 0.3), `OnGround` flag
- `update(dt, InputManager&, ChunkManager&)` — reads input, applies movement + gravity, resolves collision
- Camera follows PlayerController: `camera.setPosition(player.getPosition() + vec3(0, 1.62, 0))`
- Remove movement logic from `Camera::update()` — Camera keeps ONLY mouse look (processMouseDelta, view/projection matrix)
- GameApp::tick() changes from `m_camera.update(dt, W, S, A, D, Space, Shift)` to `m_player.update(dt, m_input, m_chunkManager)` then `m_camera.setPosition(m_player.getEyePosition())`
- Fly mode toggle (F7): bypasses PlayerController, moves Camera directly (for debugging)

**Collision resolution:**
- Player AABB: 0.6 wide × 1.8 tall × 0.6 deep (Minecraft standard), centered on position
- Clip movement delta against voxel AABBs, axis by axis: **Y first** (gravity), then X, then Z
- Per axis: extend player AABB by velocity on that axis, collect all solid blocks in extended volume, clip delta against each
- `OnGround` detection: if Y-axis clipped to zero while moving down → player is on ground
- Step-up: if horizontal collision with a 1-block ledge and space above → auto-step up (like Minecraft)
- Edge case: player spawning inside a block → push out to nearest open space
- Unit tests: falling onto flat ground stops, walking into wall stops, step-up onto 1-block, corner collision

**Files:**
```
engine/include/voxel/game/PlayerController.h
engine/src/game/PlayerController.cpp
```

---

## Story 7.3: Player Movement System

**As a** developer,
**I want** a complete movement system with gravity, jumping, sprinting, and sneaking,
**so that** locomotion feels like Minecraft.

**Acceptance Criteria:**
- Movement processed during simulation tick (not per-frame)
- Walking speed: 4.317 m/s, Sprint: 5.612 m/s (1.3× walk), Sneak: 1.295 m/s (0.3× walk)
- Jump: initial Y velocity 8.0 m/s (approximately), only when OnGround
- Gravity: 28.0 m/s² downward (Minecraft-like, not real 9.81), terminal velocity ~78 m/s
- Air control: reduced horizontal acceleration while airborne
- Sneak: prevents falling off block edges (clamp position to current block surface)
- Sprint toggle via command (ToggleSprint), sneak while holding Shift
- Camera follows player position with eye height offset (1.62 blocks above feet)

**Block physics properties (from BlockDefinition fields set in Story 3.3):**
- **Climbable blocks** (ladders, vines): when player AABB overlaps a block with `isClimbable=true`:
  - Gravity is disabled
  - Space moves up, Shift moves down, at sneak speed (1.295 m/s vertical)
  - WASD still works horizontally at walk speed
  - Releasing all keys → player stays in place (no sliding down)
  - Leaving the climbable block → normal gravity resumes
- **Move resistance** (cobwebs, honey, water): when player AABB overlaps a block with `moveResistance > 0`:
  - All movement velocities multiplied by `1.0 / (1.0 + moveResistance)` — cobweb (7) reduces speed to ~12%
  - Gravity also reduced proportionally (slow fall in cobweb)
  - Multiple overlapping resistant blocks: use the highest resistance value
- **Damage per second** (cactus, lava, fire): when player AABB overlaps a block with `damagePerSecond > 0`:
  - Apply damage every second (or fraction, accumulated over ticks)
  - V1: just log the damage amount (no health system yet — but the check exists and fires `on_entity_inside` callback from Story 9.6)
  - Future: integrate with health/hearts system

**Implementation in PlayerController:**
```cpp
void PlayerController::update(float dt, InputManager& input, ChunkManager& world) {
    // 1. Scan blocks overlapping player AABB
    auto overlapping = world.getBlocksInAABB(m_aabb);
    
    // 2. Check block physics properties
    bool inClimbable = false;
    uint8_t maxResistance = 0;
    uint32_t totalDamage = 0;
    for (auto& blockPos : overlapping) {
        auto& def = registry.getBlock(world.getBlock(blockPos));
        if (def.isClimbable) inClimbable = true;
        maxResistance = std::max(maxResistance, def.moveResistance);
        totalDamage += def.damagePerSecond;
    }
    
    // 3. Apply movement with modifiers
    float speedMultiplier = 1.0f / (1.0f + maxResistance);
    // ... rest of movement logic
}
```

---

## Story 7.4: DDA Raycasting + Block Highlight

**As a** developer,
**I want** to cast a ray from the camera to identify which block the player is looking at,
**so that** block targeting for place/break works.

**Acceptance Criteria:**
- `DDA::raycast(origin, direction, maxDistance, world) → RaycastResult`
- `RaycastResult`: `bool hit`, `glm::ivec3 blockPos`, `glm::ivec3 previousPos` (for placement), `BlockFace face`, `float distance`
- Max distance: 6 blocks (configurable)
- Correct face identification: which face of the block was hit (needed for placement side)
- Block highlight: render wireframe overlay on targeted block (simple line rendering or overlay quad)
- Updates every frame (not just on tick) for responsive feel
- Unit tests: ray hitting a block returns correct position and face, ray missing returns no hit, edge cases (exact corner/edge hits)

---

## Story 7.5: Block Place and Break (with Mining Time System)

**As a** developer,
**I want** to place and break blocks using mouse clicks with a mining time system,
**so that** the core Minecraft interaction loop works — hold to mine, instant place.

**Acceptance Criteria:**

**Mining time system (NOT instant break):**
- Holding left click starts mining the targeted block
- Mining progress: `0.0 → 1.0` over time, determined by `calculateBreakTime(block, tool)`
- Break time formula:
  ```
  baseTime = block.hardness * 1.5  (seconds, with bare hands)
  
  If tool matches a block group (e.g., pickaxe matches "cracky"):
    toolMultiplier = toolGroupCap.times[block.groups[group]]
    baseTime = block.hardness * toolMultiplier
  
  If tool doesn't match any group:
    baseTime = block.hardness * 5.0  (very slow, punching stone)
  
  If block can't be broken by hand and no matching tool:
    baseTime = infinity (never breaks)
  ```
- V1 simplification: no tool system yet, so ALL blocks use `hardness * 1.5` as break time. The formula is implemented but the tool matching branch always falls through to "bare hands." Tool capabilities come in a future epic.
- Minimum break time: 0.05 seconds (even "instant" blocks have 1 tick delay)
- If player releases left click or looks away (target changes): reset progress to 0
- If player moves more than 6 blocks from target: cancel mining

**Break overlay (crack animation):**
- 10 crack stages (textures `assets/textures/destroy_stage_0.png` through `_9.png`)
- Stage = `floor(progress * 10)`, clamped to 0–9
- Rendered as a fullscreen overlay on the targeted block face using alpha blending
- The overlay is a separate draw call: one quad positioned on the targeted face, sampled from the crack texture array
- Updated every frame (smooth visual), but mining progress ticks at simulation rate (20/sec)

**Break completion:**
- When progress reaches 1.0: push `BreakBlockCommand{pos}`
- Command processing in simulation tick: `setBlock(pos, BLOCK_AIR)`
- Drops spawned (via `get_drops` callback or default `dropItem` from BlockDefinition)
- Experience spawned (via `get_experience` callback, V1: always 0)
- After block change: mark affected section dirty, mark neighbor sections dirty if on boundary
- Events published: `BlockBrokenEvent` → triggers remesh, light update

**Right click — placement (unchanged, instant):**
- Right click: place selected block at `previousPos` from raycast → push `PlaceBlockCommand{pos, blockId}`
- Hotbar: keys 1–9 select block type, scroll wheel cycles

**Placement validation (expanded with BlockDefinition properties):**
- Can't place inside player AABB (player collision check)
- Can't place in air without adjacent solid face (must target an existing face)
- **buildable_to**: if the target position contains a block with `isBuildableTo=true` (tall grass, snow layer, dead bush), the new block REPLACES it instead of failing. The replaced block is destroyed (triggers `on_destruct` if it has one)
- **replaceable**: air and flowing liquid are always replaceable (no face targeting needed — you can place into liquid)
- **Block state for placement**: if the placed block has states (Story 3.4), call `get_state_for_placement` callback (Story 9.2) to determine the state based on player facing direction, target face, etc. Default: `baseStateId` (no rotation)

**Post-effect color overlay (wired here, defined in BlockDefinition):**
- Each frame, check if the camera position is inside a block with `postEffectColor != 0`
- If yes, render a fullscreen quad with that color at alpha blend (simple forward pass after all rendering)
- Blue tint when head is underwater, red tint in lava, green tint in slime
- Implementation: single `postEffectColor` uniform checked in the composite pass

**MiningState in PlayerController:**
```cpp
struct MiningState {
    glm::ivec3 targetBlock{0};
    float progress = 0.0f;        // 0.0 → 1.0
    float breakTime = 0.0f;       // Total time needed
    int crackStage = -1;          // -1 = not mining, 0–9 = crack overlay
    bool isMining = false;
};
```

**Wield mesh (held item in hand):**
- The currently selected block/item from the hotbar is rendered as a 3D model in the bottom-right corner of the screen
- Rendering: separate draw call after the scene, before ImGui
  - Use the block's model from `ModelRegistry` (or a generated cube mesh for FullCube blocks)
  - Apply a fixed view matrix: offset to bottom-right, tilted ~30° toward the camera
  - Scale: ~40% of a full block
  - Lit by a fixed directional light (not world lighting — always visible)
- Animation:
  - Idle: subtle slow bob (sinusoidal Y offset, 0.5Hz)
  - Mining: swing animation when left-click is held (rotate around the wrist pivot, quick forward motion synced with crack progress)
  - Place: quick forward thrust on right-click (0.2s animation)
  - Switch: when hotbar slot changes, quick drop-down then rise-up (0.15s each)
- Implementation:
  - `WieldMeshRenderer` class in `engine/include/voxel/renderer/WieldMeshRenderer.h`
  - `render(VkCommandBuffer cmd, const Camera& camera, uint16_t heldBlockId, WieldAnimState& anim)`
  - Uses the same pipeline as chunk rendering (or a simple forward pipeline) but with a separate MVP matrix
  - Reads model vertices from `ModelRegistry` — same data, different transform
- Wield mesh uses the block's texture from the texture array — no separate texture needed
