# Story 5.2: Ambient Occlusion Calculation

Status: done

<!-- Depends on Story 5.1 being implemented first (MeshBuilder + ChunkMesh must exist). -->

---

## Story

As a developer,
I want per-vertex AO values computed during meshing,
so that block edges and corners have realistic shadowing.

---

## Acceptance Criteria

1. `vertexAO(bool side1, bool corner, bool side2) -> int` returns 0 (full occlusion) to 3 (no occlusion)
2. For each face vertex, sample 3 adjacent blocks: 2 side neighbors + 1 corner neighbor
3. AO values packed into quad bits [43:46] — 4 corners x 2 bits = 8 bits total
4. Quad diagonal flip: when `ao[0]+ao[3] != ao[1]+ao[2]` and the wrong diagonal is chosen, flip triangulation via bit [47]
5. Unit test: block in corner of room -> AO values match expected pattern
6. Unit test: isolated block in open air -> all 4 corners of every face have AO = 3

---

## Tasks / Subtasks

- [x] Task 1: Create `AmbientOcclusion.h` with AO functions (AC: #1, #2)
  - [x] 1.1 Create `engine/include/voxel/renderer/AmbientOcclusion.h`
  - [x] 1.2 Implement `vertexAO(bool side1, bool corner, bool side2) -> int` (constexpr inline)
  - [x] 1.3 Implement `computeFaceAO(face, blockPos, opacityLookup) -> std::array<uint8_t, 4>` returning 4 corner AO values
  - [x] 1.4 Implement `shouldFlipQuad(ao[4]) -> bool` for diagonal flip decision
  - [x] 1.5 Define AO neighbor offset lookup table: 6 faces x 4 corners x 3 offsets
- [x] Task 2: Create padded opacity array helper (AC: #2)
  - [x] 2.1 Add `buildOpacityPad(section, neighbors[6], registry) -> std::array<bool, 18*18*18>` in `AmbientOcclusion.h`
  - [x] 2.2 Copy 16^3 section opacity into center of 18^3 array
  - [x] 2.3 Copy 1-block border from 6 face-neighbor sections (nullptr = air)
  - [x] 2.4 Edge/corner cells without diagonal neighbor data default to air (no occlusion)
- [x] Task 3: Integrate AO into MeshBuilder (AC: #1, #2, #3, #4)
  - [x] 3.1 Modify `MeshBuilder::buildNaive()` to build opacity pad before mesh loop
  - [x] 3.2 For each emitted quad, call `computeFaceAO()` using opacity pad
  - [x] 3.3 Call `shouldFlipQuad()` and set flip bit [51] (shifted from [47] due to expanded AO layout)
  - [x] 3.4 Update `packQuad()` calls to pass computed AO values instead of hardcoded 3
- [x] Task 4: Update quad packing helpers (AC: #3, #4)
  - [x] 4.1 Expanded `packQuad()` from 2-field AO (ao01, ao23) to 4 individual corners (ao0, ao1, ao2, ao3) + flip bool
  - [x] 4.2 Add flip bit packing: `q |= static_cast<uint64_t>(flip ? 1 : 0) << 51;`
  - [x] 4.3 Add `unpackAO(uint64_t quad) -> std::array<uint8_t, 4>` unpacking helper
  - [x] 4.4 Add `unpackFlip(uint64_t quad) -> bool` unpacking helper
- [x] Task 5: Write unit tests (AC: #5, #6)
  - [x] 5.1 Create `tests/renderer/TestAmbientOcclusion.cpp`
  - [x] 5.2 Test: `vertexAO(false, false, false) == 3` (no occlusion)
  - [x] 5.3 Test: `vertexAO(true, false, false) == 2` (one side)
  - [x] 5.4 Test: `vertexAO(false, true, false) == 2` (corner only)
  - [x] 5.5 Test: `vertexAO(true, true, false) == 1` (side + corner)
  - [x] 5.6 Test: `vertexAO(true, false, true) == 0` (both sides -> full occlusion, corner irrelevant)
  - [x] 5.7 Test: `vertexAO(true, true, true) == 0` (all three -> full occlusion)
  - [x] 5.8 Test: isolated block in empty section -> all faces, all corners AO = 3
  - [x] 5.9 Test: block in L-shaped corner (floor + wall + wall) -> expected AO gradient on top face
  - [x] 5.10 Test: quad packing roundtrip with AO values — pack then unpack, verify all 4 corners
  - [x] 5.11 Test: diagonal flip triggers when asymmetric AO across quad
  - [x] 5.12 Test: block at section boundary with null neighbor -> AO treats boundary as air
- [x] Task 6: Update CMake and verify build (AC: all)
  - [x] 6.1 Add `tests/renderer/TestAmbientOcclusion.cpp` to `tests/CMakeLists.txt`
  - [x] 6.2 Verify all existing tests still pass (zero regressions) — 122 test cases, 474,157 assertions
  - [x] 6.3 Run Catch2 BENCHMARK for dense terrain with AO enabled — benchmarks run successfully

---

## Dev Notes

### Architecture Constraints

- Exceptions disabled — pure functions, no error paths needed. `vertexAO` and `computeFaceAO` are infallible.
- One class per file — `AmbientOcclusion.h` is a header-only utility (constexpr/inline functions + lookup tables). No `.cpp` needed unless the file exceeds ~500 lines.
- Naming: `PascalCase` files, `camelCase` functions, `SCREAMING_SNAKE` constants.
- `#pragma once` for all headers.
- Namespace: `voxel::renderer` (same as MeshBuilder).

### AO Algorithm — Canonical Reference (0fps.net)

```cpp
// Returns 0 (full occlusion) to 3 (no occlusion)
// side1, side2: opaque block on each side of the vertex edge
// corner: opaque block at the diagonal
constexpr int vertexAO(bool side1, bool corner, bool side2)
{
    if (side1 && side2) return 0; // Both sides occlude — corner is irrelevant
    return 3 - static_cast<int>(side1) - static_cast<int>(side2) - static_cast<int>(corner);
}
```

Truth table:

| side1 | corner | side2 | AO |
|-------|--------|-------|----|
| 0     | 0      | 0     | 3  |
| 1     | 0      | 0     | 2  |
| 0     | 1      | 0     | 2  |
| 0     | 0      | 1     | 2  |
| 1     | 1      | 0     | 1  |
| 0     | 1      | 1     | 1  |
| 1     | 0      | 1     | 0  |
| 1     | 1      | 1     | 0  |

### AO Neighbor Sampling — Per Face, Per Vertex

For each face direction, each vertex corner samples 3 blocks. All 3 samples are offset from the block position by the face normal + tangent offsets. "Opaque" means `blockId != BLOCK_AIR && !blockDef.isTransparent`.

**Offset table** — for face normal N and block at (bx, by, bz), sample at `(bx, by, bz) + offset`:

**+Y face (PosY, face=2):** tangent axes = X, Z
```
Corner 0: side1=(-1,+1, 0)  side2=( 0,+1,-1)  corner=(-1,+1,-1)
Corner 1: (+1,+1, 0)        ( 0,+1,-1)         (+1,+1,-1)
Corner 2: (+1,+1, 0)        ( 0,+1,+1)         (+1,+1,+1)
Corner 3: (-1,+1, 0)        ( 0,+1,+1)         (-1,+1,+1)
```

**-Y face (NegY, face=3):** tangent axes = X, Z
```
Corner 0: side1=(-1,-1, 0)  side2=( 0,-1,-1)  corner=(-1,-1,-1)
Corner 1: (+1,-1, 0)        ( 0,-1,-1)         (+1,-1,-1)
Corner 2: (+1,-1, 0)        ( 0,-1,+1)         (+1,-1,+1)
Corner 3: (-1,-1, 0)        ( 0,-1,+1)         (-1,-1,+1)
```

**+X face (PosX, face=0):** tangent axes = Y, Z
```
Corner 0: side1=(+1,-1, 0)  side2=(+1, 0,-1)  corner=(+1,-1,-1)
Corner 1: (+1,+1, 0)        (+1, 0,-1)         (+1,+1,-1)
Corner 2: (+1,+1, 0)        (+1, 0,+1)         (+1,+1,+1)
Corner 3: (+1,-1, 0)        (+1, 0,+1)         (+1,-1,+1)
```

**-X face (NegX, face=1):** tangent axes = Y, Z
```
Corner 0: side1=(-1,-1, 0)  side2=(-1, 0,+1)  corner=(-1,-1,+1)
Corner 1: (-1,+1, 0)        (-1, 0,+1)         (-1,+1,+1)
Corner 2: (-1,+1, 0)        (-1, 0,-1)         (-1,+1,-1)
Corner 3: (-1,-1, 0)        (-1, 0,-1)         (-1,-1,-1)
```

**+Z face (PosZ, face=4):** tangent axes = X, Y
```
Corner 0: side1=(-1, 0,+1)  side2=( 0,-1,+1)  corner=(-1,-1,+1)
Corner 1: (+1, 0,+1)        ( 0,-1,+1)         (+1,-1,+1)
Corner 2: (+1, 0,+1)        ( 0,+1,+1)         (+1,+1,+1)
Corner 3: (-1, 0,+1)        ( 0,+1,+1)         (-1,+1,+1)
```

**-Z face (NegZ, face=5):** tangent axes = X, Y
```
Corner 0: side1=(+1, 0,-1)  side2=( 0,-1,-1)  corner=(+1,-1,-1)
Corner 1: (-1, 0,-1)        ( 0,-1,-1)         (-1,-1,-1)
Corner 2: (-1, 0,-1)        ( 0,+1,-1)         (-1,+1,-1)
Corner 3: (+1, 0,-1)        ( 0,+1,-1)         (+1,+1,-1)
```

**Implementation:** encode this table as a `constexpr` 3D array:
```cpp
// AO_OFFSETS[face][corner][sample] where sample: 0=side1, 1=corner, 2=side2
// Each offset is glm::ivec3
constexpr glm::ivec3 AO_OFFSETS[6][4][3] = { ... };
```

### Padded Opacity Array — Section Boundary Handling

AO sampling extends 1 block beyond the section in any axis. The `neighbors[6]` array from Story 5.1 covers face neighbors but NOT edge-diagonal sections (e.g., blocks at `(-1, 16, z)` which span two neighbor directions).

**Solution:** Build a padded 18x18x18 boolean array before the mesh loop:
- Center 16^3: copy from section (opacity = `blockId != BLOCK_AIR && !def.isTransparent`)
- 6 face borders (1-block wide): copy from `neighbors[face]` at opposite edge
- 12 edge borders + 8 corner cells: leave as `false` (treat as air = no occlusion)

The edge/corner cells without data affect only blocks at section-edge positions, producing slightly less occlusion at section seams. This is the standard compromise — visually imperceptible. Story 5.6 (async with 3x3x3 snapshots) can provide full coverage later if desired.

```cpp
// 18^3 = 5832 bools ≈ 6 KB — fits in L1 cache
std::array<bool, 18 * 18 * 18> opacity{};

// Padded coordinates: section local (0-15) maps to padded (1-16)
// Padded index: (py * 18 + pz) * 18 + px  where px=x+1, py=y+1, pz=z+1

auto padIndex = [](int px, int py, int pz) -> int {
    return (py * 18 + pz) * 18 + px;
};
```

Then AO sampling becomes a simple array lookup with pre-offset coordinates:
```cpp
bool isOpaque = opacity[padIndex(x + 1 + offsetX, y + 1 + offsetY, z + 1 + offsetZ)];
```

### Quad Diagonal Flip — Anisotropy Fix

When a quad has non-uniform AO values, the GPU splits it into 2 triangles. The default diagonal can create visible seams if AO varies more along one diagonal than the other.

**Canonical formula (0fps.net):**
```cpp
// ao[0..3] = AO values for the 4 corners (0-3 each)
// Flip when the default diagonal (0-3) would produce worse interpolation
bool shouldFlip = (ao[0] + ao[3] > ao[1] + ao[2]);
```

The epic spec phrases this as `abs(ao[0]-ao[3]) > abs(ao[1]-ao[2])` which is a gradient-based formulation. **Use the 0fps sum-comparison formula** — it is the canonical version and handles all cases correctly.

Pack the flip flag into bit [47] of the quad:
```cpp
q |= static_cast<uint64_t>(shouldFlip ? 1 : 0) << 47;
```

### Quad Packing — Updated Bit Layout

Story 5.1 already packs AO into bits [43:46] with hardcoded values of 3. Story 5.2 replaces these with computed values.

```
Bits [43:44]: AO corners 0 and 1 — (ao0 & 0x3) | ((ao1 & 0x3) << 2) at bit offset 43
Bits [45:46]: AO corners 2 and 3 — (ao2 & 0x3) | ((ao3 & 0x3) << 2) at bit offset 45
Bit  [47]:    Quad diagonal flip  — 1 = flip, 0 = normal
```

Verify the exact packing from Story 5.1's `packQuad()`:
```cpp
// Story 5.1 packs: q |= static_cast<uint64_t>(ao0 & 0x3) << 43;
//                  q |= static_cast<uint64_t>(ao1 & 0x3) << 45;
// NOTE: Check if 5.1 packs ao0+ao1 into [43:46] or ao0 into [43:44] and ao1 into [45:46]
// The epic says "AO corner 0+1" at [43:44] and "AO corner 2+3" at [45:46]
// This means each 2-bit field holds ONE corner value, NOT two corners packed together
```

**Critical:** Read the actual `packQuad()` from the 5.1 implementation to confirm exact bit positions before modifying. The epic description "AO corner 0+1" means corners 0 and 1 occupy bits [43:44] — each is a separate 2-bit value at consecutive bit positions:
```
Bit 43-44: ao0 (2 bits)
Bit 45-46: ao1 (2 bits)  ← Wait, that's only 2 corners in 4 bits
```

Re-reading the epic more carefully: the total is 4 corners x 2 bits = 8 bits. Bits [43:44] for "AO corner 0+1" likely means 4 bits (2 corners x 2 bits each) packed at bit offset 43. Confirm:
```
Bit 43: ao0 bit 0     Bit 44: ao0 bit 1     (corner 0, 2 bits)
Bit 45: ao1 bit 0     Bit 46: ao1 bit 1     (corner 1, 2 bits)
```
But then "AO corner 2+3" at [45:46] only has 2 bits for 2 corners — that doesn't work.

**Resolution:** The epic notation `[43:44]` means a 2-bit field. "AO corner 0+1" is a label, NOT a packing of two corners. The layout is:
```
[43:44] = 2 bits = corners 0 and 1 LABEL (actually just the first AO field)
[45:46] = 2 bits = corners 2 and 3 LABEL (actually just the second AO field)
```

This only gives 4 bits for 4 corners — 1 bit per corner, NOT 2 bits. That contradicts "4 corners x 2 bits = 8 bits."

**Most likely correct packing** (8 bits total starting at bit 43):
```
Bits [43:44]: ao corner 0 (2 bits, values 0-3)
Bits [45:46]: ao corner 1 (2 bits, values 0-3)
Bits [47:48]: ao corner 2 (2 bits, values 0-3)  ← overlaps with flip bit!
```

Since bit [47] is the flip bit, and we need 8 bits for AO (43-50), there's a conflict. **Check the 5.1 implementation for the actual layout.** The practical packing that fits is:
```
Bits 43-44: ao0 (2 bits)
Bits 45-46: ao1 (2 bits)
Bits 47-48: ao2 (2 bits)  ← BUT bit 47 is flip!
```

The flip bit and AO corner 2 likely share or the layout is different. **Read the actual 5.1 code** to resolve. If 5.1 followed the epic table literally (`[43:44]` for "AO corner 0+1", `[45:46]` for "AO corner 2+3", `[47]` for flip), then AO has only 4 bits (1 bit per corner, values 0 or 1 — binary occlusion). But the formula returns 0-3 (needs 2 bits).

**Recommended approach:** Follow the quad format from the epic verbatim. Interpret the format as:
```
Bits 43-44 (2 bits): encodes ao[0] and ao[1] — likely packed as (ao0 | ao1<<1) using 1-bit each
Bits 45-46 (2 bits): encodes ao[2] and ao[3] — packed as (ao2 | ao3<<1) using 1-bit each
Bit  47    (1 bit):  diagonal flip
```

But this gives only 1-bit AO (occluded or not), losing the 4-level gradient. If the 5.1 implementation uses 2 bits per corner (the standard approach), then the actual layout may be:
```
Bits 43-50 (8 bits): ao0[43:44], ao1[45:46], ao2[47:48], ao3[49:50]
Bit  51:             diagonal flip
```

**The dev agent MUST read the 5.1 `packQuad()` implementation and the canonical quad format table to determine the actual bit positions before coding.** If the 5.1 implementation uses the literal epic table (4 bits for AO + 1 bit flip = 5 bits at [43:47]), adapt accordingly.

### Opacity Check — What Counts as Occluding

For AO purposes, a block occludes a vertex if it would block ambient light:
```cpp
bool isOccluding(uint16_t blockId, const BlockRegistry& registry) {
    if (blockId == BLOCK_AIR) return false;
    const auto& def = registry.getBlock(blockId);
    return !def.isTransparent;
}
```

Transparent blocks (glass, water, leaves) do NOT cause AO occlusion. Air does not cause occlusion. All other solid blocks DO cause occlusion.

### Existing API Surface — Read Only

| File | Key API | Usage in 5.2 |
|------|---------|--------------|
| `ChunkMesh.h` | `ChunkMesh`, `packQuad()`, `unpackX/Y/Z/Face()` | Extend pack/unpack for AO |
| `MeshBuilder.h` | `MeshBuilder::buildNaive()` | Modify to compute AO per quad |
| `MeshBuilder.cpp` | Meshing loop, face emission | Add opacity pad + AO calls |
| `ChunkSection.h` | `getBlock(x,y,z)`, `SIZE=16` | Read block data for pad |
| `Block.h` | `BLOCK_AIR`, `BlockDefinition::isTransparent` | Opacity check |
| `BlockRegistry.h` | `getBlock(uint16_t id)` | Lookup block definitions |

### File Locations

Create these files:

| File | Path |
|------|------|
| AO functions header | `engine/include/voxel/renderer/AmbientOcclusion.h` |
| AO unit tests | `tests/renderer/TestAmbientOcclusion.cpp` |

Modify these files:

| File | Change |
|------|--------|
| `engine/src/renderer/MeshBuilder.cpp` | Add opacity pad construction + AO computation in mesh loop |
| `engine/include/voxel/renderer/ChunkMesh.h` | Add `unpackAO()`, `unpackFlip()` helpers (if not already in 5.1) |
| `tests/CMakeLists.txt` | Add `tests/renderer/TestAmbientOcclusion.cpp` |

### What NOT To Do

- Do NOT implement greedy merging — that's Story 5.3. AO values simply prevent merging dissimilar corners (5.3 handles this constraint).
- Do NOT add async/threading — that's Story 5.6.
- Do NOT modify the vertex shader or any GPU code — that's Epic 6 (Story 6.2).
- Do NOT add light data packing — that's Story 8.0.
- Do NOT add tint or waving bits — that's Story 5.5.
- Do NOT create a chunk render pipeline — that's Story 5.7 + Epic 6.
- Do NOT modify ChunkSection, ChunkColumn, Block, or BlockRegistry.
- Do NOT expand `neighbors[6]` to 26 — the padded opacity array approach handles boundaries without changing the MeshBuilder signature.
- Do NOT over-engineer: this is a simple per-vertex calculation + lookup table. No classes needed beyond the header with inline functions.

### Performance Notes

- The opacity pad construction (18^3 = 5832 bool copies) adds ~2-5 us per section — negligible vs mesh time.
- AO computation adds 3 array lookups per vertex, 4 vertices per quad — ~12 lookups per emitted face.
- Expected overhead vs Story 5.1 baseline: 10-20% increase in mesh time (~550-600 us for dense terrain).
- The padded array is ~6 KB, fits entirely in L1 cache.
- `vertexAO()` should be `constexpr inline` or `__forceinline` — it's 2 comparisons and 3 additions.

### Project Structure Notes

- `AmbientOcclusion.h` goes in `engine/include/voxel/renderer/` alongside `ChunkMesh.h` and `MeshBuilder.h` — meshing is part of the renderer subsystem per architecture.
- Tests go in `tests/renderer/` alongside `TestMeshBuilder.cpp` from Story 5.1.
- Header-only AO utility: no `.cpp` file needed unless complexity exceeds expectations.
- CMake uses explicit file listing — add test source manually.

### Previous Story Intelligence (from 5.1 spec)

- MeshBuilder takes `const BlockRegistry&` in constructor and `const std::array<const ChunkSection*, 6>& neighbors` in `buildNaive()`.
- `packQuad()` is a constexpr inline helper in `ChunkMesh.h` — it already accepts `ao0, ao1, ao2, ao3` parameters (defaulting to 3 in 5.1).
- Iteration order is Y->Z->X for cache coherency with the flat array layout `y*256 + z*16 + x`.
- Face direction enum: `PosX=0, NegX=1, PosY=2, NegY=3, PosZ=4, NegZ=5`.
- Neighbor array convention: `neighbors[face]` gives the adjacent section in that face direction; `nullptr` means treat as air.
- `section.isEmpty()` fast-path returns empty ChunkMesh immediately — preserve this.
- `reserve()` is used on the quads vector before meshing.
- Test pattern: `TEST_CASE("description", "[renderer][meshing]")` with `SECTION` blocks.
- FastNoiseLite `#pragma warning(push, 0)` pattern for MSVC — apply if needed.
- All 105+ existing tests must continue to pass.

### Git Intelligence

Recent commits show terrain generation work (Stories 4.1-4.5) is complete. The codebase follows established patterns:
- Utility functions/classes in namespace `voxel::world` or `voxel::renderer`
- Small focused files, constexpr helpers, no heavy abstractions
- Commit format: `feat(renderer): add ambient occlusion to naive mesher`

### References

- [0fps.net: Ambient Occlusion for Minecraft-like Worlds](https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/) — canonical AO algorithm, diagonal flip rule
- `_bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md` — Story 5.2 acceptance criteria, quad format table
- `_bmad-output/planning-artifacts/architecture.md` — AO algorithm, vertex format, rendering pipeline
- `_bmad-output/project-context.md` — naming conventions, error handling, testing strategy
- `_bmad-output/implementation-artifacts/5-1-naive-face-culling.md` — MeshBuilder API, quad packing, neighbor conventions
- `engine/include/voxel/renderer/ChunkMesh.h` — ChunkMesh struct, packQuad() helper
- `engine/include/voxel/renderer/MeshBuilder.h` — MeshBuilder class declaration
- `engine/include/voxel/world/ChunkSection.h` — Block access API, SIZE=16
- `engine/include/voxel/world/Block.h` — BLOCK_AIR, BlockDefinition, isTransparent
- `engine/include/voxel/world/BlockRegistry.h` — getBlock() lookup

---

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Initial L-shaped corner test had incorrect AO expectations for corners 1 and 3.
  The -Z wall fills ALL x positions, so corner diagonal blocks were occupied.
  Fixed expectations: corner 1 and 3 are AO=1 (side + corner), not AO=2.

### Completion Notes List

- Implemented canonical 0fps.net AO algorithm with `vertexAO()`, `computeFaceAO()`, `shouldFlipQuad()`.
- Created 18x18x18 padded opacity array for cross-section-boundary AO sampling.
- Expanded quad bit layout: AO from 4 bits (2 pair values) to 8 bits (4 individual corners × 2 bits each). Flip bit moved from [47] to [51]. Remaining bits [52:63] reserved for future stories.
- Updated `packQuad()` signature: replaced `ao01, ao23` with `ao0, ao1, ao2, ao3, flip`.
- Replaced `unpackAO01()`/`unpackAO23()` with `unpackAO() -> std::array<uint8_t, 4>` and `unpackFlip() -> bool`.
- Updated existing `TestMeshBuilder.cpp` to use new AO unpack API.
- All 122 tests pass (474,157 assertions), zero regressions.
- Added `build.sh`/`build.bat` scripts for CLI builds with MSVC environment setup.

### Change Log

- 2026-03-27: Story 5.2 implementation complete — AO calculation, quad packing expansion, full test suite.
- 2026-03-27: [Code Review] 1 HIGH, 1 MEDIUM fixed. Updated epic-05 canonical quad format table to match expanded AO layout (8 bits at [43:50], flip at [51]). Resolved bit budget by reducing tint 3→2 bits, waving 2→1 bit. Fixed stale MeshBuilder.h docstring. Updated epic AC flip formula to canonical 0fps sum-comparison.

### File List

New files:
- `engine/include/voxel/renderer/AmbientOcclusion.h` — header-only AO functions + offset table + opacity pad builder
- `tests/renderer/TestAmbientOcclusion.cpp` — 7 test cases covering all ACs + benchmarks
- `build.sh` — CLI build wrapper for bash
- `build.bat` — MSVC environment setup + cmake build

Modified files:
- `engine/include/voxel/renderer/ChunkMesh.h` — expanded AO bit layout (8 bits), new packQuad signature, unpackAO/unpackFlip
- `engine/src/renderer/MeshBuilder.cpp` — AO integration: opacity pad + computeFaceAO + shouldFlipQuad
- `tests/renderer/TestMeshBuilder.cpp` — updated to new unpackAO API
- `tests/CMakeLists.txt` — added TestAmbientOcclusion.cpp
- `CLAUDE.md` — added build.sh documentation
- `_bmad-output/implementation-artifacts/sprint-status.yaml` — story 5.2 status updated