# Story 7.4: DDA Raycasting + Block Highlight

Status: review

## Story

As a developer,
I want to cast a ray from the camera to identify which block the player is looking at,
so that block targeting for place/break works.

## Acceptance Criteria

1. `voxel::physics::raycast(origin, direction, maxDistance, world, registry) -> RaycastResult` free function using the Amanatides & Woo DDA algorithm
2. `RaycastResult` contains: `bool hit`, `glm::ivec3 blockPos`, `glm::ivec3 previousPos` (for placement), `BlockFace face`, `float distance`
3. Max distance: 6 blocks (configurable via `static constexpr float MAX_REACH = 6.0f`)
4. Correct face identification: the face of the block the ray entered (needed for placement side in Story 7.5)
5. Block highlight: render wireframe overlay on the targeted block using ImGui's `ImDrawList` for 3D line rendering (project 12 edges of the block AABB to screen space)
6. Raycast updates every frame (in `render()`, not `tick()`) for responsive feel — uses camera position/forward directly
7. Unit tests: ray hitting a block returns correct position and face, ray missing returns no hit, diagonal ray, edge/corner cases, non-solid transparent blocks are skipped

## Tasks / Subtasks

- [x] Task 1: Create RaycastResult struct and raycast function (AC: 1, 2, 3, 4)
  - [x] 1.1 Create `engine/include/voxel/physics/Raycast.h` with `RaycastResult` struct and `raycast()` free function declaration
  - [x] 1.2 Create `engine/src/physics/Raycast.cpp` implementing Amanatides & Woo DDA
  - [x] 1.3 Register new source file in `engine/CMakeLists.txt`
- [x] Task 2: Integrate raycast into GameApp (AC: 6)
  - [x] 2.1 Add `#include "voxel/physics/Raycast.h"` to GameApp.h
  - [x] 2.2 Add `voxel::physics::RaycastResult m_raycastResult{}` member to GameApp
  - [x] 2.3 In `GameApp::render()`, before drawing, compute ray from `m_camera.getPosition()` + `m_camera.getForward()` and call `raycast()`
  - [x] 2.4 Store result in `m_raycastResult` for use by highlight drawing and debug overlay
- [x] Task 3: Render block highlight wireframe (AC: 5)
  - [x] 3.1 Add `GameApp::drawBlockHighlight()` private method
  - [x] 3.2 If `m_raycastResult.hit`, project the 8 corners of the targeted block AABB to screen space using VP matrix
  - [x] 3.3 Draw 12 edges as lines on ImGui foreground draw list (white, 2px, slight alpha)
  - [x] 3.4 Call `drawBlockHighlight()` in `GameApp::render()` alongside `drawCrosshair()`
- [x] Task 4: Add targeted block info to debug overlay (AC: 6)
  - [x] 4.1 In `buildDebugOverlay()`, show targeted block position, face, distance, and block string ID when hit
- [x] Task 5: Unit tests (AC: 7)
  - [x] 5.1 Create `tests/physics/TestRaycast.cpp`
  - [x] 5.2 Register new test file in `tests/CMakeLists.txt`
  - [x] 5.3 Test: ray straight down onto flat ground hits correct block and returns NegY face
  - [x] 5.4 Test: ray into wall returns correct block position and face (PosX, NegX, PosZ, NegZ)
  - [x] 5.5 Test: ray into empty air returns no hit
  - [x] 5.6 Test: ray exceeding max distance returns no hit
  - [x] 5.7 Test: `previousPos` is the air block before the hit (placement position)
  - [x] 5.8 Test: ray passes through non-solid/transparent blocks (glass has `hasCollision=true` so it stops, but air/water with `hasCollision=false` is skipped)
  - [x] 5.9 Test: diagonal ray through multiple voxels hits the first solid one
  - [x] 5.10 Test: ray origin inside a solid block returns immediate hit at that position (edge case: player clipped into block)

## Dev Notes

### Algorithm: Amanatides & Woo DDA

The DDA (Digital Differential Analyzer) algorithm traverses the voxel grid efficiently by stepping one voxel at a time along the ray:

