# Story 7.5: Block Place and Break (with Mining Time System)

Status: review

## Story

As a developer,
I want to place and break blocks using mouse clicks with a mining time system,
so that the core Minecraft interaction loop works — hold to mine, instant place.

## Acceptance Criteria

1. **Mining time system**: Holding LMB starts mining the targeted block; progress goes 0.0→1.0 over `hardness * 1.5` seconds (bare hands, V1); minimum break time 0.05s; releasing LMB or looking away resets progress to 0; moving >6 blocks from target cancels mining
2. **Crack overlay**: 10-stage break animation (textures `destroy_stage_0.png`–`_9.png`); stage = `floor(progress * 10)` clamped 0–9; rendered as alpha-blended quad on the targeted block face; visual updates every frame, progress ticks at simulation rate (20/sec)
3. **Break completion**: When progress reaches 1.0, push `BreakBlockCommand{pos}`; command handler calls `setBlock(pos, BLOCK_AIR)`; publish `BlockBrokenEvent{pos, previousBlockId}`; dirty propagation handled automatically by `ChunkManager::setBlock`
4. **Right-click placement**: RMB places selected hotbar block at `raycastResult.previousPos`; push `PlaceBlockCommand{pos, blockId}`; validate: not inside player AABB, target is air or `isBuildableTo`; publish `BlockPlacedEvent{pos, blockId}`
5. **Hotbar → registry resolution**: Map `HOTBAR_BLOCK_NAMES[slot]` to `BlockRegistry::getIdByName("base:<name>")`; keys 1–9 and scroll wheel already select slot
6. **EventBus integration**: Add `EventBus m_eventBus` to `GameApp`; publish `BlockPlacedEvent` / `BlockBrokenEvent` on successful place/break
7. **Post-effect color overlay**: Each frame, check if camera is inside a block with `postEffectColor != 0`; render fullscreen tinted quad (blue=water, red=lava)
8. **Wield mesh**: Render currently selected block as 3D model in bottom-right of screen; idle bob, mining swing, place thrust, slot-switch drop-up animations
9. **Unit tests**: Mining progress accumulation and reset, break time calculation, placement validation (player AABB overlap, buildable_to, air check), command push/drain for PlaceBlock/BreakBlock, EventBus event publication

## Tasks / Subtasks

### Part A — Mining System + Block Breaking (AC: 1, 2, 3, 6, 9)

- [x] **A1. Add MiningState to PlayerController** (AC: 1)
  - [ ] Add `MiningState` struct: `targetBlock(ivec3)`, `progress(float)`, `breakTime(float)`, `crackStage(int, -1=not mining)`, `isMining(bool)`
  - [ ] Add `updateMining(float dt, const RaycastResult&, bool lmbDown, const BlockRegistry&)` method
  - [ ] Implement `calculateBreakTime(const BlockDefinition&)` → `hardness * 1.5f` (V1 bare hands); minimum 0.05s
  - [ ] Progress increments by `dt / breakTime` each tick; clamp to 1.0
  - [ ] Reset conditions: LMB released, target block changed, distance > 6 blocks
  - [ ] When progress >= 1.0 → return break-ready signal (consumed by GameApp to push command)

- [x] **A2. Add EventBus to GameApp** (AC: 6)
  - [ ] Add `voxel::game::EventBus m_eventBus` member to `GameApp`
  - [ ] Wire `BlockBrokenEvent` / `BlockPlacedEvent` publication in command drain handler

- [x] **A3. Wire breaking flow in GameApp::tick()** (AC: 1, 3)
  - [ ] Call `m_player.updateMining(dt, m_raycastResult, m_input->isMouseButtonDown(0), m_blockRegistry)` each tick
  - [ ] When mining completes: push `GameCommand{BreakBlock, 0, tick, BreakBlockPayload{pos}}`
  - [ ] Add `BreakBlock` case in drain switch: `previousId = getBlock(pos)`, `setBlock(pos, BLOCK_AIR)`, publish `BlockBrokenEvent{pos, previousId}`
  - [ ] Guard all block interaction behind `m_input->isCursorCaptured()` check

