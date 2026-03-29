# Story 8.0: Wire Light Data into Meshing Pipeline

Status: review

## Story

As a developer,
I want the mesher to read light values and bake them into vertex data,
so that Stories 8.1–8.4 can propagate light and see results rendered.

## Acceptance Criteria

1. **LightMap class** exists at `engine/include/voxel/world/LightMap.h` with per-block storage: `uint8_t[4096]` packed `[sky:4 | block:4]`, accessors `getSkyLight(x,y,z)`, `getBlockLight(x,y,z)`, `setSkyLight(x,y,z,val)`, `setBlockLight(x,y,z,val)`, and an `isClear()` helper that returns true if all values are zero.
2. **MeshBuilder** signature changes — `buildGreedy()` and `buildNaive()` accept an optional `const LightMap*` (nullable) plus `std::array<const LightMap*, 6>` for neighbor light maps. When `LightMap*` is null, the mesher writes default light values (sky=15, block=0 per corner) representing full brightness.
3. **Per-quad light data** is stored in `ChunkMesh::quadLightData` (parallel `std::vector<uint32_t>` alongside `quads`). Each `uint32_t` packs 4 corner light values: `[corner0:8 | corner1:8 | corner2:8 | corner3:8]` where each corner byte is `[sky:4 | block:4]`. A matching `translucentQuadLightData` vector parallels `translucentQuads`.
4. **Light averaging** per vertex corner follows the same 4-neighbor sampling pattern as AO: for each corner of a quad, average the sky and block light values from the 4 adjacent blocks (the face block + 2 edge-adjacent + 1 corner-diagonal), clamping to 0–15 per channel. When all LightMap pointers are null, skip averaging and write default (0xF0 = sky 15, block 0) for every corner.
5. **MeshJobInput** extended with `std::array<uint8_t, 4096> lightData` plus `std::array<std::array<uint8_t, 4096>, 6> neighborLightData` and `std::array<bool, 6> hasNeighborLight`. Before 8.1 is implemented, snapshot copies zero-filled light data (no overhead beyond the copy).
6. **ModelVertex** extended with `uint8_t light = 0xF0` field (sky=15, block=0). `sizeof(ModelVertex)` updates from 36 to 40 bytes. Static assert and GPU upload code updated accordingly.
7. **GPU upload** — Light data is uploaded to the Gigabuffer appended after quad data in the same allocation. Allocation size = `quadCount * 8 + quadCount * 4` bytes for opaque (and same pattern for translucent). The shader computes the light data offset from `quadCount * 2` (in uint32 units) relative to the chunk's base.
8. **chunk.vert** reads the parallel light data from the Gigabuffer at computed offset, unpacks per-corner light, selects by `cornerIndex`, and outputs `fragSkyLight` and `fragBlockLight` as interpolated floats (0.0–1.0, mapped from 0–15).
9. **gbuffer.frag** receives `fragSkyLight` and `fragBlockLight` varyings. For now, these are unused — G-Buffer output is unchanged (albedo+AO in RT0, normal in RT1). Light values will be consumed when Story 8.4 extends the G-Buffer.
10. **lighting.frag** is unchanged. Current sun+ambient lighting continues as-is.
11. **ZERO visual change** — Rendering output is pixel-identical before and after this story. The light pipeline is plumbed and ready but produces no visible effect until Stories 8.1–8.4 fill in real data.
12. **Unit tests** — (a) MeshBuilder with null LightMap produces identical quad output to current code. (b) MeshBuilder with a test LightMap containing non-zero values produces quads with corresponding non-zero light data in `quadLightData`. (c) LightMap accessors: set/get round-trip for sky and block channels. (d) Light averaging: single bright block surrounded by dark → correct falloff at corners.

## Tasks / Subtasks