```cpp
RaycastResult raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,  // MUST be normalized
    float maxDistance,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry)
{
    // 1. Starting voxel = floor(origin)
    glm::ivec3 blockPos = glm::ivec3(glm::floor(origin));
    glm::ivec3 prevPos = blockPos;

    // 2. Step direction per axis: +1 or -1
    glm::ivec3 step;
    step.x = (direction.x >= 0.0f) ? 1 : -1;
    step.y = (direction.y >= 0.0f) ? 1 : -1;
    step.z = (direction.z >= 0.0f) ? 1 : -1;

    // 3. tDelta = how far along ray (in t) to cross one full voxel on each axis
    glm::vec3 tDelta;
    tDelta.x = (direction.x != 0.0f) ? std::abs(1.0f / direction.x) : FLT_MAX;
    tDelta.y = (direction.y != 0.0f) ? std::abs(1.0f / direction.y) : FLT_MAX;
    tDelta.z = (direction.z != 0.0f) ? std::abs(1.0f / direction.z) : FLT_MAX;

    // 4. tMax = distance (in t) to next voxel boundary on each axis
    //    When step > 0: distance to the next integer boundary above origin
    //    When step < 0: distance to the current integer boundary below origin
    //    NOTE: when origin is exactly on a boundary and step < 0, fractional part = 0,
    //    so tMax = 0 — this correctly triggers an immediate step to the previous voxel.
    glm::vec3 tMax;
    tMax.x = ((step.x > 0) ? (std::floor(origin.x) + 1.0f - origin.x)
                            : (origin.x - std::floor(origin.x))) * tDelta.x;
    tMax.y = ((step.y > 0) ? (std::floor(origin.y) + 1.0f - origin.y)
                            : (origin.y - std::floor(origin.y))) * tDelta.y;
    tMax.z = ((step.z > 0) ? (std::floor(origin.z) + 1.0f - origin.z)
                            : (origin.z - std::floor(origin.z))) * tDelta.z;

    // 5. Track which face was last entered (initially none — first block is origin block)
    BlockFace face = BlockFace::PosY; // placeholder until first step

    // 6. Traverse — check current voxel first, then step
    float distance = 0.0f;
    while (distance <= maxDistance) {
        // Y-bounds check: world is [WORLD_MIN_Y .. WORLD_MAX_Y] (0..255)
        // ChunkManager::getBlock() asserts Y in range — must guard here
        if (blockPos.y < 0 || blockPos.y > 255) {
            // Stepped outside world — no hit possible on this axis
            // Continue stepping only if other axes might bring us back
            // For simplicity: treat as miss. Ray going up into sky or below bedrock.
            break;
        }

        // Check block at current position
        uint16_t blockId = world.getBlock(blockPos);
        if (blockId != BLOCK_AIR) {
            const auto& def = registry.getBlockType(blockId);
            if (def.hasCollision) {
                return RaycastResult{true, blockPos, prevPos, face, distance};
            }
        }

        prevPos = blockPos;

        // Step along axis with smallest tMax
        // Face entered = OPPOSITE of step direction (we stepped +X → entered NegX face)
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            distance = tMax.x;
            tMax.x += tDelta.x;
            blockPos.x += step.x;
            face = (step.x > 0) ? BlockFace::NegX : BlockFace::PosX;
        } else if (tMax.y < tMax.z) {
            distance = tMax.y;
            tMax.y += tDelta.y;
            blockPos.y += step.y;
            face = (step.y > 0) ? BlockFace::NegY : BlockFace::PosY;
        } else {
            distance = tMax.z;
            tMax.z += tDelta.z;
            blockPos.z += step.z;
            face = (step.z > 0) ? BlockFace::NegZ : BlockFace::PosZ;
        }
    }
    return RaycastResult{false, {}, {}, BlockFace::PosY, 0.0f};
}
```

**Critical: Face identification logic** — The face the ray enters is the OPPOSITE of the step direction on the last axis advanced. If we stepped +X to reach a block, the ray entered through the block's NegX face.

