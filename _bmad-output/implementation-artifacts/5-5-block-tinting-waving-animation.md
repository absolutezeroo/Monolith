# Story 5.5: Block Tinting + Waving Animation in Vertex Format

Status: done

## Story

As a developer,
I want per-vertex color tint and waving animation flags stored in mesh data,
so that grass/leaves change color by biome AND leaves/plants/water animate with wind.

## Why Now

Both tinting and waving require bits in the quad format. If added after the vertex format is finalized and shaders are written (Epic 6), every quad needs reformatting and every shader needs updating. The `BlockDefinition` already has `tintIndex` and `waving` fields (Block.h lines 75-76) — this story wires them into the meshing output.

## Acceptance Criteria

1. **AC1 — Quad format extended**: `packQuad()` accepts `tintIndex` (3 bits) and `wavingType` (2 bits), packed into the reserved bits [52:63] of the 64-bit quad
2. **AC2 — Unpack helpers**: `unpackTintIndex(quad)` and `unpackWavingType(quad)` return the stored values
3. **AC3 — Meshing integration**: `MeshBuilder::buildNaive()` reads `BlockDefinition::tintIndex` and `BlockDefinition::waving` during face emission and packs them into each quad
4. **AC4 — TintPalette structure**: CPU-side `TintPalette` class holds a table of up to 8 RGB colors, indexed by tint index
5. **AC5 — Biome color generation**: `TintPalette::buildForBiome(BiomeType)` populates entries with hardcoded V1 colors (8 biome types × 3 tint categories = 24 colors)
6. **AC6 — Roundtrip tests**: Pack tint index + waving type into quad → unpack → verify all values preserved, including all existing fields (no regressions)
7. **AC7 — Meshing tests**: Grass block → tint index 1 packed in quad; flower block → waving type 2 packed; stone block → tint 0, waving 0
8. **AC8 — TintPalette tests**: Build palette for different biome types, verify correct RGB values for grass/foliage/water indices

## Scope Clarification — V1 vs Future

**V1 (this story):**
- Extend `packQuad()` / add unpack helpers for tint and waving bits
- Wire `BlockDefinition::tintIndex` and `::waving` into meshing
- CPU-side `TintPalette` with hardcoded V1 biome colors
- Unit tests for packing roundtrip and meshing integration

**Deferred (future stories):**
- GPU upload of TintPalette as SSBO/UBO (Story 6.8: Block Tinting Shader Support)
- `chunk.frag` shader reads tint index, multiplies albedo by palette color (Story 6.2 / 6.8)
- `chunk.vert` shader reads waving type, applies vertex displacement animation (Story 6.2)
- Data-driven gradient textures loaded from assets (post-MVP)
- Lua API for `tint = "grass"` in block registration (Story 9.2)
- Per-biome TintPalette selection in ChunkManager (future integration story)

## Critical: Quad Format Discrepancy

**The epic spec's bit table is outdated.** The actual code allocates more bits for AO than the epic assumed:

**Epic spec assumed (WRONG for current code):**
```
[43:44]  AO corner 0+1 (2 bits)
[45:46]  AO corner 2+3 (2 bits)
[47]     Flip (1 bit)
[48:]    15 bits remaining
```

**Actual code (ChunkMesh.h, lines 33-37, CORRECT):**
```
[43:44]  AO corner 0 (2 bits, values 0-3)
[45:46]  AO corner 1 (2 bits, values 0-3)
[47:48]  AO corner 2 (2 bits, values 0-3)
[49:50]  AO corner 3 (2 bits, values 0-3)
[51]     Flip (1 bit)
[52:63]  Reserved (12 bits available)
```

The implementation correctly uses 4×2=8 bits for AO as specified in Story 5.2's AC. This leaves **12 reserved bits**, not 16. Story 5.5 claims 5 of those 12.

