# Story 5.3: Binary Greedy Meshing Implementation

Status: ready-for-dev

<!-- Dependencies: Story 5.1 (MeshBuilder + ChunkMesh) MUST be complete. Story 5.2 (AO) SHOULD be complete. -->

---

## Story

As a developer,
I want the binary greedy meshing algorithm,
so that meshing runs at ~74us/chunk with maximum face merging.

---

## Acceptance Criteria

1. `MeshBuilder::buildGreedy(const ChunkSection&, neighbors[6]) -> ChunkMesh` produces correct, merged mesh data
2. Uses bitmasks per slice: bit=1 where face is visible, XOR with neighbor slice for face detection
3. Greedy merging via bitwise operations (trailing zeros, row scanning) merges coplanar adjacent faces of same block type into larger quads
4. Quad format: 8 bytes total -- position + merged width/height + block type + face + AO packed (via `packQuad()`)
5. Performance: < 200us/chunk for typical terrain (benchmark via Catch2 BENCHMARK)
6. Unit test: flat ground plane merges into minimal quads
7. Unit test: sphere produces correct face count +/-5%
8. Reference: cgerikj/binary-greedy-meshing algorithm adapted for 16^3 sections

---

## Tasks / Subtasks

- [ ] Task 1: Add `buildGreedy()` to `MeshBuilder` (AC: #1)
  - [ ] 1.1 Add `buildGreedy()` declaration to `engine/include/voxel/renderer/MeshBuilder.h`
  - [ ] 1.2 Add private helpers: `buildFaceMasks()`, `greedyMergeSlice()`
  - [ ] 1.3 Add `MeshWorkspace` struct for pre-allocated per-call buffers (face masks, block type cache)
- [ ] Task 2: Build padded block data (AC: #2)
  - [ ] 2.1 Construct 18x18x18 `uint16_t` padded block ID array from section + neighbors
  - [ ] 2.2 Center 16^3 from section, 6 face borders from neighbors (nullptr = BLOCK_AIR)
  - [ ] 2.3 Edge/corner cells default to BLOCK_AIR
- [ ] Task 3: Face mask generation via bitmask operations (AC: #2)
  - [ ] 3.1 For each of 6 face directions, build 16 slices of 16x16 face visibility masks
  - [ ] 3.2 Each row within a slice = `uint16_t` bitmask; bit=1 if that position has a visible face
  - [ ] 3.3 Face detection: `solid[here] & ~solid[neighbor]` per column/row using bitwise AND/NOT
  - [ ] 3.4 Total: 6 faces x 16 slices x 16 rows = 1536 `uint16_t` masks
- [ ] Task 4: Greedy merge within each slice (AC: #3)
  - [ ] 4.1 For each face direction, for each slice, process rows bottom-to-top
  - [ ] 4.2 Use `std::countr_zero()` (C++20 `<bit>`) to find first set bit in row
  - [ ] 4.3 Extend width: scan consecutive bits with same block type
  - [ ] 4.4 Extend height: check subsequent rows for identical pattern (same columns, same type)
  - [ ] 4.5 Clear consumed bits from all merged rows
  - [ ] 4.6 Emit merged quad via `packQuad()` with computed width/height
- [ ] Task 5: Integrate AO with merged quads (AC: #4)
  - [ ] 5.1 After merging, compute AO at the 4 physical corners of each merged quad
  - [ ] 5.2 Use the opacity pad and `computeFaceAO()` from Story 5.2's `AmbientOcclusion.h`
  - [ ] 5.3 Apply diagonal flip via `shouldFlipQuad()` from Story 5.2
  - [ ] 5.4 If Story 5.2 not yet implemented: set AO=3 and flip=0 as fallback (matches 5.1 behavior)
- [ ] Task 6: Write unit tests (AC: #6, #7)
  - [ ] 6.1 Create `tests/renderer/TestGreedyMeshing.cpp`
  - [ ] 6.2 Test: empty section -> 0 quads
  - [ ] 6.3 Test: single block in empty section -> 6 quads, same as naive
  - [ ] 6.4 Test: flat ground plane (16x16 solid at y=0) -> merges into 5 large quads (top=1, sides=4, bottom=1 => total 6, but top is one 16x16 quad, bottom is one 16x16 quad, 4 sides are each one 16x1 quad)
  - [ ] 6.5 Test: two adjacent blocks of same type -> 6 quads (all faces merge where possible)
  - [ ] 6.6 Test: two adjacent blocks of DIFFERENT types -> 10 quads (no merging across types)
  - [ ] 6.7 Test: 2x2x2 cube -> 6 quads (one per face, each 2x2)
  - [ ] 6.8 Test: checkerboard pattern -> no merging possible (each face isolated)
  - [ ] 6.9 Test: sphere shape -> face count within +/-5% of expected mathematical value
  - [ ] 6.10 Test: section boundary with solid neighbor -> boundary faces culled correctly
  - [ ] 6.11 Test: greedy mesh output matches naive mesh visually (same total visible area)
  - [ ] 6.12 Test: quad packing roundtrip with width > 1 and height > 1
- [ ] Task 7: Performance benchmarks (AC: #5)
  - [ ] 7.1 BENCHMARK: dense terrain (all solid) -> measure time, compare with naive baseline
  - [ ] 7.2 BENCHMARK: typical terrain (half-filled + scattered) -> target < 200us
  - [ ] 7.3 BENCHMARK: flat ground plane -> target < 50us (highly mergeable)
  - [ ] 7.4 Log speedup factor vs naive for same test cases
- [ ] Task 8: Build system registration (AC: #1)
  - [ ] 8.1 Add `tests/renderer/TestGreedyMeshing.cpp` to `tests/CMakeLists.txt`
  - [ ] 8.2 No new engine source files needed (implementation goes in existing `MeshBuilder.cpp`)
  - [ ] 8.3 Verify all existing tests pass (zero regressions)

---

## Dev Notes

### Architecture Constraints

- **Exceptions disabled** -- `buildGreedy()` is infallible (returns `ChunkMesh` directly, not `Result<ChunkMesh>`). Inputs are always valid section data.
- **No ECS for chunks** -- operates on `ChunkSection&` directly.
- **One class per file** -- `buildGreedy()` goes into existing `MeshBuilder.h/.cpp`. Do NOT create a separate `GreedyMesher` class.
- **Max ~500 lines per file** -- `MeshBuilder.cpp` will grow significantly. If it exceeds ~500 lines, extract helper functions into a private implementation detail (anonymous namespace in `.cpp` or a `detail` namespace in a separate header). Suggested split: put greedy merge algorithm helpers in an anonymous namespace within `MeshBuilder.cpp`.
- **Naming**: `PascalCase` classes, `camelCase` methods, `m_` prefix members, `SCREAMING_SNAKE` constants.
- **`#pragma once`** for all headers.
- **Namespace**: `voxel::renderer` (same as MeshBuilder).

### Algorithm Overview -- Binary Greedy Meshing for 16^3 Sections

The algorithm has 3 phases:

**Phase 1: Build padded data (18^3 block IDs + 18^3 opacity)**
```
Section 16^3 -> center of 18^3 padded array
6 neighbor sections -> 1-block face borders
Edge/corner cells -> BLOCK_AIR (same approach as Story 5.2 opacity pad)
```

**Phase 2: Face mask generation (bitwise culling)**
For each of 6 face directions:
- Iterate 16 slices perpendicular to the face normal
- Each slice: 16 rows, each row = `uint16_t` bitmask
- A bit is SET if: block at that position is solid AND adjacent block in the face direction is air/transparent
- Detection formula: `solid_here & ~solid_neighbor` applied per-row via bitwise ops

For Y-facing faces (top/bottom), the column comparison is within the same XZ position but shifted by 1 in Y. For X/Z-facing faces, compare adjacent columns in the padded array.

**Phase 3: Greedy merge per slice**
For each 16x16 face mask slice:
```
for each row r (0..15):
    bits = faceMask[r]
    while bits != 0:
        col = std::countr_zero(static_cast<uint16_t>(bits))
        type = blockType at (slice, r, col)

        // Extend width: consecutive same-type set bits
        width = 1
        for c in col+1..15:
            if bit c not set OR blockType != type: break
            width++

        widthMask = ((1 << width) - 1) << col

        // Extend height: subsequent rows with identical pattern
        height = 1
        for r2 in r+1..15:
            if (faceMask[r2] & widthMask) != widthMask: break
            if any block in [r2, col..col+width) has different type: break
            faceMask[r2] &= ~widthMask   // consume bits
            height++

        bits &= ~widthMask  // consume bits in current row

        // Map (slice, row, col) back to (x, y, z) based on face direction
        // Emit quad via packQuad(x, y, z, type, face, width, height, ao0..ao3)
```

### Coordinate Mapping -- Face Direction to Slice Axes

Each face direction defines a mapping from (slice, row, col) to (x, y, z):

```
Face PosX (face=0): normal=+X, slice axis=X, row axis=Y, col axis=Z
  slice=0..15 -> x=0..15
  row=0..15   -> y=0..15
  col=0..15   -> z=0..15
  Neighbor for face detection: block at (x+1, y, z)

Face NegX (face=1): normal=-X, slice axis=X, row axis=Y, col axis=Z
  slice=0..15 -> x=15..0 (reverse)
  Neighbor for face detection: block at (x-1, y, z)

Face PosY (face=2): normal=+Y, slice axis=Y, row axis=X, col axis=Z
  slice=0..15 -> y=0..15
  row=0..15   -> x=0..15
  col=0..15   -> z=0..15
  Neighbor for face detection: block at (x, y+1, z)

Face NegY (face=3): normal=-Y, slice axis=Y, row axis=X, col axis=Z
  slice=0..15 -> y=15..0
  Neighbor for face detection: block at (x, y-1, z)

Face PosZ (face=4): normal=+Z, slice axis=Z, row axis=Y, col axis=X
  slice=0..15 -> z=0..15
  row=0..15   -> y=0..15
  col=0..15   -> x=0..15
  Neighbor for face detection: block at (x, y, z+1)

Face NegZ (face=5): normal=-Z, slice axis=Z, row axis=Y, col axis=X
  slice=0..15 -> z=15..0
  Neighbor for face detection: block at (x, y, z-1)
```

**Implementation:** Use a constexpr axis remapping table or inline lambdas per face to convert (slice, row, col) to (x, y, z). Keep it simple -- 6 switch cases with coordinate remapping, NOT a complex axis abstraction.

### Bitmask Generation -- Detailed Example

For PosY faces (top faces), processing slice y=5:
```cpp
// For each row x in 0..15:
uint16_t bits = 0;
for (int z = 0; z < 16; ++z) {
    uint16_t here = padded[x+1][y+1][z+1];     // block at (x, y=5, z)
    uint16_t above = padded[x+1][y+2][z+1];    // block at (x, y=6, z)
    bool hereSolid = (here != BLOCK_AIR && !registry.getBlock(here).isTransparent);
    bool aboveSolid = (above != BLOCK_AIR && !registry.getBlock(above).isTransparent);
    if (hereSolid && !aboveSolid) {
        bits |= (1u << z);
    }
}
faceMask[x] = bits;  // row x of the slice for PosY at y=5
```

**Optimization:** Pre-build a 18^3 `bool` opacity array (from Story 5.2's pattern) to avoid repeated `BlockRegistry` lookups during mask generation. Then face detection becomes:
```cpp
bool hereSolid = opacity[padIndex(x+1, y+1, z+1)];
bool neighborSolid = opacity[padIndex(x+1, y+2, z+1)];
if (hereSolid && !neighborSolid) bits |= (1u << z);
```

**Further optimization for bulk column processing:** Build 16-bit solid columns along each axis, then apply bitwise AND/NOT:
```cpp
// Build solid column along Z for position (x, y) in padded coords:
uint16_t solidCol = 0;
for (int z = 0; z < 16; ++z)
    if (opacity[(y+1)*18*18 + (z+1)*18 + (x+1)])
        solidCol |= (1u << z);

// PosZ face mask for position (x, y): solid here AND NOT solid at z+1
uint16_t neighborCol = 0;
for (int z = 0; z < 16; ++z)
    if (opacity[(y+1)*18*18 + (z+2)*18 + (x+1)])
        neighborCol |= (1u << z);

faceBits = solidCol & ~neighborCol;
```

### C++20 Bit Operations

Use `<bit>` header for portable bit intrinsics:
```cpp
#include <bit>

// Find position of lowest set bit (replaces __builtin_ctz / _BitScanForward)
int pos = std::countr_zero(static_cast<uint16_t>(bits));

// Count set bits (for debug/validation)
int count = std::popcount(static_cast<uint16_t>(bits));
```

`std::countr_zero` returns the number of bits for the type if the value is 0 (16 for uint16_t). Always check `bits != 0` before calling.

**MSVC note:** `std::countr_zero` compiles to `TZCNT` or `BSF` on x86-64 with MSVC 2022. No manual intrinsics needed.

### AO Integration with Greedy Merging

**Merge predicate is block type ONLY** -- AO is NOT part of the merge decision. This follows the epic spec ("merges coplanar adjacent faces of same block type") and is the standard approach.

After merging produces a quad with position (x, y, z), width W, height H, and face direction:
1. Compute the 4 physical corner positions of the merged quad
2. For each corner, call `computeFaceAO()` (or equivalent from 5.2) using the opacity pad
3. Apply `shouldFlipQuad()` diagonal flip check
4. Pack AO + flip into the quad via `packQuad()`

**Corner positions for a merged quad** (face-dependent):
```
For PosY face at (x, y, z) with width=W (along Z), height=H (along X):
  Corner 0: (x,     y, z)
  Corner 1: (x,     y, z+W)
  Corner 2: (x+H,   y, z+W)
  Corner 3: (x+H,   y, z)
```

The exact mapping depends on the face direction's row/col axes (see coordinate mapping above). The dev agent must map the merged quad's (row, col, width, height) back to world-space block coordinates for correct AO sampling.

**Fallback if Story 5.2 is not yet complete:** Set ao0=ao1=ao2=ao3=3 and flip=0 for all quads. The greedy merge logic is independent of AO.

### Workspace Struct -- Avoid Per-Call Allocation

Pre-allocate all working buffers in a struct reused across calls:

```cpp
struct MeshWorkspace {
    // 18^3 padded block IDs (5832 entries)
    std::array<uint16_t, 18 * 18 * 18> blockPad{};

    // 18^3 opacity flags for AO (5832 entries, ~6 KB)
    std::array<bool, 18 * 18 * 18> opacityPad{};

    // Face masks: 6 faces x 16 slices x 16 rows = 1536 uint16_t values
    std::array<uint16_t, 6 * 16 * 16> faceMasks{};

    // Block type cache per slice: 16x16 block IDs for current slice
    std::array<uint16_t, 16 * 16> sliceBlockTypes{};
};
```

**Ownership:** The `MeshWorkspace` can be a member of `MeshBuilder` (since `MeshBuilder` is not shared across threads). When Story 5.6 adds async meshing, each worker thread will have its own `MeshBuilder` instance with its own workspace.

Alternatively, pass the workspace as a stack-local variable inside `buildGreedy()` -- the total size is ~20 KB, well within stack limits and fits in L1/L2 cache.

### Quad Format -- 64-bit Packed Layout (canonical, from 5.1)

```
Bit range   Width   Field                  Set by 5.3?
---------   -----   ---------------------  -----------
[0:5]       6       X position (0-63)      YES
[6:11]      6       Y position (0-63)      YES
[12:17]     6       Z position (0-63)      YES
[18:23]     6       Width - 1 (0-63)       YES (merged width)
[24:29]     6       Height - 1 (0-63)      YES (merged height)
[30:39]     10      Block state ID         YES
[40:42]     3       Face direction (0-5)   YES
[43:44]     2       AO corner 0+1          YES (if 5.2 done, else 3)
[45:46]     2       AO corner 2+3          YES (if 5.2 done, else 3)
[47]        1       Quad diagonal flip     YES (if 5.2 done, else 0)
[48]        1       Is non-cubic model     YES (0 -- greedy only handles full cubes)
[49:63]     15      Future fields          NO (stay 0)
```

Use the existing `packQuad()` from `ChunkMesh.h`. It already accepts width, height, and AO parameters:
```cpp
packQuad(x, y, z, blockStateId, face, width, height, ao0, ao1, ao2, ao3)
```

### Performance Targets and Strategy

| Test Case | Naive Baseline | Greedy Target | Notes |
|-----------|---------------|---------------|-------|
| Dense 16^3 (all solid) | ~500us | < 200us | Only boundary faces, highly mergeable |
| Typical terrain (half fill) | ~300us | < 150us | Ground plane + scattered |
| Flat ground (1 layer) | ~60us | < 30us | Nearly perfect merging |
| Empty section | ~0us | ~0us | Fast-path via `isEmpty()` |

**Key performance principles:**
1. **Pre-build opacity and block type pads ONCE** -- amortize over all 6 face directions
2. **Bitmask face detection** -- 16 blocks per `uint16_t` operation vs per-block neighbor checks
3. **`std::countr_zero` for bit scanning** -- single CPU instruction per bit position
4. **Reserve quads vector** -- typical merged mesh has far fewer quads than naive (estimate ~2048)
5. **Stack-local workspace** -- ~20 KB, no heap allocation per call
6. **`isEmpty()` fast-path** -- return immediately for empty sections (same as naive)

### MeshBuilder API -- Updated Class Declaration

```cpp
class MeshBuilder {
public:
    explicit MeshBuilder(const BlockRegistry& registry);

    // Existing: naive face culling (one quad per visible face)
    [[nodiscard]] ChunkMesh buildNaive(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors) const;

    // NEW: binary greedy meshing (merged quads)
    [[nodiscard]] ChunkMesh buildGreedy(
        const world::ChunkSection& section,
        const std::array<const world::ChunkSection*, 6>& neighbors) const;

private:
    const world::BlockRegistry& m_registry;

    [[nodiscard]] uint16_t getAdjacentBlock(...) const;  // existing
};
```

The `buildGreedy()` method signature mirrors `buildNaive()` exactly -- same inputs, same output type. This allows future callers to swap between naive and greedy easily.

### Existing API Surface -- Read Only (Do NOT Modify)

| File | Key API | Usage |
|------|---------|-------|
| `ChunkSection.h` | `getBlock(x,y,z)`, `isEmpty()`, `SIZE=16`, `VOLUME=4096` | Block data source |
| `Block.h` | `BLOCK_AIR=0`, `BlockDefinition::isTransparent`, `BlockDefinition::isSolid` | Face culling decision |
| `BlockRegistry.h` | `getBlock(uint16_t id) -> const BlockDefinition&` | Block property lookup |
| `ChunkMesh.h` | `ChunkMesh`, `packQuad()`, `unpackX/Y/Z/Width/Height/Face/AO/BlockStateId()` | Output format |
| `MeshBuilder.h` | `MeshBuilder` class, `buildNaive()` | Extend with `buildGreedy()` |
| `AmbientOcclusion.h` | `vertexAO()`, `computeFaceAO()`, `shouldFlipQuad()`, opacity pad builder | AO computation (if 5.2 is done) |

### File Locations

**Modify these files:**

| File | Change |
|------|--------|
| `engine/include/voxel/renderer/MeshBuilder.h` | Add `buildGreedy()` declaration |
| `engine/src/renderer/MeshBuilder.cpp` | Add `buildGreedy()` implementation + helper functions |
| `tests/CMakeLists.txt` | Add `tests/renderer/TestGreedyMeshing.cpp` |

**Create these files:**

| File | Path |
|------|------|
| Greedy meshing unit tests | `tests/renderer/TestGreedyMeshing.cpp` |

No new engine source files needed. The implementation lives in `MeshBuilder.cpp` alongside `buildNaive()`.

### Iteration Strategy -- Performance-Critical Inner Loop

The inner merge loop is the hot path. Structure it for minimal branching:

```cpp
// Process one slice (16 rows of uint16_t face masks)
// sliceMasks[16] = face visibility bits per row
// sliceBlocks[16][16] = block type IDs per row/col

for (int row = 0; row < 16; ++row) {
    uint16_t bits = sliceMasks[row];
    while (bits != 0) {
        int col = std::countr_zero(bits);
        uint16_t type = sliceBlocks[row][col];

        // Width extension: find consecutive same-type bits
        int width = 1;
        uint16_t scanBits = bits >> (col + 1);
        while (scanBits & 1) {
            if (sliceBlocks[row][col + width] != type) break;
            ++width;
            scanBits >>= 1;
        }

        uint16_t widthMask = static_cast<uint16_t>(((1u << width) - 1) << col);

        // Height extension: scan subsequent rows
        int height = 1;
        for (int r2 = row + 1; r2 < 16; ++r2) {
            if ((sliceMasks[r2] & widthMask) != widthMask) break;

            bool allSameType = true;
            for (int c = col; c < col + width; ++c) {
                if (sliceBlocks[r2][c] != type) { allSameType = false; break; }
            }
            if (!allSameType) break;

            sliceMasks[r2] &= static_cast<uint16_t>(~widthMask);
            ++height;
        }

        bits &= static_cast<uint16_t>(~widthMask);

        // Emit quad (coordinate mapping is face-dependent)
        // ...
    }
}
```

### What NOT To Do

- Do NOT create a separate `GreedyMesher` class -- add `buildGreedy()` to the existing `MeshBuilder`
- Do NOT modify `ChunkMesh.h` or `packQuad()` -- the existing format supports width/height already
- Do NOT modify `ChunkSection`, `ChunkColumn`, `Block`, or `BlockRegistry`
- Do NOT add async/threading -- that's Story 5.6
- Do NOT implement non-cubic block model meshing -- that's Story 5.4
- Do NOT add tint index or waving bits -- that's Story 5.5
- Do NOT add light data packing -- that's Story 8.0
- Do NOT create a chunk render pipeline -- that's Story 5.7 + Epic 6
- Do NOT delete or modify `buildNaive()` -- it remains as a correctness baseline and fallback
- Do NOT use 64-bit bitmask columns as in the cgerikj reference (that's for 62+ block chunks). Use `uint16_t` masks since our sections are 16 blocks per axis.
- Do NOT over-abstract the axis remapping -- keep it as simple switch/case or inline per-face logic
- Do NOT pre-optimize for multi-block-type scenarios -- the merge predicate is simply "same block type ID". Texture atlas considerations are Story 6.2's domain.

### Previous Story Intelligence

**From Story 5.1 (Naive Face Culling):**
- `MeshBuilder` constructor takes `const BlockRegistry&`, stored as `m_registry`
- `buildNaive()` uses `getAdjacentBlock()` helper for boundary handling -- reuse this pattern or replace with padded array approach
- Iteration order: Y-Z-X for cache coherency (matches `y*256 + z*16 + x` layout)
- `section.isEmpty()` fast-path returns empty `ChunkMesh` immediately -- preserve this
- `reserve()` on quads vector before meshing (naive uses 8192 estimate)
- Face direction enum: `PosX=0, NegX=1, PosY=2, NegY=3, PosZ=4, NegZ=5`
- Neighbor convention: `neighbors[face]` gives adjacent section; `nullptr` = air
- `packQuad()` accepts width, height, ao0-ao3 -- all defaulting for naive (width=height=1, ao=3)

**From Story 5.2 (Ambient Occlusion) spec:**
- Opacity pad: 18^3 `bool` array, center 16^3 from section, borders from neighbors
- `padIndex(px, py, pz) = (py * 18 + pz) * 18 + px` where padded coords = local + 1
- `vertexAO(side1, corner, side2)` returns 0-3
- `computeFaceAO()` samples 3 blocks per vertex corner using `AO_OFFSETS[face][corner][sample]` lookup table
- `shouldFlipQuad(ao[4])` returns true when `ao[0] + ao[3] > ao[1] + ao[2]`

**From recent git history:**
- All terrain generation (Epic 4) is complete -- no in-flight world gen changes to worry about
- Story 5.1 code exists and follows established patterns
- Test naming: `TEST_CASE("description", "[renderer][meshing]")` with `SECTION` blocks
- CMake pattern: add `.cpp` files explicitly, not via GLOB
- `#pragma warning(push, 0)` pattern for third-party headers under MSVC `/W4`

### Project Structure Notes

- `buildGreedy()` implementation goes in existing `engine/src/renderer/MeshBuilder.cpp` (no new engine source files)
- Helper functions/structs (workspace, axis mapping, slice processing) go in an anonymous namespace within `MeshBuilder.cpp`
- Test file: `tests/renderer/TestGreedyMeshing.cpp` (separate from `TestMeshBuilder.cpp` to keep files under 500 lines)
- CMake uses explicit file listing -- add test source manually to `tests/CMakeLists.txt`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md -- Story 5.3]
- [Source: _bmad-output/planning-artifacts/architecture.md -- ADR-005 (Binary Greedy Meshing), System 5 (Vulkan Renderer), Vertex Format]
- [Source: _bmad-output/project-context.md -- Naming Conventions, Error Handling, Code Organization, Build System]
- [Source: _bmad-output/implementation-artifacts/5-1-naive-face-culling.md -- MeshBuilder API, packQuad(), neighbor conventions]
- [Source: _bmad-output/implementation-artifacts/5-2-ambient-occlusion-calculation.md -- AO algorithm, opacity pad, diagonal flip]
- [Source: engine/include/voxel/renderer/ChunkMesh.h -- packQuad() with width/height/AO params]
- [Source: engine/include/voxel/renderer/MeshBuilder.h -- MeshBuilder class declaration]
- [Source: engine/src/renderer/MeshBuilder.cpp -- buildNaive() implementation, getAdjacentBlock()]
- [Source: engine/include/voxel/world/ChunkSection.h -- Block access API, SIZE=16, toIndex()]
- [Source: engine/include/voxel/world/Block.h -- BLOCK_AIR, BlockDefinition, isTransparent]
- [Source: engine/include/voxel/world/BlockRegistry.h -- getBlock() lookup]
- [Reference: cgerikj/binary-greedy-meshing (GitHub) -- Algorithm reference, adapted for 16^3]
- [Reference: 0fps.net/2012/06/30/meshing-in-a-minecraft-game/ -- Foundational greedy meshing]
- [Reference: Handmade Seattle 2022 "Optimism in Design" by Davis Morley -- Binary meshing talk]

---

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List