### Block Highlight Rendering Approach

Use ImGui's `ImDrawList` for 3D wireframe overlay (same pattern as existing `drawCrosshair()` and `drawHotbar()`). This avoids creating a new Vulkan pipeline:

```cpp
void GameApp::drawBlockHighlight()
{
    if (!m_raycastResult.hit) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;

    glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();

    // Block AABB: integer position → (pos, pos+1)
    glm::vec3 bmin = glm::vec3(m_raycastResult.blockPos);
    glm::vec3 bmax = bmin + glm::vec3(1.0f);

    // Slight expansion to prevent z-fighting with block faces
    constexpr float OFFSET = 0.002f;
    bmin -= glm::vec3(OFFSET);
    bmax += glm::vec3(OFFSET);

    // Project 8 corners to screen space
    // Draw 12 edges between visible corners
    // Skip any edge where EITHER endpoint is behind the camera (clipW <= 0)
    // since perspective divide produces nonsensical coordinates behind the near plane
}
```

The 8 cube corners and 12 edges are straightforward. Project each corner with the VP matrix, perform perspective divide, convert to screen coordinates, then draw lines. Skip any edge where either vertex is behind the near plane (`clipW <= 0`).

### Existing Code to Reuse (DO NOT recreate)

| Component | Location | Usage |
|-----------|----------|-------|
| `Ray` struct | `engine/include/voxel/math/Ray.h` | Available but NOT used — raycast() takes separate `origin`/`direction` params for hot-path simplicity |
| `BlockFace` enum | `engine/include/voxel/renderer/ChunkMesh.h:13` | `PosX=0, NegX=1, PosY=2, NegY=3, PosZ=4, NegZ=5` — reuse this existing enum |
| `BLOCK_AIR` | `engine/include/voxel/world/Block.h` | Constant `0` for air checks |
| `ChunkManager::getBlock(ivec3)` | `engine/include/voxel/world/ChunkManager.h` | Query block at world position, returns `BLOCK_AIR` for unloaded chunks |
| `BlockRegistry::getBlockType(uint16_t)` | `engine/include/voxel/world/BlockRegistry.h` | Get `BlockDefinition` to check `hasCollision` |
| `Camera::getPosition()` | `engine/include/voxel/renderer/Camera.h` | Ray origin |
| `Camera::getForward()` | `engine/include/voxel/renderer/Camera.h` | Ray direction (already normalized) |
| `Camera::getViewMatrix()`, `getProjectionMatrix()` | Same | For screen-space projection in highlight rendering |
| `ImGui::GetForegroundDrawList()` | Dear ImGui | For wireframe overlay (same pattern as `drawCrosshair()`) |
| `BlockDefinition::hasCollision` | `engine/include/voxel/world/Block.h` | Filter which blocks stop the ray |

### RaycastResult Struct

```cpp
// engine/include/voxel/physics/Raycast.h
#pragma once

#include "voxel/renderer/ChunkMesh.h"  // for BlockFace enum

#include <glm/vec3.hpp>

namespace voxel::physics
{

struct RaycastResult
{
    bool hit = false;
    glm::ivec3 blockPos{0};      // Position of the hit block
    glm::ivec3 previousPos{0};   // Air block before hit (placement target for Story 7.5)
    renderer::BlockFace face = renderer::BlockFace::PosY;  // Face the ray entered
    float distance = 0.0f;       // Distance from origin to hit
};

/// Maximum block targeting reach (configurable — used in GameApp).
static constexpr float MAX_REACH = 6.0f;

/// Cast a ray through the voxel grid using Amanatides & Woo DDA.
/// @param origin      Ray origin (world space, e.g., camera eye position).
/// @param direction   Normalized ray direction.
/// @param maxDistance  Maximum traversal distance in blocks.
/// @param world       ChunkManager for block queries.
/// @param registry    BlockRegistry for collision checks.
/// @return RaycastResult with hit=true if a solid block was found.
RaycastResult raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    const world::ChunkManager& world,
    const world::BlockRegistry& registry);

} // namespace voxel::physics
```