**New authoritative bit layout after this story:**
```
Bit range   Width   Field                  Set by      Status
─────────   ─────   ─────────────────────  ──────────  ──────
[0:5]       6       X position (0-63)      Story 5.1   Done
[6:11]      6       Y position (0-63)      Story 5.1   Done
[12:17]     6       Z position (0-63)      Story 5.1   Done
[18:23]     6       Width - 1 (0-63)       Story 5.3   Done
[24:29]     6       Height - 1 (0-63)      Story 5.3   Done
[30:45]     16      Block state ID (0-65535) Story 5.1 Done
[46:48]     3       Face direction (0-5)   Story 5.1   Done
[49:50]     2       AO corner 0 (0-3)     Story 5.2   Done
[51:52]     2       AO corner 1 (0-3)     Story 5.2   Done
[53:54]     2       AO corner 2 (0-3)     Story 5.2   Done
[55:56]     2       AO corner 3 (0-3)     Story 5.2   Done
[57]        1       Quad diagonal flip     Story 5.2   Done
[58]        1       Non-cubic flag         Story 5.4   Done
[59:61]     3       Tint index (0-7)       Story 5.5   THIS STORY
[62:63]     2       Waving type (0-3)      Story 5.5   THIS STORY
```

**Known constraint**: Future Story 8.0 needs 8 bits for light data (sky:4 + block:4) but only 7 reserved bits remain. Story 8.0 will need to either reduce light precision to 7 bits total (sky:4 + block:3 or sky:3 + block:4), or restructure the format (e.g., reduce AO from 2-bit to 1-bit per corner, freeing 4 bits). This is Story 8.0's problem — do NOT preemptively solve it here.

## Tasks / Subtasks