- [x] **A4. Crack overlay rendering** (AC: 2)
  - [ ] Create/source 10 crack textures (`assets/textures/destroy_stage_0.png` through `_9.png`)
  - [ ] Load crack textures into a separate `VkImage` texture array (10 layers)
  - [ ] Render crack quad on targeted block face: alpha-blended, positioned using `raycastResult.face` and `raycastResult.blockPos`
  - [ ] Crack stage driven by `MiningState::crackStage` = `floor(progress * 10)`, clamped 0–9
  - [ ] Shader: sample crack texture by stage index, alpha blend over the block face
  - [ ] Create `crack_overlay.vert` / `crack_overlay.frag` shaders (or reuse block highlight pipeline with texture)

- [x] **A5. Tests for mining system** (AC: 9)
  - [ ] Test: progress accumulates correctly over multiple ticks
  - [ ] Test: progress resets when LMB released
  - [ ] Test: progress resets when target block changes
  - [ ] Test: `calculateBreakTime` returns `hardness * 1.5`, minimum 0.05s
  - [ ] Test: crack stage = `floor(progress * 10)`, clamped 0–9
  - [ ] Test: `BreakBlock` command sets block to `BLOCK_AIR`
  - [ ] Test: `BlockBrokenEvent` published with correct `previousBlockId`

### Part B — Block Placement (AC: 4, 5, 9)

- [x] **B1. Wire placement flow in GameApp::tick()** (AC: 4)
  - [ ] On `wasMouseButtonPressed(RMB)` + `m_raycastResult.hit` + cursor captured:
    - Resolve `blockId` from `m_hotbarSlot` via registry lookup
    - Push `GameCommand{PlaceBlock, 0, tick, PlaceBlockPayload{previousPos, blockId}}`
  - [ ] Add `PlaceBlock` case in drain switch: validate then `setBlock(pos, blockId)`, publish `BlockPlacedEvent`

- [x] **B2. Placement validation** (AC: 4)
  - [ ] Check target position is air OR `isBuildableTo` (tall grass, snow, dead bush)
  - [ ] Check placement block AABB does not overlap player AABB (`m_player.getAABB()`)
  - [ ] If validation fails → silently discard command (no crash, no error)

- [x] **B3. Hotbar → registry ID resolution** (AC: 5)
  - [ ] Convert `HOTBAR_BLOCK_NAMES[slot]` (display names like "Stone") to registry IDs (`"base:stone"`)
  - [ ] Add a lookup table or naming convention mapping (lowercase + prefix `"base:"`)
  - [ ] Handle missing block gracefully (fallback to `BLOCK_AIR` or skip placement)

- [x] **B4. Tests for placement** (AC: 9)
  - [ ] Test: placement at `previousPos` sets correct block ID
  - [ ] Test: placement rejected when overlapping player AABB
  - [ ] Test: placement into `isBuildableTo` block succeeds (replaces target)
  - [ ] Test: placement into solid block fails
  - [ ] Test: `BlockPlacedEvent` published with correct position and blockId

### Part C — Post-Effect Color Overlay (AC: 7)

- [x] **C1. Head-inside-block tint** (AC: 7)
  - [ ] Each frame in render: check block at camera position via `getBlock(ivec3(floor(eyePos)))`
  - [ ] If `BlockDefinition::postEffectColor != 0` → render fullscreen quad with that RGBA tint
  - [ ] Simple forward pass after scene rendering, before ImGui
  - [ ] Shader: single-uniform color, alpha blend

### Part D — Wield Mesh (AC: 8)

