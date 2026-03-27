# Story 5.4: Non-Cubic Block Model Meshing

Status: ready-for-dev

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

- [ ] **Task 1: Define ModelVertex struct** (AC: #2, #3)
  - [ ] 1.1 Add `ModelVertex` to `ChunkMesh.h` — fields: `glm::vec3 position`, `glm::vec3 normal`, `glm::vec2 uv`, `uint16_t blockStateId`, `uint8_t ao`, `uint8_t padding` (32 bytes, GPU-friendly alignment)
  - [ ] 1.2 Extend `ChunkMesh` with `std::vector<ModelVertex> modelVertices` and `uint32_t modelVertexCount`
  - [ ] 1.3 Add `isEmpty()` method that checks both `quadCount == 0 && modelVertexCount == 0`

- [ ] **Task 2: Add isFullFace() to BlockDefinition** (AC: #5)
  - [ ] 2.1 Add `[[nodiscard]] bool isFullFace(BlockFace face) const` method to `BlockDefinition` in `Block.h`
  - [ ] 2.2 Implementation: `FullCube` → always true; `Slab` → true for top/bottom depending on `half` state; all others → false
  - [ ] 2.3 Unit test: verify `isFullFace()` returns correct values for each ModelType

- [ ] **Task 3: Create ModelMesher** (AC: #4)
  - [ ] 3.1 Create `engine/include/voxel/renderer/ModelMesher.h` — header-only or minimal, in `voxel::renderer` namespace
  - [ ] 3.2 `generateSlab(x, y, z, blockDef, stateValues, ao) -> std::vector<ModelVertex>` — half-height box (0–0.5 or 0.5–1.0 based on `half` property)
  - [ ] 3.3 `generateCross(x, y, z, blockDef) -> std::vector<ModelVertex>` — two diagonal quads from (0,0,0)→(1,1,1) and (1,0,0)→(0,1,1), 4 vertices each = 8 vertices, both sides visible (no backface cull)
  - [ ] 3.4 `generateTorch(x, y, z, blockDef, stateValues) -> std::vector<ModelVertex>` — thin vertical box centered (7/16 to 9/16 XZ, 0 to 10/16 Y), wall variant angled with offset

- [ ] **Task 4: Create ModelRegistry stub** (AC: #7)
  - [ ] 4.1 Create `engine/include/voxel/renderer/ModelRegistry.h` — class in `voxel::renderer` namespace
  - [ ] 4.2 API: `getModelVertices(ModelType, const BlockDefinition&, const StateMap&) -> const std::vector<ModelVertex>&`
  - [ ] 4.3 For `Slab`/`Cross`/`Torch`: delegate to `ModelMesher` generators
  - [ ] 4.4 For `Stair`/`Connected`/`JsonModel`/`MeshModel`/`Custom`: return empty vector (stub, log warning once)
  - [ ] 4.5 Pre-bake common models at registry init time where possible (cross pattern is always the same geometry, only texture differs)

- [ ] **Task 5: Integrate into MeshBuilder** (AC: #1, #6)
  - [ ] 5.1 In `buildNaive()`, after the cubic face-culling loop, add a second pass for non-cubic blocks
  - [ ] 5.2 During the cubic pass: when checking face visibility, use `isFullFace()` instead of just `isSolid` — a non-cubic neighbor that doesn't fully cover the face means the cubic face IS emitted
  - [ ] 5.3 During the non-cubic pass: iterate section, for each non-FullCube block, query `ModelRegistry` for vertices, offset by block position, append to `modelVertices`
  - [ ] 5.4 For non-cubic block faces adjacent to a solid full-cube neighbor: cull the face (the full cube covers it entirely)
  - [ ] 5.5 Set bit 48 (is non-cubic) on any quads related to non-cubic geometry if using the quad format path

- [ ] **Task 6: Unit tests** (AC: #8)
  - [ ] 6.1 Create `tests/renderer/TestNonCubicMeshing.cpp`
  - [ ] 6.2 Test: empty section → 0 quads, 0 model vertices
  - [ ] 6.3 Test: single slab block in empty section → correct ModelVertex count (box = 6 faces × 2 triangles × 3 vertices = 36, or 6 faces × 4 vertices = 24 if indexed)
  - [ ] 6.4 Test: slab on stone → stone top face emitted (slab doesn't fully cover), slab bottom NOT emitted (stone covers)
  - [ ] 6.5 Test: cross block (flower) → 8 model vertices (2 diagonal quads × 4 verts), double-sided
  - [ ] 6.6 Test: torch block → correct thin box geometry
  - [ ] 6.7 Test: ModelVertex roundtrip — construct, verify fields
  - [ ] 6.8 Test: `isFullFace()` truth table for each ModelType
  - [ ] 6.9 Add test source to `tests/CMakeLists.txt`

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

### Debug Log References

### Completion Notes List

### File List