- [x] **Task 1: Extend packQuad() with tint and waving parameters** (AC: #1, #2)
  - [x] 1.1 In `ChunkMesh.h`, add `uint8_t tintIndex = 0` and `uint8_t wavingType = 0` parameters to `packQuad()` (after the `flip` parameter)
  - [x] 1.2 Pack `tintIndex & 0x7` into bits [59:61]: `q |= static_cast<uint64_t>(tintIndex & 0x7) << 59;`
  - [x] 1.3 Pack `wavingType & 0x3` into bits [62:63]: `q |= static_cast<uint64_t>(wavingType & 0x3) << 62;`
  - [x] 1.4 Add `inline constexpr uint8_t unpackTintIndex(uint64_t quad)` → `(quad >> 59) & 0x7`
  - [x] 1.5 Add `inline constexpr uint8_t unpackWavingType(uint64_t quad)` → `(quad >> 62) & 0x3`
  - [x] 1.6 Update the bit layout comment block at the top of `ChunkMesh.h` to reflect the new allocation

- [x] **Task 2: Wire tint and waving into MeshBuilder::buildNaive()** (AC: #3)
  - [x] 2.1 blockDef already looked up outside the face loop (existing code)
  - [x] 2.2 Read `blockDef.tintIndex` and `blockDef.waving`
  - [x] 2.3 Pass them to `packQuad()` as the new parameters
  - [x] 2.4 BlockDefinition lookup already hoisted outside the face loop (pre-existing)

- [x] **Task 3: Create TintPalette** (AC: #4, #5)
  - [x] 3.1 Create `engine/include/voxel/renderer/TintPalette.h`
  - [x] 3.2 Define `TintPalette` class in `voxel::renderer` namespace
  - [x] 3.3 Storage: `std::array<glm::vec3, 8> m_colors` — index 0 = white (no tint), indices 1-7 = tint colors
  - [x] 3.4 API: `getColor(uint8_t index) const -> glm::vec3`, `setColor(uint8_t index, glm::vec3 color)`
  - [x] 3.5 Static factory: `static TintPalette buildForBiome(BiomeType biome)` — returns palette with biome-appropriate colors
  - [x] 3.6 V1 hardcoded colors per biome (adapted to actual BiomeType enum: 8 biome types)

- [x] **Task 4: Create TintPalette implementation** (AC: #5)
  - [x] 4.1 Create `engine/src/renderer/TintPalette.cpp`
  - [x] 4.2 Implement `buildForBiome()` with switch on BiomeType — each biome provides grass/foliage/water colors
  - [x] 4.3 Default palette: index 0 = white, index 1 = mid-green (grass), index 2 = dark-green (foliage), index 3 = blue (water), indices 4-7 = white (mod reserved)
  - [x] 4.4 Add `TintPalette.cpp` to `engine/CMakeLists.txt` sources list

- [x] **Task 5: Unit tests — quad packing roundtrip** (AC: #6)
  - [x] 5.1 Create `tests/renderer/TestTintWaving.cpp`
  - [x] 5.2 Test: `packQuad()` with tintIndex=5, wavingType=2 → `unpackTintIndex()` returns 5, `unpackWavingType()` returns 2
  - [x] 5.3 Test: `packQuad()` with tintIndex=0, wavingType=0 → values are 0 (default behavior unchanged)
  - [x] 5.4 Test: `packQuad()` with tintIndex=7 (max), wavingType=3 (max) → roundtrip correct
  - [x] 5.5 Test: All existing unpack functions (X, Y, Z, width, height, blockStateId, face, AO, flip) still return correct values when tint and waving are also set (no bit overlap)
  - [x] 5.6 Test: constexpr pack/unpack (compile-time validation)

- [x] **Task 6: Unit tests — meshing integration** (AC: #7)
  - [x] 6.1 Register test blocks with tint/waving: grass (tintIndex=1, waving=2), oak_leaves (tintIndex=2, waving=1), stone (tintIndex=0, waving=0)
  - [x] 6.2 Test: grass block → all emitted quads have tintIndex=1, wavingType=2
  - [x] 6.3 Test: stone block → all emitted quads have tintIndex=0, wavingType=0
  - [x] 6.4 Test: section with mixed blocks → each quad carries the correct tint/waving for its block type
  - [x] 6.5 Test: existing meshing behavior unchanged — face count, AO values, quad positions all identical to pre-tint tests

- [x] **Task 7: Unit tests — TintPalette** (AC: #8)
  - [x] 7.1 Test: default palette → index 0 = white (1,1,1)
  - [x] 7.2 Test: `buildForBiome(BiomeType::Plains)` → grass index has green tint
  - [x] 7.3 Test: `buildForBiome(BiomeType::Desert)` → grass index has brownish tint
  - [x] 7.4 Test: index 0 always returns white regardless of biome (all 8 biomes tested)

- [x] **Task 8: Build system** (AC: all)
  - [x] 8.1 Add `engine/src/renderer/TintPalette.cpp` to `engine/CMakeLists.txt`
  - [x] 8.2 Add `tests/renderer/TestTintWaving.cpp` to `tests/CMakeLists.txt`
  - [x] 8.3 Verify all existing tests pass (zero regressions — 474,563 assertions in 135 test cases)

## Dev Notes

### Architecture Compliance

- **One class per file**: `TintPalette` in its own header/cpp pair
- **Namespace**: `voxel::renderer` for all new types
- **Error handling**: No exceptions. `getColor()` clamps index to 0-7 range, never fails.
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, SCREAMING_SNAKE constants
- **Max 500 lines per file** — TintPalette is small (~100 lines total), no risk
- **`#pragma once`** for all headers

### Existing Code to Reuse — DO NOT REINVENT

- **`packQuad()` in `ChunkMesh.h`** (line 42): EXTEND this function, do NOT create a new packing function
- **`unpackX/Y/Z/...` helpers** (lines 73-108): Follow exact same pattern for new unpack functions
- **`BlockFace` enum** (ChunkMesh.h line 11): Already defined, do NOT redefine
- **`BlockDefinition::tintIndex` and `::waving`** (Block.h lines 75-76): Already exist with correct types, just read them
- **`BlockRegistry::getBlock(id)`** (already used in `MeshBuilder.cpp` line 68): Use for block property lookup
- **`buildOpacityPad()` / `computeFaceAO()`** (AmbientOcclusion.h): Existing AO pipeline, do NOT modify
- **`BiomeType` enum**: Check if it exists in WorldGenerator.h or similar from Story 4.3

### packQuad() Extension Detail

Current signature (ChunkMesh.h line 42-54):
```cpp
inline constexpr uint64_t packQuad(
    uint8_t x, uint8_t y, uint8_t z,
    uint16_t blockStateId, BlockFace face,
    uint8_t width = 1, uint8_t height = 1,
    uint8_t ao0 = 3, uint8_t ao1 = 3,
    uint8_t ao2 = 3, uint8_t ao3 = 3,
    bool flip = false)
```

New signature — add two defaulted parameters at the end:
```cpp
inline constexpr uint64_t packQuad(
    uint8_t x, uint8_t y, uint8_t z,
    uint16_t blockStateId, BlockFace face,
    uint8_t width = 1, uint8_t height = 1,
    uint8_t ao0 = 3, uint8_t ao1 = 3,
    uint8_t ao2 = 3, uint8_t ao3 = 3,
    bool flip = false,
    uint8_t tintIndex = 0,    // NEW: bits [52:54]
    uint8_t wavingType = 0)   // NEW: bits [55:56]
```

Add two lines to the function body:
```cpp
q |= static_cast<uint64_t>(tintIndex & 0x7) << 52;
q |= static_cast<uint64_t>(wavingType & 0x3) << 55;
```

All existing call sites pass no tint/waving → default 0 → backward compatible.

### MeshBuilder Integration Detail

Current `buildNaive()` (MeshBuilder.cpp lines 45-91) looks up `BlockDefinition` only when `neighborId != BLOCK_AIR` (line 68). For tint/waving, we need the *current* block's definition, not the neighbor's.

Optimization: hoist the block definition lookup outside the face loop. Current code enters the face loop at line 55. Change to:

```cpp
uint16_t blockId = section.getBlock(x, y, z);
if (blockId == world::BLOCK_AIR) continue;

const auto& blockDef = m_registry.getBlock(blockId);  // Hoist here — once per block

for (uint8_t f = 0; f < BLOCK_FACE_COUNT; ++f)
{
    // ... face culling logic unchanged ...
    if (shouldEmit)
    {
        // ... AO computation unchanged ...
        uint64_t quad = packQuad(
            static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(z),
            blockId, face, 1, 1,
            ao[0], ao[1], ao[2], ao[3], flip,
            blockDef.tintIndex, blockDef.waving);  // NEW args
        mesh.quads.push_back(quad);
    }
}
```

### TintPalette Design

```cpp
// TintPalette.h
#pragma once
#include <array>
#include <glm/glm.hpp>
#include <cstdint>

namespace voxel::renderer
{

/// Small color lookup table for biome-dependent block tinting.
/// Index 0 = no tint (white). Indices 1-7 = tint colors.
/// Uploaded to GPU as UBO/SSBO in Story 6.8.
class TintPalette
{
public:
    static constexpr uint8_t MAX_ENTRIES = 8;
    static constexpr uint8_t TINT_NONE = 0;
    static constexpr uint8_t TINT_GRASS = 1;
    static constexpr uint8_t TINT_FOLIAGE = 2;
    static constexpr uint8_t TINT_WATER = 3;

    TintPalette();

    [[nodiscard]] glm::vec3 getColor(uint8_t index) const;
    void setColor(uint8_t index, glm::vec3 color);

    /// Build a palette with biome-appropriate colors.
    /// V1: hardcoded LUT. Future: gradient texture sampling.
    static TintPalette buildForBiome(/* BiomeType biome */);

private:
    std::array<glm::vec3, MAX_ENTRIES> m_colors;
};

} // namespace voxel::renderer
```

The `BiomeType` parameter for `buildForBiome()` depends on what Epic 4 defined. Check `engine/include/voxel/world/WorldGenerator.h` or related files for the enum. If `BiomeType` is not accessible from the renderer namespace, use a forward declaration or accept an integer biome ID.

### Biome Color Table (V1 Hardcoded)

| Biome | Grass (index 1) | Foliage (index 2) | Water (index 3) |
|-------|----------------|-------------------|-----------------|
| Plains | (0.55, 0.76, 0.38) | (0.47, 0.65, 0.33) | (0.24, 0.45, 0.75) |
| Forest | (0.45, 0.68, 0.30) | (0.40, 0.60, 0.28) | (0.22, 0.42, 0.72) |
| Desert | (0.75, 0.72, 0.42) | (0.68, 0.65, 0.38) | (0.28, 0.50, 0.70) |
| Taiga | (0.50, 0.68, 0.45) | (0.42, 0.58, 0.40) | (0.20, 0.40, 0.70) |
| Swamp | (0.40, 0.55, 0.25) | (0.35, 0.50, 0.22) | (0.38, 0.50, 0.45) |
| Jungle | (0.35, 0.80, 0.25) | (0.30, 0.70, 0.20) | (0.18, 0.38, 0.68) |
| Tundra | (0.60, 0.65, 0.50) | (0.55, 0.58, 0.45) | (0.25, 0.48, 0.78) |
| Ocean | (0.55, 0.76, 0.38) | (0.47, 0.65, 0.33) | (0.15, 0.35, 0.80) |

These are approximate — tune during visual testing. Index 0 is always `(1.0, 1.0, 1.0)` (white = no tint). Indices 4-7 are reserved for mods, default to white.

### Waving Type Values

| Value | Name | Visual Effect (applied in Story 6.2 vertex shader) |
|-------|------|-----------------------------------------------------|
| 0 | None | Static — no animation |
| 1 | Leaves | Slow XZ sway, small amplitude |
| 2 | Plants | Faster Y+XZ bob, medium amplitude |
| 3 | Liquid | Wave pattern on surface, large amplitude |

This story only stores the value in the quad. The actual vertex displacement is implemented in the vertex shader (Story 6.2).

### What NOT To Do

- Do NOT modify AO packing (bits 43-51) — those are stable and correct
- Do NOT implement shader logic — tint multiplication and waving displacement are Story 6.2 / 6.8
- Do NOT upload TintPalette to GPU — that's Story 6.8
- Do NOT create per-chunk palette management — that's a future integration story
- Do NOT make TintPalette thread-safe — it's built once per biome, read-only after construction
- Do NOT implement Lua API for tint/waving — that's Story 9.2
- Do NOT try to solve Story 8.0's light data bit shortage — document it and move on
- Do NOT modify `buildGreedy()` if it exists — only modify `buildNaive()`. Greedy will get tint/waving when implemented (Story 5.3) by following the same pattern
- Do NOT change `BlockDefinition` — `tintIndex` and `waving` fields already exist

### File Structure

```
engine/include/voxel/renderer/
  ChunkMesh.h           ← MODIFY: extend packQuad(), add unpackTintIndex/WavingType
  TintPalette.h         ← CREATE: biome tint color lookup table

engine/src/renderer/
  MeshBuilder.cpp       ← MODIFY: wire tintIndex and waving into packQuad() calls
  TintPalette.cpp       ← CREATE: implementation with hardcoded biome colors

engine/CMakeLists.txt   ← MODIFY: add TintPalette.cpp to sources

tests/renderer/
  TestTintWaving.cpp    ← CREATE: roundtrip, meshing, palette tests

tests/CMakeLists.txt    ← MODIFY: add TestTintWaving.cpp
```

### Project Structure Notes

- All new files go in existing directories — no new directories needed
- `TintPalette` needs GLM for `glm::vec3` — GLM is already a dependency of the engine target
- BiomeType may need to be forward-declared or a separate include from `world/` — check what Epic 4 created
- Follow existing CMakeLists.txt patterns: explicit file listing, no GLOB

### Previous Story Intelligence

**From Story 5.1 (done) — buildNaive pattern:**
- Y-Z-X iteration, skip air, check 6 faces, emit via `packQuad()`
- Section `isEmpty()` fast-path returns empty ChunkMesh immediately
- `reserve()` on quads vector before meshing (8192 estimate)
- All `packQuad()` calls default width=1, height=1

**From Story 5.2 (review) — AO integration:**
- `computeFaceAO()` returns `std::array<uint8_t, 4>` — 4 corner AO values
- `shouldFlipQuad()` checks AO anisotropy for diagonal flip
- Opacity pad built once via `buildOpacityPad()`, reused for all faces
- AO corners use bits [43:50], flip uses bit [51] — confirmed in code

**From Story 5.3 (ready-for-dev) — greedy meshing:**
- Will add `buildGreedy()` to MeshBuilder — same output format
- Greedy method will need to also pack tint/waving for each merged quad
- If 5.3 is implemented before 5.5: the dev must also add tint/waving to `buildGreedy()` calls
- If 5.5 is implemented first: `buildGreedy()` doesn't exist yet, no change needed

**From Story 5.4 (ready-for-dev) — non-cubic meshing:**
- Uses separate `ModelVertex` buffer, not the 64-bit quad format
- `ModelVertex` has a `flags` field that can carry tint/waving (see Story 5.4 spec line 128)
- Non-cubic bit in quad format (bit 48 in epic spec) is NOT needed yet per 5.4's own notes
- If 5.4 adds `ModelVertex`, tint/waving for non-cubic blocks should also be set in `ModelVertex::flags`

### Git Intelligence

Recent commits follow pattern:
```
5f247a0 feat(renderer): finalize AO system with expanded quad packing, CLI build scripts, and tests
c541086 feat(renderer): add ambient occlusion calculation for naive mesher
e7ce707 feat(renderer): implement naive face culling mesher with chunk mesh and quad packing
```

Commit convention: `feat(renderer): <description>` for renderer features.

### Testing Standards

- Framework: Catch2 v3
- Pattern: `TEST_CASE("description", "[renderer][meshing]")` with `SECTION` blocks
- Register test blocks via helper functions (see existing `TestMeshBuilder.cpp` pattern)
- Use `REQUIRE()` for assertions
- No GPU/Vulkan in unit tests — test CPU-side logic only
- `constexpr` tests where possible (compile-time validation of pack/unpack)

### References

- [Source: engine/include/voxel/renderer/ChunkMesh.h — Actual quad bit layout, packQuad(), unpack helpers]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition with tintIndex (line 75) and waving (line 76)]
- [Source: engine/src/renderer/MeshBuilder.cpp — buildNaive() implementation, face emission loop]
- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md — Story 5.5 epic spec]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 4 Block Registry, System 5 Vulkan Renderer]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, code organization]
- [Source: _bmad-output/implementation-artifacts/5-4-non-cubic-block-model-meshing.md — ModelVertex flags field]
- [Source: _bmad-output/implementation-artifacts/5-3-binary-greedy-meshing-implementation.md — buildGreedy() context]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Story spec assumed bits [52:54] for tint and [55:56] for waving (based on 10-bit blockStateId). Actual code uses 16-bit blockStateId, placing AO at [49:56], flip at [57], non-cubic at [58]. Adapted to bits [59:61] for tint (3 bits) and [62:63] for waving (2 bits). All 5 bits used, 0 reserved remaining.
- Story biome table included Ocean/Swamp. Actual BiomeType enum has Savanna/IcePlains instead. Adapted colors for the real enum values.
- blockDef was already hoisted outside the face loop in buildNaive() (done in Story 5.4), so subtasks 2.1/2.4 were pre-satisfied.

### Completion Notes List

- Extended packQuad() with tintIndex (3 bits) and wavingType (2 bits) at bits [59:63]
- Added unpackTintIndex() and unpackWavingType() constexpr helpers
- Wired blockDef.tintIndex and blockDef.waving into buildNaive() packQuad() calls
- Created TintPalette class with buildForBiome() factory for all 8 BiomeType values
- 168 assertions in 3 new test cases (roundtrip, meshing integration, TintPalette)
- Full regression suite: 474,563 assertions in 135 test cases — all pass
- Non-cubic ModelVertex flags field not wired (out of scope, existing comment documents intent)

### Code Review Fixes Applied
- Wired tint/waving into greedy mesher: `greedyMergeFace()` now accepts `BlockRegistry` and passes `blockDef.tintIndex`/`blockDef.waving` to `packQuad()` — both opaque and transparent passes
- Added greedy mesher tint/waving test case (3 sections: grass, stone, mixed blocks)
- Fixed story bit layout table: corrected blockStateId to 16-bit [30:45], updated all downstream bit offsets to match actual code
- Full regression suite: 474,619 assertions in 136 test cases — all pass

### File List

- `engine/include/voxel/renderer/ChunkMesh.h` — MODIFIED: extended packQuad() params, updated bit layout comment, added unpackTintIndex/unpackWavingType
- `engine/src/renderer/MeshBuilder.cpp` — MODIFIED: pass blockDef.tintIndex/waving to packQuad() in buildNaive()
- `engine/include/voxel/renderer/TintPalette.h` — NEW: TintPalette class header
- `engine/src/renderer/TintPalette.cpp` — NEW: TintPalette implementation with hardcoded biome colors
- `engine/CMakeLists.txt` — MODIFIED: added TintPalette.cpp to sources
- `tests/renderer/TestTintWaving.cpp` — NEW: roundtrip, meshing integration, and TintPalette tests
- `tests/CMakeLists.txt` — MODIFIED: added TestTintWaving.cpp

### Change Log

- 2026-03-27: Story 5.5 implemented — tint index (3 bits) + waving type (2 bits) packed into quad format, TintPalette CPU-side biome color table, full test coverage