- [x] **D1. WieldMeshRenderer class** (AC: 8)
  - [ ] Create `engine/include/voxel/renderer/WieldMeshRenderer.h` and `.cpp`
  - [ ] `render(VkCommandBuffer, Camera&, uint16_t heldBlockId, WieldAnimState&)`
  - [ ] Render selected block as 3D cube model at bottom-right of screen
  - [ ] Fixed view matrix: offset to lower-right, tilted ~30° toward camera, 40% scale
  - [ ] Lit by fixed directional light (not world lighting)
  - [ ] Uses block textures from existing texture array

- [x] **D2. Wield animation state** (AC: 8)
  - [ ] `WieldAnimState` struct: `animType(Idle/Mining/Place/Switch)`, `timer(float)`, `prevSlot(int)`
  - [ ] Idle: sinusoidal Y bob at 0.5Hz
  - [ ] Mining: swing rotation synced with crack progress
  - [ ] Place: 0.2s forward thrust on RMB
  - [ ] Switch: 0.15s drop-down + 0.15s rise-up on slot change

## Dev Notes

### Critical Architecture Rules

- **No direct state mutation**: All block changes go through `CommandQueue` → `drain()` → `setBlock()`. Never call `setBlock` from input handlers directly.
- **No C++ exceptions**: Use `Result<T>` (`std::expected<T, EngineError>`) for fallible operations. `setBlock` itself is infallible (asserts chunk loaded).
- **Chunks NOT in ECS**: All block data lives in `ChunkManager`. ECS holds only entity metadata.
- **Fixed tick rate**: 20 ticks/sec. Mining progress updates in `tick()`, visual crack overlay updates in `render()`.
- **Command pattern**: `GameCommand` struct with `CommandType` enum + variant payload. Push in input handling, drain in simulation.

### Existing Infrastructure (DO NOT RECREATE)

| Component | File | Status | Notes |
|-----------|------|--------|-------|
| `CommandType::PlaceBlock/BreakBlock` | `game/GameCommand.h` | DONE | Enum values + payload structs already defined |
| `PlaceBlockPayload{pos, blockId}` | `game/GameCommand.h` | DONE | Uses `math::IVec3` + `core::uint16` |
| `BreakBlockPayload{pos}` | `game/GameCommand.h` | DONE | Uses `math::IVec3` |
| `CommandQueue::push/drain` | `game/CommandQueue.h` | DONE | Thread-safe FIFO, drain with lambda |
| `EventType::BlockPlaced/BlockBroken` | `game/EventBus.h` | DONE | Event types + structs defined |
| `EventBus::publish<T>/subscribe<T>` | `game/EventBus.h` | DONE | Type-safe via `EventTypeTraits` |
| `RaycastResult.previousPos` | `physics/Raycast.h` | DONE | The air block adjacent to hit = placement target |
| `m_raycastResult` in GameApp | `game/GameApp.h` | DONE | Updated every frame in `render()` |
| `ChunkManager::setBlock(pos, id)` | `world/ChunkManager.h` | DONE | Auto-propagates dirty flags to neighbors |
| `ChunkManager::getBlock(pos)` | `world/ChunkManager.h` | DONE | Returns `BLOCK_AIR` if chunk not loaded |
| `InputManager::wasMouseButtonPressed` | `input/InputManager.h` | DONE | One-shot per click (RMB placement) |
| `InputManager::isMouseButtonDown` | `input/InputManager.h` | DONE | Continuous hold detection (LMB mining) |
| `m_hotbarSlot` in GameApp | `game/GameApp.h` | DONE | 0–8, updated by scroll/number keys |
| `HOTBAR_BLOCK_NAMES[9]` | `game/GameApp.cpp` | DONE | Display names, need registry ID mapping |
| `BlockDefinition::hardness` | `world/Block.h` | DONE | Float field, default 1.0f |
| `BlockDefinition::isBuildableTo` | `world/Block.h` | DONE | For replaceable blocks (grass, snow) |
| `BlockDefinition::hasCollision` | `world/Block.h` | DONE | Used by raycast to skip non-solid |
| `PlayerController::getAABB()` | `game/PlayerController.h` | CHECK | May need to add if not exposed |
| `BLOCK_AIR` constant | `world/Block.h` | DONE | = 0 |

