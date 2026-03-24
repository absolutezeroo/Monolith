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
- Player AABB: 0.6 wide × 1.8 tall × 0.6 deep (Minecraft standard), centered on position
- Collision resolution: clip movement delta against voxel AABBs, axis by axis: **Y first** (gravity), then X, then Z
- Per axis: extend player AABB by velocity on that axis, collect all solid blocks in extended volume, clip delta against each
- `OnGround` detection: if Y-axis clipped to zero while moving down → player is on ground
- Step-up: if horizontal collision with a 1-block ledge and space above → auto-step up (like Minecraft)
- Edge case: player spawning inside a block → push out to nearest open space
- Unit tests: falling onto flat ground stops, walking into wall stops, step-up onto 1-block, corner collision

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

## Story 7.5: Block Place and Break

**As a** developer,
**I want** to place and break blocks using mouse clicks,
**so that** the core Minecraft interaction loop works.

**Acceptance Criteria:**
- Left click: break targeted block → push `BreakBlockCommand{pos}`
- Right click: place selected block at `previousPos` from raycast → push `PlaceBlockCommand{pos, blockId}`
- Command processing in simulation tick: `setBlock(pos, BLOCK_AIR)` for break, `setBlock(pos, selectedId)` for place
- After block change: mark affected section dirty, mark neighbor sections dirty if on boundary
- Events published: `BlockBrokenEvent`, `BlockPlacedEvent` → triggers remesh, light update
- Placement validation: can't place inside player AABB, can't place in air without adjacent solid
- Hotbar: keys 1–9 select block type from a hardcoded list (UI comes later), scroll wheel cycles
- Break animation: not required for V1 (instant break)
- Place/break sound: not required for V1