- [ ] Task 1: Create LightMap class (AC: #1)
  - [ ] 1.1 — Create `engine/include/voxel/world/LightMap.h` with `uint8_t m_data[4096]` storage
  - [ ] 1.2 — Implement accessors: `getSkyLight`, `getBlockLight`, `setSkyLight`, `setBlockLight`, `isClear`, `clear`, `getRaw`, `setRaw`
  - [ ] 1.3 — Use Y-major indexing: `index = y*256 + z*16 + x` (matching ChunkSection)
  - [ ] 1.4 — Header-only or thin .cpp — keep it simple since it's all inline bit manipulation

- [ ] Task 2: Extend ChunkMesh with parallel light data (AC: #3)
  - [ ] 2.1 — Add `std::vector<uint32_t> quadLightData` to ChunkMesh (parallel to `quads`)
  - [ ] 2.2 — Add `std::vector<uint32_t> translucentQuadLightData` (parallel to `translucentQuads`)
  - [ ] 2.3 — Add light pack/unpack helpers: `packCornerLight(sky0,blk0, sky1,blk1, sky2,blk2, sky3,blk3) → uint32_t`
  - [ ] 2.4 — Ensure `isEmpty()` still works (light vectors don't affect emptiness check)

- [ ] Task 3: Extend MeshBuilder interface (AC: #2, #4)
  - [ ] 3.1 — Add optional `const LightMap*` and `std::array<const LightMap*, 6>` parameters to `buildGreedy()` and `buildNaive()` — default both to null
  - [ ] 3.2 — When LightMap is null, emit default light: `packCornerLight(15,0, 15,0, 15,0, 15,0)` per quad
  - [ ] 3.3 — When LightMap is non-null, average 4 adjacent light values per corner using the same neighbor-sampling offsets as AO (reuse `AO_OFFSETS[face][corner]`)
  - [ ] 3.4 — Push one uint32 to `quadLightData` for each quad pushed to `quads` (maintain parallel invariant)
  - [ ] 3.5 — Same for translucent quads

- [ ] Task 4: Extend ModelVertex (AC: #6)
  - [ ] 4.1 — Add `uint8_t light = 0xF0;` field to ModelVertex (after `flags`)
  - [ ] 4.2 — Update `static_assert(sizeof(ModelVertex) == 40, ...)`
  - [ ] 4.3 — Update all GPU upload code referencing ModelVertex size/stride
  - [ ] 4.4 — In `buildNonCubicPass()`, set `light` from LightMap if available, else 0xF0

- [ ] Task 5: Extend MeshJobInput for light snapshots (AC: #5)
  - [ ] 5.1 — Add light data arrays to MeshJobInput (or a LightMap copy per section + 6 neighbors)
  - [ ] 5.2 — Add `hasNeighborLight` flags
  - [ ] 5.3 — MeshChunkTask::ExecuteRange passes light data to buildGreedy()
  - [ ] 5.4 — Snapshot code in chunk pipeline copies light data (currently all zeros = default)

- [ ] Task 6: GPU upload — append light data in Gigabuffer (AC: #7)
  - [ ] 6.1 — Modify allocation size: `quadCount * 12` bytes instead of `quadCount * 8`
  - [ ] 6.2 — Upload layout: `[quad0_lo, quad0_hi, quad1_lo, quad1_hi, ... | light0, light1, ...]`
  - [ ] 6.3 — Same for translucent: `transQuadCount * 12` bytes total
  - [ ] 6.4 — No changes to ChunkRenderInfo/GpuChunkRenderInfo — shader computes light offset from quadCount

- [ ] Task 7: Shader changes (AC: #8, #9, #10)
  - [ ] 7.1 — chunk.vert: Read light uint32 at `gigabuffer.data[quadCount * 2 + quadIndex]` using quadCount from ChunkRenderInfo
  - [ ] 7.2 — chunk.vert: Unpack per-corner light, select by cornerIndex, output `fragSkyLight` and `fragBlockLight` as floats
  - [ ] 7.3 — gbuffer.frag: Accept fragSkyLight/fragBlockLight as inputs but do not use (no RT change)
  - [ ] 7.4 — chunk.frag (forward): Accept fragSkyLight/fragBlockLight but do not modify output
  - [ ] 7.5 — lighting.frag: No changes
  - [ ] 7.6 — Recompile all SPIR-V shaders

- [ ] Task 8: Unit tests (AC: #12)
  - [ ] 8.1 — TestLightMap.cpp: set/get round-trip, boundary values, isClear
  - [ ] 8.2 — TestMeshing.cpp (extend): null LightMap → identical quads as before
  - [ ] 8.3 — TestMeshing.cpp (extend): non-null LightMap → quadLightData populated with correct values
  - [ ] 8.4 — TestMeshing.cpp (extend): light averaging matches AO sampling pattern

- [ ] Task 9: CMakeLists updates (AC: implicit)
  - [ ] 9.1 — Add LightMap source (if .cpp needed) to `engine/CMakeLists.txt`
  - [ ] 9.2 — Add TestLightMap.cpp to `tests/CMakeLists.txt`

## Dev Notes

### CRITICAL: Quad Format Bit Layout Discrepancy

The architecture doc and epic state that bits 49–63 of the packed quad format are "reserved" for lighting. **This is WRONG.** ALL 64 bits are currently occupied:

```
Bits [0:5]   X position (6 bits)
Bits [6:11]  Y position (6 bits)
Bits [12:17] Z position (6 bits)
Bits [18:23] Width - 1  (6 bits)
Bits [24:29] Height - 1 (6 bits)
Bits [30:45] Texture layer index (16 bits)
Bits [46:48] Face direction (3 bits)
Bits [49:50] AO corner 0 (2 bits)
Bits [51:52] AO corner 1 (2 bits)
Bits [53:54] AO corner 2 (2 bits)
Bits [55:56] AO corner 3 (2 bits)
Bits [57]    Quad diagonal flip (1 bit)
Bits [58]    Non-cubic model flag (1 bit)
Bits [59:61] Tint index (3 bits)
Bits [62:63] Waving type (2 bits)
= 64 bits TOTAL (zero free)
```

**See:** `engine/include/voxel/renderer/ChunkMesh.h:25-42` — the packed quad format definition.

### Solution: Parallel Light Buffer

Instead of embedding light in the quad, use a **parallel `uint32_t` vector** alongside the quad vector. Each uint32 packs 4 corners of light data for one quad:

```cpp
// In ChunkMesh:
std::vector<uint64_t> quads;              // existing
std::vector<uint32_t> quadLightData;      // NEW — parallel, same length as quads
```

**GPU memory layout in Gigabuffer:**
```
[quad0_lo][quad0_hi][quad1_lo][quad1_hi]...[quadN_hi] | [light0][light1]...[lightN]
 ^— 8 bytes per quad —^                               ^— 4 bytes per quad —^
```

**Shader reads light at computed offset:**
```glsl
// chunk.vert — read light for this quad
uint lightOffset = chunkInfo.infos[gl_InstanceIndex].quadCount * 2u;
uint lightPacked = gigabuffer.data[lightOffset + quadIndex];
uint cornerLight = bitfieldExtract(lightPacked, int(cornerIndex) * 8, 8);
float fragSkyLight = float(cornerLight >> 4) / 15.0;
float fragBlockLight = float(cornerLight & 0xF) / 15.0;
```

**Why not alternative approaches:**
- Expanding to 128-bit quads would double GPU bandwidth for ALL rendering — unacceptable.
- Reducing texture bits (16→10) breaks existing pack/unpack and limits future expansion.
- Per-section 3D lightmap textures add texture management complexity and don't match the architecture's "bake into vertices" design.
- The parallel buffer has ZERO impact on existing quad format, minimal memory overhead (+50%), and clean shader access.

### Light Averaging (Same Pattern as AO)

The light averaging per vertex corner follows the EXACT same sampling pattern as ambient occlusion in `AO_OFFSETS[6][4][3][3]` from `engine/include/voxel/renderer/AmbientOcclusion.h`. For each corner of each face:
1. Sample the 3 neighbors: side1, side2, corner-diagonal (from AO_OFFSETS)
2. Plus the face block itself (the block just outside the face)
3. Average sky values: `(sky_face + sky_side1 + sky_side2 + sky_corner) / 4`
4. Average block values: `(blk_face + blk_side1 + blk_side2 + blk_corner) / 4`
5. Pack: `(avgSky << 4) | avgBlock`

When a sampled position is opaque (solid block), its light contribution is 0 for both channels.
When a position is outside the snapshot bounds (no neighbor), treat as sky=15, block=0 (open air).

### MeshJobInput Light Snapshot Strategy

The current `MeshJobInput` copies `ChunkSection` (8KB each × 7 = 56KB). Adding light data adds 4KB per LightMap × 7 = 28KB. Total per job: ~84KB. This is acceptable for enkiTS worker threads.

However, **before Story 8.1**, no LightMap data exists. To avoid waste:
- Add a `bool hasLightData = false` flag to MeshJobInput
- When false, MeshBuilder skips light averaging entirely and writes default values
- This means zero performance regression until lighting is actually implemented

### Existing Infrastructure to Reuse

| Component | File | What to Reuse |
|-----------|------|---------------|
| AO sampling offsets | `engine/include/voxel/renderer/AmbientOcclusion.h` | `AO_OFFSETS[6][4][3][3]` — same neighbor pattern for light |
| Padded block array | `engine/src/renderer/MeshBuilder.cpp` | The 18³ `blockPad` array already handles neighbor access for AO; light sampling can use the same padded approach |
| Gigabuffer upload | `engine/src/renderer/ChunkUploadManager.*` | Extend allocation size and upload to include light data |
| GpuChunkRenderInfo | `engine/include/voxel/renderer/ChunkRenderInfo.h` | `quadCount` field already available in SSBO — shader uses it to compute light offset |

### Files that Exist with Light Properties (Already Defined, NOT YET Used)

- `engine/include/voxel/world/Block.h:68-69` — `lightEmission` (0–15) and `lightFilter` (0–15) fields in BlockDefinition
- `engine/src/world/BlockRegistry.cpp:250-251` — JSON loading for these fields
- `assets/scripts/base/blocks.json` — Light values for all blocks (glowstone=15 emission, water=2 filter, glass=0 filter, stone=15 filter)

These are ALREADY loaded and available via `BlockRegistry::getBlockType(stateId).lightEmission` etc. Do NOT recreate them.

### Shader Binding Layout (Current State)

```
chunk.vert / gbuffer.frag / chunk.frag:
  set=0, binding=0: Gigabuffer SSBO (readonly buffer, uint data[])
  set=0, binding=1: ChunkRenderInfo SSBO (readonly buffer, ChunkRenderInfo infos[])
  set=0, binding=4: Block texture array (sampler2DArray)
  set=0, binding=5: Tint palette SSBO (readonly buffer, vec4 colors[8])

lighting.frag:
  set=0, binding=0: G-Buffer albedo+AO (sampler2D)
  set=0, binding=1: G-Buffer normal (sampler2D)
  set=0, binding=2: G-Buffer depth (sampler2D)
```

No new SSBO bindings are needed — light data lives in the Gigabuffer alongside quads.

### G-Buffer — No Changes This Story

Current G-Buffer has 2 RTs:
- RT0: RGBA8_SRGB (albedo.rgb + AO.a)
- RT1: RG16_SFLOAT (normal.xy octahedral)

Story 8.4 will add RT2 for light data. This story just passes light as varyings through chunk.vert/gbuffer.frag without storing them in the G-Buffer.

### Vertex Shader Output Locations

Current outputs use locations 0–5. Add:
- `layout(location = 6) out float fragSkyLight;`
- `layout(location = 7) out float fragBlockLight;`

Ensure all fragment shaders that pair with chunk.vert declare these inputs (even if unused).

### Testing Strategy

**Regression safety:** Run the existing meshing test suite. With null LightMaps, quad output MUST be byte-identical to the current implementation. The only difference is the new `quadLightData` vector being populated with default values.

**New tests:**
1. `TestLightMap.cpp` — Pure unit tests for the data structure (set/get, boundary, clear)
2. Extend `tests/world/TestMeshing.cpp` — Test that:
   - `quadLightData.size() == quads.size()` always
   - Null LightMap → all light values = 0xF0 (sky=15, block=0)
   - Non-null LightMap with a bright block → correct averaged corners
   - Light doesn't affect quad merging (same block+AO+light-differs → still merges, light from first block in merged region)

**Build:** `bash build.sh VoxelTests && ./build/msvc-debug/tests/VoxelTests`

### Project Structure Notes

- `LightMap.h` goes in `engine/include/voxel/world/` alongside ChunkSection.h (same subsystem)
- `TestLightMap.cpp` goes in `tests/world/` alongside TestChunk.cpp
- No new directories or modules needed
- Architecture lists `LightMap.h` in `world/` already — this matches the intended structure

### Previous Story Learnings (from 7.4 and 7.5)

- **Pattern:** All new headers use `#pragma once`, namespace `voxel::world` for world types
- **Testing:** Catch2 v3 with `TEST_CASE` + `SECTION` pattern. Existing test suite: 212 cases, 489K+ assertions
- **Code review findings (7.4):** Defensive bounds checks on arrays, edge-case comments appreciated
- **Build:** Full regression before declaring done. Zero build warnings expected
- **Convention:** New classes use RAII, camelCase methods, `m_` prefix members, PascalCase files

### Git Intelligence

Recent commit pattern: `feat(scope): description` — e.g., `feat(physics): implement DDA raycasting...`
Expected commit for this story: `feat(world): add LightMap and wire light data into meshing pipeline`

### References

- [Source: architecture.md#Lighting System Architecture] — Dual 4-bit light channels, BFS propagation design
- [Source: architecture.md#Meshing Pipeline Architecture] — Binary greedy meshing, 8-byte quad format
- [Source: architecture.md#Async Chunk Pipeline] — enkiTS job stages, snapshot pattern
- [Source: epics/epic-8.md#Story 8.0] — Original acceptance criteria and interface change spec
- [Source: engine/include/voxel/renderer/ChunkMesh.h] — Packed quad format (ALL 64 bits used)
- [Source: engine/include/voxel/renderer/AmbientOcclusion.h] — AO_OFFSETS neighbor sampling pattern
- [Source: engine/include/voxel/renderer/MeshBuilder.h] — Current buildGreedy/buildNaive signatures
- [Source: engine/include/voxel/renderer/MeshJobTypes.h] — MeshJobInput/MeshChunkTask snapshot pattern
- [Source: engine/include/voxel/world/Block.h:68-69] — lightEmission/lightFilter already defined
- [Source: engine/include/voxel/renderer/ChunkRenderInfo.h] — GpuChunkRenderInfo std430 layout
- [Source: assets/shaders/chunk.vert] — Current vertex pulling + output layout
- [Source: assets/shaders/gbuffer.frag] — Current G-Buffer MRT outputs
- [Source: assets/shaders/lighting.frag] — Current deferred lighting (sun+ambient only)

---

## Senior Developer Review (AI)

### Implementation Summary

All 9 tasks completed. Story 8.0 wires light data through the full meshing pipeline — from CPU-side LightMap storage through the async meshing job system, GPU upload, and vertex shader output — without changing any rendering output.

### Files Created
| File | Purpose |
|------|---------|
| `engine/include/voxel/world/LightMap.h` | Header-only LightMap class: 4096-byte packed [sky:4\|block:4] per block |
| `tests/world/TestLightMap.cpp` | Unit tests for LightMap (set/get, clear, boundary, raw access) |
| `tests/renderer/TestMeshingLight.cpp` | Light data tests for naive and greedy meshing paths |

### Files Modified
| File | Changes |
|------|---------|
| `engine/include/voxel/renderer/ChunkMesh.h` | Added `quadLightData`, `translucentQuadLightData`, `packCornerLight`, `unpackCornerLightByte`, `DEFAULT_CORNER_LIGHT`. Extended `ModelVertex` with `light` field (36→40 bytes). |
| `engine/include/voxel/renderer/MeshBuilder.h` | Added optional `LightMap*` + neighbor array params to `buildGreedy`/`buildNaive`. Updated `buildNonCubicPass` to accept `LightMap*`. |
| `engine/src/renderer/MeshBuilder.cpp` | Added `buildLightPad`, `computeFaceLight`, `FACE_NORMAL_OFFSETS`. Extended `MeshWorkspace` with lightPad. All mesh paths emit parallel light data. |
| `engine/include/voxel/renderer/MeshJobTypes.h` | Added `LightMap` data/flags to `MeshJobInput`. `MeshChunkTask` passes light to `buildGreedy`. |
| `engine/src/renderer/ChunkUploadManager.cpp` | Allocation size: `quadCount * 12` (8 quad + 4 light). Two-stage upload per allocation. |
| `assets/shaders/chunk.vert` | Reads light from gigabuffer at `baseU32 + quadCount*2 + localQuad`. Outputs `fragSkyLight`/`fragBlockLight`. Handles opaque/translucent detection via range check. |
| `assets/shaders/gbuffer.frag` | Accepts `fragSkyLight`/`fragBlockLight` inputs (unused). |
| `assets/shaders/chunk.frag` | Accepts `fragSkyLight`/`fragBlockLight` inputs (unused). |
| `assets/shaders/translucent.frag` | Accepts `fragSkyLight`/`fragBlockLight` inputs (unused). |
| `tests/CMakeLists.txt` | Added `TestLightMap.cpp` and `TestMeshingLight.cpp`. |

### Design Decisions
1. **Parallel buffer over embedded bits**: All 64 quad bits are occupied. Parallel `uint32_t` vector adds 50% memory but avoids format changes.
2. **Light averaging reuses AO_OFFSETS**: Same 4-neighbor corner sampling pattern (face block + 2 edges + 1 diagonal).
3. **Opaque/translucent detection in vertex shader**: Range-checked `quadIndex*2` against `gigabufferOffset/4` to distinguish draw paths — avoids needing `gl_BaseVertex` extension.
4. **Two-stage GPU upload**: Quads and light uploaded as separate staging operations to avoid a temporary buffer copy.
5. **Default light = sky 15, block 0**: When no LightMap is provided, all corners get full sky brightness — matches pre-lighting visual output.

### Test Results
- **Light tests**: 10 test cases, 63 assertions — all pass
- **Meshing tests**: 25 test cases, 634 assertions — all pass (zero regression)
- **Full suite**: 236 test cases, 489,737 assertions — all pass

### Known Limitations
- Greedy-merged quads use light from the origin block's corners (not per-corner-block position). Acceptable since light data is all-default until Story 8.1.
- SPIR-V shaders need recompilation before running the game (Task 7.6 not automated).
- `MeshJobInput` size grows by ~28KB per job when `hasLightData=true` (currently always false).

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