### Data Flow: Break Block

```
render() each frame:
  m_raycastResult = raycast(eyePos, forward, 6.0, chunkMgr, registry)
  crackStage = m_player.getMiningState().crackStage
  if crackStage >= 0 → draw crack overlay on raycastResult.blockPos face

tick() at 20Hz:
  if cursor captured AND isMouseButtonDown(LMB) AND raycastResult.hit:
    m_player.updateMining(dt, raycastResult, true, registry)
    if mining complete:
      push BreakBlockCommand{raycastResult.blockPos}
  else:
    m_player.resetMining()

  drain commands:
    case BreakBlock:
      prevId = chunkMgr.getBlock(pos)
      chunkMgr.setBlock(pos, BLOCK_AIR)    // auto-dirties neighbors
      eventBus.publish<BlockBroken>({pos, prevId})
```

### Data Flow: Place Block

```
tick() at 20Hz:
  if cursor captured AND wasMouseButtonPressed(RMB) AND raycastResult.hit:
    blockId = resolveHotbarBlockId(m_hotbarSlot)
    push PlaceBlockCommand{raycastResult.previousPos, blockId}

  drain commands:
    case PlaceBlock:
      targetBlock = chunkMgr.getBlock(pos)
      targetDef = registry.getBlockType(targetBlock)
      if targetBlock != BLOCK_AIR AND !targetDef.isBuildableTo:
        return  // silently reject
      if playerAABB.overlaps(AABB{pos, pos+1}):
        return  // can't place inside yourself
      chunkMgr.setBlock(pos, blockId)    // auto-dirties neighbors
      eventBus.publish<BlockPlaced>({pos, blockId})
```

### Files to Create

| File | Purpose |
|------|---------|
| `engine/include/voxel/game/MiningState.h` | MiningState struct + calculateBreakTime |
| `engine/include/voxel/renderer/WieldMeshRenderer.h` | Wield mesh renderer class |
| `engine/src/renderer/WieldMeshRenderer.cpp` | Wield mesh implementation |
| `engine/include/voxel/renderer/CrackOverlay.h` | Crack texture loading + overlay rendering |
| `engine/src/renderer/CrackOverlay.cpp` | Crack overlay implementation |
| `assets/shaders/crack_overlay.vert` | Crack overlay vertex shader |
| `assets/shaders/crack_overlay.frag` | Crack overlay fragment shader |
| `assets/shaders/wield.vert` | Wield mesh vertex shader (or reuse chunk.vert) |
| `assets/shaders/wield.frag` | Wield mesh fragment shader (or reuse chunk.frag) |
| `assets/shaders/post_effect.vert` | Post-effect fullscreen quad vertex |
| `assets/shaders/post_effect.frag` | Post-effect tint fragment |
| `tests/game/TestMiningState.cpp` | Mining system unit tests |
| `tests/game/TestBlockPlacement.cpp` | Placement validation unit tests |

### Files to Modify

| File | Changes |
|------|---------|
| `game/src/GameApp.h` | Add `EventBus m_eventBus`, `MiningState` access, wield/crack renderer members |
| `game/src/GameApp.cpp` | Wire LMB/RMB → commands in `tick()`, add PlaceBlock/BreakBlock drain cases, crack overlay + post-effect in `render()`, hotbar→registry mapping |
| `engine/include/voxel/game/PlayerController.h` | Add `updateMining()`, `resetMining()`, `getMiningState()`, expose `getAABB()` if not public |
| `engine/src/game/PlayerController.cpp` | Implement mining update logic |
| `engine/CMakeLists.txt` | Add new source files |
| `tests/CMakeLists.txt` | Add new test files |
| `assets/textures/` | Add 10 crack stage textures (destroy_stage_0–9.png) |

