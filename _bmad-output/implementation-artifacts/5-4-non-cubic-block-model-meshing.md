# Story 5.4: Non-Cubic Block Model Meshing

Status: done

## Story

As a developer,
I want a second meshing path for blocks that aren't full cubes,
so that torches, flowers, slabs, stairs, fences, and other shaped blocks render correctly.

## Why Now

The greedy mesher (5.3) only handles full cubes. Non-cubic blocks (slabs, stairs, cross-pattern flowers, torches, fences) need a completely different meshing approach. If we add this after the pipeline is async (5.6) and uploaded to gigabuffer (5.7), every interface (ChunkMesh, snapshot, upload, shader) needs retrofitting. The quad format already reserves bit 48 for this.

## Acceptance Criteria

1. **AC1 — BlockModelType routing**: `MeshBuilder::buildNaive()` (and future `buildGreedy()`) skips non-`FullCube` blocks during greedy/naive pass; a separate pass emits their geometry
2. **AC2 — ModelVertex format**: `ModelVertex` struct defined for arbitrary (non-quad-merged) geometry — position, normal, UV, block state ID, AO, light slots
3. **AC3 — ChunkMesh extension**: `ChunkMesh` gains `std::vector<ModelVertex> modelVertices` + `uint32_t modelVertexCount` alongside existing quads
4. **AC4 — Built-in model types**: Geometry generators for `Slab`, `Cross`, and `Torch` produce correct vertices from `BlockDefinition` + block state
5. **AC5 — isFullFace()**: `BlockDefinition::isFullFace(BlockFace)` returns whether a given face fully covers the 1×1 block boundary, used for face culling between cubic and non-cubic blocks
6. **AC6 — Face culling correctness**: A slab on stone → stone's top face toward slab IS emitted (slab doesn't fully cover), slab's bottom face toward stone is NOT emitted (stone fully covers)
7. **AC7 — ModelRegistry stub**: `ModelRegistry` class exists with API for future `JsonModel`/`MeshModel`/`Connected`/`Custom` loading (V1: returns empty for unimplemented types)
8. **AC8 — Unit tests**: Tests for each implemented model type (Slab, Cross, Torch), face culling between cubic/non-cubic, ModelVertex packing roundtrip, empty section fast-path still works

## Scope Clarification — V1 vs Future

**V1 (this story):**
- `Slab`, `Cross`, `Torch` — fully functional geometry generators
- `ModelVertex` struct + `ChunkMesh` extension
- `isFullFace()` on `BlockDefinition`
- `ModelRegistry` class with stub API
- Integration into `MeshBuilder::buildNaive()`

**Deferred (future stories / Lua scripting epic):**
- `Stair` — complex 3-box geometry with state-dependent rotation (needs robust state system)
- `Connected` — fence/wall connectivity logic checking 4 neighbors against `connects_to` groups
- `JsonModel` — Minecraft-style rotated cuboid elements, loaded from Lua registration
- `MeshModel` — OBJ file loading, `ModelRegistry::loadOBJ()`
- `Custom` — Lua-defined node boxes

The deferred types will use the same `ModelVertex` format and `ModelRegistry` infrastructure built here. Their `ModelRegistry` methods should exist but return an error/empty result indicating "not yet implemented."

## Tasks / Subtasks