### GameApp Integration Points

**In `GameApp.h`:**
- Add `#include "voxel/physics/Raycast.h"`
- Add member: `voxel::physics::RaycastResult m_raycastResult{}`
- Add private method: `void drawBlockHighlight()`

**In `GameApp::render()` — after `beginFrame()`, before `buildDebugOverlay()`:**
```cpp
// Raycast from camera every frame for responsive targeting
{
    glm::vec3 origin = glm::vec3(m_camera.getPosition()); // dvec3 → vec3 is fine for 6-block range
    glm::vec3 dir = m_camera.getForward();
    m_raycastResult = voxel::physics::raycast(origin, dir, voxel::physics::MAX_REACH, m_chunkManager, m_blockRegistry);
}
```

**In `GameApp::render()` — after `drawCrosshair()` call:**
```cpp
drawBlockHighlight();
```

**In `GameApp::buildDebugOverlay()` — after the facing direction text:**
```cpp
if (m_raycastResult.hit)
{
    auto& bp = m_raycastResult.blockPos;
    uint16_t blockId = m_chunkManager.getBlock(bp);
    const auto& def = m_blockRegistry.getBlockType(blockId);
    ImGui::Text("Target: %d, %d, %d (%s) d=%.1f",
                bp.x, bp.y, bp.z, def.stringId.c_str(), m_raycastResult.distance);
}
```

### File Structure

**New files to create:**
```
engine/include/voxel/physics/Raycast.h     # RaycastResult struct + raycast() declaration
engine/src/physics/Raycast.cpp             # DDA implementation
tests/physics/TestRaycast.cpp              # Unit tests
```

**Files to modify:**
```
engine/CMakeLists.txt                      # Add src/physics/Raycast.cpp to sources
tests/CMakeLists.txt                       # Add physics/TestRaycast.cpp to test sources
game/src/GameApp.h                         # Add raycast include, result member, drawBlockHighlight()
game/src/GameApp.cpp                       # Raycast in render(), highlight drawing, debug overlay info
```

### Testing Strategy

Tests use the same pattern as `TestPlayerController.cpp` — create a `ChunkManager` + `BlockRegistry`, fill blocks manually, then call `raycast()` and verify the result.

**Test helper pattern (from Story 7.2/7.3):**
```cpp
#include <catch2/catch_test_macros.hpp>
#include "voxel/physics/Raycast.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/BlockRegistry.h"

// Reuse makeTestRegistry() and setupFlatGround() patterns from TestPlayerController.cpp
// Or duplicate the minimal setup needed (register stone block ID 1, set blocks in ChunkManager)
```

**Tag tests with `[physics][raycast]`** to match the project-context testing taxonomy (`Physics/DDA: Hit detection, face ID, max distance`).

**Logger null guard**: In test context, spdlog logger may be null. Existing tests show this is handled — raycast code should not log in the hot path anyway.

### Project Structure Notes

- `engine/include/voxel/physics/` is a **new directory** — this is the first file in the physics module as planned in the architecture (`physics/ → Collision, DDA`)
- The `physics` namespace is `voxel::physics` following the convention
- `BlockFace` enum lives in `renderer/ChunkMesh.h` — import it from there rather than creating a duplicate. Do NOT move `BlockFace` to a shared location as part of this story — just use the existing include. Future refactor can address cross-module coupling if needed
- No `Result<T>` needed for raycast — it's a hot-path function returning a simple struct. Failure = `hit == false`

### Previous Story Intelligence (from 7.2 + 7.3)

**Patterns established:**
- `tickPhysics()` uses `MovementInput` struct — similarly, raycast uses `RaycastResult` return struct
- Tests use `makeTestRegistry()` helper + `ChunkManager` with manually placed blocks
- Logger null guards: `if (Log::getLogger()) {...}` — but raycast should avoid logging in hot path
- `ChunkManager::getBlock()` returns `BLOCK_AIR (0)` for unloaded chunks — safe for raycast
- Y bounds: world is 0-255 (`ChunkColumn::COLUMN_HEIGHT = 256`, valid Y is 0-255). Raycast MUST bounds-check Y before calling `getBlock()` — see pseudocode. Use constants from `ChunkColumn.h` rather than hardcoding 255
- Story 7.2 code review found: always check all blocks the player footprint overlaps. Raycast is simpler (single voxel at a time) but apply same thoroughness to edge cases