### Previous Story Learnings (from 7.1–7.4)

- **7.1 (Command Queue)**: `EventBus` is NOT yet added to `GameApp` — Story 7.5 must add it. The `EventTypeTraits` system enforces compile-time type safety for pub/sub. Use `bus.publish<EventType::BlockPlaced>(...)` pattern.
- **7.2 (Collision)**: `PlayerController` uses 0.3×0.9×0.3 half-extents (0.6×1.8×0.6 full AABB). `getAABB()` may need to be exposed publicly for placement collision check.
- **7.3 (Movement)**: `updateMining` should be called AFTER movement update in tick to use the current player position. The `MovementInput` struct pattern shows how to pass input state cleanly.
- **7.4 (DDA Raycast)**: `m_raycastResult` is updated in `render()`, read in `tick()` — one frame stale is acceptable. `previousPos` is the placement target. The raycast skips `hasCollision=false` blocks. The `drawBlockHighlight()` method in GameApp shows how to render overlays on block faces (use as reference for crack overlay positioning).
- **Code review patterns**: Stories 7.2 and 7.1 both had code review findings. Common issues: missing edge cases in collision checks, type safety enforcement. Ensure `getAABB()` checks all footprint positions, not just center.
- **Test pattern**: Use `makeTestRegistry()` + `setupFlatGround()` helpers from `TestPlayerController.cpp`. All tests use Catch2 v3 with `TEST_CASE`/`SECTION`/`CHECK` macros.

### Naming Conventions (from project-context.md)

- Classes: `PascalCase` (`MiningState`, `WieldMeshRenderer`, `CrackOverlay`)
- Methods: `camelCase` (`updateMining()`, `calculateBreakTime()`, `resetMining()`)
- Members: `m_` prefix (`m_progress`, `m_breakTime`, `m_crackStage`)
- Constants: `SCREAMING_SNAKE` (`MIN_BREAK_TIME`, `MAX_CRACK_STAGES`)
- Files: `PascalCase` (`MiningState.h`, `WieldMeshRenderer.cpp`)
- Namespaces: `voxel::game` for gameplay, `voxel::renderer` for rendering
- Booleans: `is`/`has`/`should` prefix (`m_isMining`, `isBuildableTo`)
- Enums: `enum class PascalCase { PascalCase }` — e.g., `WieldAnimType::Idle`

---

## Dev Agent Record

### Files Created

| File | Purpose |
|------|---------|
| `engine/include/voxel/game/MiningState.h` | MiningState struct + calculateBreakTime free function |
| `engine/include/voxel/renderer/WieldMeshRenderer.h` | WieldAnimState struct with animation update/trigger logic |
| `tests/game/TestMiningState.cpp` | Mining system unit tests (6 test cases, 60 assertions) |
| `tests/game/TestBlockPlacement.cpp` | Placement/breaking validation tests (8 test cases, 54 assertions) |

### Files Modified

| File | Changes |
|------|---------|
| `engine/include/voxel/game/PlayerController.h` | Added MiningState include, updateMining/resetMining/getMiningState methods, m_miningState member |
| `engine/src/game/PlayerController.cpp` | Implemented updateMining() with hardness lookup, distance check, progress accumulation, crack stage calc |
| `game/src/GameApp.h` | Added EventBus, WieldMeshRenderer includes; handleBlockInteraction, resolveHotbarBlockId, drawCrackOverlay, drawPostEffectTint, drawWieldMesh methods; m_eventBus, m_prevHotbarSlot, m_wieldAnim members |
| `game/src/GameApp.cpp` | Added HOTBAR_BLOCK_IDS mapping; resolveHotbarBlockId; handleBlockInteraction (mining update + PlaceBlock/BreakBlock command push/drain + validation); drawCrackOverlay (face-projected quad with progressive darkening); drawPostEffectTint (head-inside-block fullscreen tint); drawWieldMesh (isometric block with idle/mining/place/switch animations); wield animation triggers |
| `tests/CMakeLists.txt` | Added TestMiningState.cpp and TestBlockPlacement.cpp |