- [x] **Task 1: Define ModelVertex struct** (AC: #2, #3)
  - [x] 1.1 Add `ModelVertex` to `ChunkMesh.h` — fields: `glm::vec3 position`, `glm::vec3 normal`, `glm::vec2 uv`, `uint16_t blockStateId`, `uint8_t ao`, `uint8_t flags`
  - [x] 1.2 Extend `ChunkMesh` with `std::vector<ModelVertex> modelVertices` and `uint32_t modelVertexCount`
  - [x] 1.3 Add `isEmpty()` method that checks both `quadCount == 0 && modelVertexCount == 0`

- [x] **Task 2: Add isFullFace() to BlockDefinition** (AC: #5)
  - [x] 2.1 Add `[[nodiscard]] bool isFullFace(uint8_t faceIndex, const StateMap& state) const` method to `BlockDefinition` in `Block.h` (uses uint8_t to avoid world→renderer dependency; matches BlockFace enum ordering)
  - [x] 2.2 Implementation: `FullCube` → always true; `Slab` → true for top/bottom depending on `half` state; all others → false
  - [x] 2.3 Unit test: verify `isFullFace()` returns correct values for each ModelType (FullCube, Slab bottom/top/default, Cross, Torch, Stair, Connected)

- [x] **Task 3: Create ModelMesher** (AC: #4)
  - [x] 3.1 Create `engine/include/voxel/renderer/ModelMesher.h` + `engine/src/renderer/ModelMesher.cpp` in `voxel::renderer` namespace
  - [x] 3.2 `generateSlab(x, y, z, blockDef, stateValues, ao, outVertices)` — half-height box (0–0.5 or 0.5–1.0 based on `half` property), 36 vertices
  - [x] 3.3 `generateCross(x, y, z, blockDef, outVertices)` — two diagonal quads, 4 faces (front+back per quad), 24 vertices total
  - [x] 3.4 `generateTorch(x, y, z, blockDef, stateValues, outVertices)` — thin vertical box centered (7/16 to 9/16 XZ, 0 to 10/16 Y), wall variant offset by facing direction

- [x] **Task 4: Create ModelRegistry stub** (AC: #7)
  - [x] 4.1 Create `engine/include/voxel/renderer/ModelRegistry.h` + `engine/src/renderer/ModelRegistry.cpp` in `voxel::renderer` namespace
  - [x] 4.2 API: `getModelVertices(x, y, z, blockDef, state, outVertices)` — appends to output vector
  - [x] 4.3 For `Slab`/`Cross`/`Torch`: delegate to `ModelMesher` generators
  - [x] 4.4 For `Stair`/`Connected`/`JsonModel`/`MeshModel`/`Custom`: no output, log warning via VX_LOG_WARN
  - [x] 4.5 Pre-bake deferred to future — V1 generates on each call

- [x] **Task 5: Integrate into MeshBuilder** (AC: #1, #6)
  - [x] 5.1 In `buildNaive()`, after the cubic face-culling loop, call `buildNonCubicPass()` for non-cubic blocks
  - [x] 5.2 During the cubic pass: when checking face visibility, use `isFullFace()` instead of just `isSolid` — a non-cubic neighbor that doesn't fully cover the face means the cubic face IS emitted
  - [x] 5.3 In `buildNonCubicPass()`: iterate section, for each non-FullCube block, query `ModelRegistry` for vertices, append to `modelVertices`
  - [x] 5.4 Greedy mesher updated: added `buildCubicOpacityPad()` that excludes non-cubic blocks from face mask generation, ensuring only FullCube blocks get greedy-merged quads
  - [x] 5.5 Bit 48 not set in V1 — non-cubic blocks use separate ModelVertex buffer, bit 48 becomes relevant at gigabuffer upload (Story 5.7)

- [x] **Task 6: Unit tests** (AC: #8)
  - [x] 6.1 Create `tests/renderer/TestNonCubicMeshing.cpp`
  - [x] 6.2 Test: empty section → 0 quads, 0 model vertices, isEmpty() == true
  - [x] 6.3 Test: single slab block in empty section → 36 model vertices (6 faces × 6 verts), 0 quads
  - [x] 6.4 Test: slab on stone → stone's PosY face culled by slab's full NegY face, slab produces 36 model vertices
  - [x] 6.5 Test: cross block (flower) → 24 model vertices (4 quads × 6 verts), 0 quads
  - [x] 6.6 Test: torch block → 36 model vertices (thin box, 6 faces × 6 verts)
  - [x] 6.7 Test: ModelVertex roundtrip — construct, verify all fields
  - [x] 6.8 Test: `isFullFace()` truth table for FullCube, Slab (bottom/top/default), Cross, Torch, Stair, Connected
  - [x] 6.9 Add test source to `tests/CMakeLists.txt`
  - [x] 6.10 Additional: ChunkMesh::isEmpty() tests, greedy mesher non-cubic exclusion tests, mixed cubic+non-cubic section tests

## Dev Notes

### Architecture Compliance

- **One class per file**: `ModelMesher` in its own header, `ModelRegistry` in its own header/cpp pair
- **Namespace**: `voxel::renderer` for all new types
- **Error handling**: No exceptions — `ModelRegistry` methods return empty vectors for unimplemented types, log a warning
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, SCREAMING_SNAKE constants
- **Max 500 lines per file** — if `ModelMesher` grows too large with all generators, split into `SlabMesher.h`, `CrossMesher.h`, etc.

### Existing Code to Reuse

- **`BlockFace` enum**: Already in `AmbientOcclusion.h` (line ~25) — reuse, do NOT redefine
- **`packQuad()` / `unpackX()` etc.**: In `ChunkMesh.h` — the quad path remains for cubic blocks
- **`BlockRegistry::getBlock(id)`**: Look up `modelType` during meshing
- **`BlockRegistry::getStateValues(stateId)`**: Get state properties (e.g., `half=top` for slabs)
- **`AmbientOcclusion::computeFaceAO()`**: Can be used for non-cubic face AO too
- **`ChunkSection::getBlock(x,y,z)`**: Access block data during iteration

### Quad Format Reference (64-bit)

```
[0:5]    X pos        [6:11]   Y pos        [12:17]  Z pos
[18:23]  Width-1      [24:29]  Height-1     [30:39]  Block state ID (10 bits)
[40:42]  Face dir     [43:44]  AO 0+1       [45:46]  AO 2+3
[47]     Flip         [48]     Is non-cubic  [49:52]  Sky light
[53:56]  Block light  [57:59]  Tint index   [60:61]  Waving type
[62:63]  Reserved
```

Bit 48 = 1 signals the vertex shader to use the model vertex path instead of quad reconstruction. This bit is set by `MeshBuilder` when emitting non-cubic geometry through the quad format path. However, for V1, non-cubic blocks use the separate `ModelVertex` buffer, so bit 48 may not be needed yet — it becomes relevant when both buffers are uploaded to gigabuffer (Story 5.7).

### ModelVertex Format Design

```cpp
struct ModelVertex {
    glm::vec3 position;      // World-relative position within chunk (0-16 range)
    glm::vec3 normal;         // Face normal for lighting
    glm::vec2 uv;             // Texture coordinates (0-1, maps to texture array layer)
    uint16_t blockStateId;    // For texture lookup in shader
    uint8_t ao;               // Ambient occlusion (0-3, same scale as quad AO)
    uint8_t flags;            // Bit 0: tint index LSB, Bits 1-2: waving type, etc.
    // Total: 36 bytes — pad to 40 for alignment if needed, or pack tighter
};
```

Consider using a tightly packed format instead if vertex count is a concern. The exact format should be finalized during implementation based on what the GPU shader (Story 6.2) will consume. For V1, clarity > compression.

### Face Culling Logic for Non-Cubic Blocks

The key insight: face culling depends on whether the *neighbor* fully covers the shared face, not whether the block itself is solid.

```
shouldEmitFace(thisBlock, neighborBlock, face):
  if neighbor is air → emit
  if neighbor is FullCube and solid → do NOT emit (fully occluded)
  if neighbor is non-cubic → check neighborBlock.isFullFace(oppositeFace)
    true → do NOT emit
    false → emit
```

For the cubic pass (existing `buildNaive`):
```
shouldEmitCubicFace(thisBlock, neighborBlock, face):
  OLD: emit if neighbor is air or transparent
  NEW: emit if neighbor is air OR transparent OR !neighbor.isFullFace(oppositeFace)
```

This means a stone block next to a slab WILL emit its face toward the slab (since slab doesn't fully cover).

### Slab Geometry Detail

A slab is a half-height box. With `half=bottom`: box from (0,0,0) to (1,0.5,1). With `half=top`: box from (0,0.5,0) to (1,1,1).

Faces: 6 faces like a cube, but Y-extent is halved. Top and bottom faces are at y=0.5 or y=0/y=1.

`isFullFace()` for Slab:
- `half=bottom`: NegY (bottom) → true, PosY (top) → false, sides → false (half-height)
- `half=top`: PosY (top) → true, NegY (bottom) → false, sides → false

### Cross Geometry Detail

Two quads intersecting diagonally through the block center:
- Quad A: corners at (0,0,0), (1,0,1), (1,1,1), (0,1,0) — diagonal from -X-Z to +X+Z
- Quad B: corners at (1,0,0), (0,0,1), (0,1,1), (1,1,0) — diagonal from +X-Z to -X+Z

Both quads are double-sided (emit front and back faces, or disable backface culling for these). This gives 4 triangles total (8 if double-sided with separate normals).

`isFullFace()` for Cross: always false (transparent from all 6 cardinal directions).

### Torch Geometry Detail

Standing torch: thin box centered at (7/16, 0, 7/16) to (9/16, 10/16, 9/16).
Wall torch: same box but tilted/offset toward the attached wall face.

`isFullFace()` for Torch: always false.

### What NOT To Do

- Do NOT modify `packQuad()` or the 64-bit quad format — non-cubic blocks use `ModelVertex` buffer
- Do NOT create a separate `NonCubicMeshBuilder` class — add methods to existing `MeshBuilder`
- Do NOT implement `Stair`, `Connected`, `JsonModel`, `MeshModel`, or `Custom` geometry generators — stub only
- Do NOT add OBJ parsing — that's future work
- Do NOT add Lua registration hooks — that's Epic 9
- Do NOT add async/threading — that's Story 5.6
- Do NOT upload to gigabuffer — that's Story 5.7
- Do NOT worry about shader consumption — that's Epic 6

### File Structure

```
engine/include/voxel/renderer/
  ChunkMesh.h           ← MODIFY: add ModelVertex, extend ChunkMesh
  MeshBuilder.h         ← MODIFY: add non-cubic meshing method
  ModelMesher.h         ← CREATE: geometry generators for Slab/Cross/Torch
  ModelRegistry.h       ← CREATE: model lookup + caching

engine/src/renderer/
  MeshBuilder.cpp       ← MODIFY: integrate non-cubic pass
  ModelRegistry.cpp     ← CREATE: implementation

engine/include/voxel/world/
  Block.h               ← MODIFY: add isFullFace() method

tests/renderer/
  TestNonCubicMeshing.cpp  ← CREATE
  CMakeLists.txt           ← MODIFY: add test source
```

### Project Structure Notes

- All new files go in existing `engine/include/voxel/renderer/` and `engine/src/renderer/` directories
- Test file in existing `tests/renderer/` directory
- No new directories needed
- Follows established pattern: header in `include/`, implementation in `src/`, test in `tests/`

### Previous Story Intelligence

**From Story 5.1 (done):**
- `ChunkMesh.h` already has the quad format with bit 48 reserved
- `MeshBuilder::buildNaive()` iterates Y-Z-X, skips air, checks 6 faces
- Test pattern: register blocks → fill section → call buildNaive → assert quad count
- MSVC C4100 warning fix: use `[[maybe_unused]]` or cast to `(void)param`

**From Story 5.2 (in-progress):**
- `AmbientOcclusion.h` has `vertexAO()`, `computeFaceAO()`, `shouldFlipQuad()`
- Padded 18x18x18 opacity array pattern — reusable for non-cubic face culling
- If 5.2 is not complete when implementing 5.4, hardcode AO to 3 for model vertices

**From Story 5.3 (ready-for-dev):**
- `buildGreedy()` will be added to MeshBuilder — non-cubic blocks must be excluded from greedy bitmask
- If 5.3 is not implemented yet, only `buildNaive()` needs modification
- Future: `buildSection()` will orchestrate both greedy (cubic) and model (non-cubic) passes

### Git Intelligence

Recent commits show the pattern:
```
c541086 feat(renderer): add ambient occlusion calculation for naive mesher
e7ce707 feat(renderer): implement naive face culling mesher with chunk mesh and quad packing
```

Commit convention: `feat(renderer): <description>` for renderer features.

### Testing Standards

- Framework: Catch2 v3
- Pattern: `TEST_CASE("description", "[renderer][meshing]")` with `SECTION` blocks
- Register test blocks via helper functions (see `TestMeshBuilder.cpp` pattern)
- Use `REQUIRE()` for assertions, `BENCHMARK()` for performance
- No GPU/Vulkan in unit tests — test CPU-side logic only

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md — Story 5.4]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 4 Block Registry, Packed Quad Format]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling rules]
- [Source: engine/include/voxel/renderer/ChunkMesh.h — Current quad format and packing helpers]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition with ModelType enum]
- [Source: engine/src/renderer/MeshBuilder.cpp — Current buildNaive() implementation]
- [Source: tests/renderer/TestMeshBuilder.cpp — Test patterns and helpers]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- MSVC C4100 warning for unused `neighbors` parameter in `buildNonCubicPass()` — fixed with `[[maybe_unused]]`
- Test failure: `getIdByName()` returns type index, not base state ID — when multi-state blocks (slab with 2 states) are registered before single-state blocks, the type index diverges from the base state ID. Fixed test helpers to use `registerBlock()` return value directly.

### Code Review Fixes (Claude Opus 4.6)

- **[M1] State ID passthrough**: All ModelMesher generators were using `blockDef.baseStateId` instead of the actual block state ID. Fixed by threading `stateId` (the real `blockId` from `section.getBlock()`) through `ModelRegistry::getModelVertices()` → `ModelMesher::generate*()`. Top slab state variants now report correct `blockStateId` in `ModelVertex`.
- **[M2] Non-cubic face culling**: `buildNonCubicPass()` now computes a per-block `faceMask` by checking each of the 6 face neighbors. Faces occluded by a solid opaque `FullCube` neighbor are excluded. `emitBox()` only emits faces whose bit is set in the mask. Slab-on-stone now correctly culls the slab's bottom face (30 model vertices instead of 36).
- **[L1] static_assert**: Added `static_assert(sizeof(ModelVertex) == 36)` to `ChunkMesh.h`.
- **[L2] Redundant index**: Removed duplicate index computation in `buildCubicOpacityPad()`.

### Completion Notes List

- **ModelVertex struct**: 36 bytes (position vec3, normal vec3, uv vec2, blockStateId u16, ao u8, flags u8). Stored in separate `modelVertices` vector on `ChunkMesh` alongside packed quads. Compile-time size assertion added.
- **isFullFace()**: Implemented as inline method on `BlockDefinition` taking `uint8_t faceIndex` (not `BlockFace`) to avoid world→renderer namespace dependency. Accepts optional `StateMap` for state-dependent models (slab half).
- **ModelMesher**: Static methods generate triangle-list vertices (6 per face). Slab = up to 36 verts (half-height box, face-culled), Cross = 24 verts (2 diagonal quads × front+back), Torch = up to 36 verts (thin box with wall offset, face-culled). `emitBox()` accepts `faceMask` to skip occluded faces.
- **ModelRegistry**: Delegates to ModelMesher for Slab/Cross/Torch, passing `stateId` and `faceMask`. Stubs log VX_LOG_WARN for unimplemented types. Appends directly to output vector (no intermediate allocation).
- **MeshBuilder integration**: Naive mesher: cubic pass skips non-FullCube blocks and uses `isFullFace()` for neighbor face culling; `buildNonCubicPass()` computes per-block face visibility mask and emits model vertices. Greedy mesher: added `buildCubicOpacityPad()` that only marks FullCube opaque blocks, ensuring non-cubic blocks are excluded from face mask generation and greedy merging.
- **Tests**: 132 test cases, 474,395 assertions, all pass. Non-cubic specific: 53 assertions across 3 test cases covering empty section, slab, cross, torch, face culling, ModelVertex fields, isFullFace truth table, ChunkMesh::isEmpty, greedy mesher non-cubic exclusion.

### File List

- `engine/include/voxel/renderer/ChunkMesh.h` — MODIFIED: added ModelVertex struct, extended ChunkMesh with modelVertices/modelVertexCount/isEmpty()
- `engine/include/voxel/world/Block.h` — MODIFIED: added isFullFace() method to BlockDefinition
- `engine/include/voxel/renderer/ModelMesher.h` — CREATED: geometry generators for Slab/Cross/Torch
- `engine/src/renderer/ModelMesher.cpp` — CREATED: ModelMesher implementation
- `engine/include/voxel/renderer/ModelRegistry.h` — CREATED: model lookup and delegation
- `engine/src/renderer/ModelRegistry.cpp` — CREATED: ModelRegistry implementation with stubs
- `engine/include/voxel/renderer/MeshBuilder.h` — MODIFIED: added ModelRegistry member, buildNonCubicPass() method
- `engine/src/renderer/MeshBuilder.cpp` — MODIFIED: cubic pass skips non-cubic blocks, uses isFullFace() for face culling, added buildNonCubicPass(), added buildCubicOpacityPad() for greedy mesher
- `engine/CMakeLists.txt` — MODIFIED: added ModelMesher.cpp and ModelRegistry.cpp sources
- `tests/renderer/TestNonCubicMeshing.cpp` — CREATED: unit tests for all non-cubic meshing functionality
- `tests/CMakeLists.txt` — MODIFIED: added TestNonCubicMeshing.cpp