**Code review findings from 7.2 to apply:**
- Ensure proper bounds checking on block positions before calling `getBlock()`
- Test edge cases at chunk boundaries (ChunkManager handles cross-chunk queries transparently)

**Files from 7.2/7.3 to NOT modify:**
- `PlayerController.h/cpp` — raycast is independent of player physics
- `GameCommand.h` — no new command types needed (raycast is render-time, not tick-time)

### Architecture Compliance

- **ADR-004**: Chunks NOT in ECS — block queries via `ChunkManager::getBlock()` (compliant)
- **ADR-008**: Exceptions disabled — raycast returns a struct, no exceptions
- **ADR-010**: Command Pattern — raycast is a read-only query, no commands needed. Story 7.5 will push `PlaceBlock`/`BreakBlock` commands based on raycast results
- **Tick vs Render**: Raycast runs per-frame in `render()` for responsive feel (not per-tick). This is correct per AC and UX spec. The raycast result is ephemeral render state, not game state
- **Naming**: `voxel::physics::raycast()` — free function, camelCase, in `physics` namespace
- **One class per file**: RaycastResult struct + free function in one header is fine (trivially related types)
- **Max 500 lines**: DDA implementation should be ~80-100 lines

### References

- [Source: epics/epic-07-player-interaction.md#Story 7.4]
- [Source: architecture.md#System 7 — DDA 3D Raycasting]
- [Source: ux-spec.md#Section 4 — Block Interaction — Targeting]
- [Source: ux-spec.md#Section 8 — Visual Feedback — Block Highlight]
- [Source: project-context.md#Testing Strategy — Physics/DDA]
- [Source: project-context.md#Naming Conventions]
- [Source: PRD.md#FR-3.3 — DDA 3D Raycasting]
- [Source: PRD.md#FR-3.7 — Block highlight overlay]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

None — clean implementation with zero build errors and all tests passing on first run.

### Completion Notes List

- Implemented Amanatides & Woo DDA algorithm in `voxel::physics::raycast()` free function (~95 lines)
- `RaycastResult` struct contains hit, blockPos, previousPos, face, distance — all ACs satisfied
- Y bounds checked against `ChunkColumn::COLUMN_HEIGHT` constant (not hardcoded 255)
- Block highlight wireframe via ImGui `ImDrawList` — projects 8 AABB corners to screen, draws 12 edges, skips behind-camera vertices
- Z-fighting prevention with 0.002 AABB expansion offset
- Raycast runs per-frame in `render()` for responsive feel (not tick-based)
- Debug overlay shows targeted block position, string ID, face name, and distance
- 8 test cases (62 assertions) covering all AC-7 scenarios: hit, miss, face ID, max distance, previousPos, non-solid skip, diagonal, origin-inside-block
- Full regression suite: 212 test cases, 489560 assertions — all pass

### File List

**New files:**
- `engine/include/voxel/physics/Raycast.h` — RaycastResult struct + raycast() declaration
- `engine/src/physics/Raycast.cpp` — Amanatides & Woo DDA implementation
- `tests/physics/TestRaycast.cpp` — 8 unit test cases

**Modified files:**
- `engine/CMakeLists.txt` — Added `src/physics/Raycast.cpp` to sources
- `tests/CMakeLists.txt` — Added `physics/TestRaycast.cpp` to test sources
- `game/src/GameApp.h` — Added raycast include, `m_raycastResult` member, `drawBlockHighlight()` declaration
- `game/src/GameApp.cpp` — Raycast in render(), drawBlockHighlight() implementation, debug overlay target info

## Change Log

- 2026-03-29: Implemented Story 7.4 — DDA Raycasting + Block Highlight (all tasks complete)