### Design Decisions

1. **Mining logic on PlayerController** (not GameApp): Keeps mining state co-located with the player entity. `updateMining` takes ChunkManager + BlockRegistry to look up block hardness at the target position.
2. **Two-phase command handling**: Movement commands drain into physics, then block commands drain separately. This ensures mining uses post-physics player position for distance checks (per 7.3 learnings).
3. **ImGui-based rendering for overlays**: Crack overlay, post-effect tint, and wield mesh all use ImGui draw lists (consistent with existing block highlight and crosshair). GPU-optimized Vulkan pipelines for these can follow in a future shader story if needed.
4. **Crack overlay as face-projected quad**: The 4 corners of the hit face are projected to screen space (same technique as block highlight). Progressive darkening uses alpha ramp (stage 0: 40 alpha, stage 9: 220 alpha) + crack pattern lines at higher stages.
5. **HOTBAR_BLOCK_IDS lookup table**: Static mapping from display slot index to registry string IDs. Returns BLOCK_AIR if name not found in registry (graceful fallback).

### Test Results

- Mining tests: 6 test cases, 60 assertions - all pass
- Placement tests: 8 test cases, 54 assertions - all pass
- Full regression suite: 226 test cases, 489,674 assertions - all pass

### Scope Boundaries

**IN SCOPE (V1):**
- Mining time system with bare-hands formula (`hardness * 1.5`)
- Crack overlay with 10-stage textures
- Block break via command pattern → `setBlock(pos, BLOCK_AIR)` → event
- Block place via command pattern → validation → `setBlock(pos, blockId)` → event
- EventBus wiring in GameApp
- Hotbar → registry ID resolution
- Post-effect color overlay (head-in-block tint)
- Wield mesh rendering with animations
- Unit tests for mining + placement logic

**OUT OF SCOPE (future epics):**
- Tool system / tool-speed multipliers (Epic 9+ — formula is implemented but tool branch always falls through to bare hands)
- Item drops as entities (just log for now — no inventory/entity system yet)
- Experience from breaking (V1: always 0)
- Sound effects on break/place (no audio system yet)
- Particle effects on break (no particle system yet)
- `get_state_for_placement` Lua callback (Epic 9 — use `baseStateId` default)
- `on_destruct` callback for replaced blocks (Epic 9)
- Health system for damage blocks (just log damage, no HP bar)

### Project Structure Notes

- New gameplay files go under `engine/include/voxel/game/` and `engine/src/game/`
- New renderer files go under `engine/include/voxel/renderer/` and `engine/src/renderer/`
- Shaders go under `assets/shaders/` as `.vert`/`.frag` GLSL files (compiled to SPIR-V by CMake)
- Textures go under `assets/textures/`
- Tests go under `tests/game/` following existing `Test*.cpp` naming

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-07-player-interaction.md — Story 7.5]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 7 (Physics), System 10 (Network-Readiness)]
- [Source: _bmad-output/planning-artifacts/ux-spec.md — Section 4 (Block Interaction), Section 5 (HUD Layout)]
- [Source: _bmad-output/planning-artifacts/PRD.md — FR-3 (Player-World Interaction)]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Threading Rules]
- [Source: engine/include/voxel/game/GameCommand.h — PlaceBlock/BreakBlock payloads]
- [Source: engine/include/voxel/game/EventBus.h — BlockPlacedEvent/BlockBrokenEvent]
- [Source: engine/include/voxel/physics/Raycast.h — RaycastResult with previousPos]
- [Source: engine/include/voxel/world/ChunkManager.h — setBlock/getBlock with dirty propagation]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition fields (hardness, isBuildableTo)]
- [Source: _bmad-output/implementation-artifacts/7-4-dda-raycasting-block-highlight.md — Previous story learnings]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
