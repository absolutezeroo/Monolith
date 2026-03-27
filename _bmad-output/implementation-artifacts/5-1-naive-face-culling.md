# Story 5.1: Naive Face Culling (Baseline Mesher)

Status: done

## Story

As a developer,
I want a simple mesher that only emits faces between solid and air blocks,
so that I have a working baseline to render chunks before optimizing.

## Acceptance Criteria

1. `MeshBuilder::buildNaive(const ChunkSection&, neighbors[6]) → ChunkMesh` produces correct quad data
2. `ChunkMesh` struct contains `std::vector<uint64_t> quads` (packed 8-byte format) and `uint32_t quadCount`
3. Only emit a face when the adjacent block (in that face's direction) is air or transparent
4. Neighbor sections provided for face culling at section boundaries (local coord 15→0 edge); null neighbor = treat as air
5. Quad format encodes position, block state ID, face direction with width=1/height=1 (no merging), AO=3 (no occlusion)
6. Unit test: single block in empty section → 6 faces
7. Unit test: two adjacent blocks → 10 faces (shared face culled)
8. Performance baseline measured via Catch2 BENCHMARK (~500μs/chunk for dense terrain)

## Tasks / Subtasks

- [x] Task 1: Create `ChunkMesh` struct (AC: #2)
  - [x] 1.1 Create `engine/include/voxel/renderer/ChunkMesh.h` with `ChunkMesh` struct
  - [x] 1.2 Define `BlockFace` enum class: `PosX, NegX, PosY, NegY, PosZ, NegZ` (6 values)
  - [x] 1.3 Define quad packing/unpacking helper functions (constexpr inline)
- [x] Task 2: Create `MeshBuilder` class (AC: #1, #3, #4, #5)
  - [x] 2.1 Create `engine/include/voxel/renderer/MeshBuilder.h` with class declaration
  - [x] 2.2 Create `engine/src/renderer/MeshBuilder.cpp` with `buildNaive()` implementation
  - [x] 2.3 Implement face culling: iterate all blocks, for each non-air block check 6 neighbors
  - [x] 2.4 Handle section boundary checks using neighbor array (null = air)
  - [x] 2.5 Pack each visible face into 64-bit quad format
- [x] Task 3: Register in build system (AC: #1)
  - [x] 3.1 Add `src/renderer/MeshBuilder.cpp` to `engine/CMakeLists.txt`
  - [x] 3.2 Add `tests/renderer/TestMeshBuilder.cpp` to `tests/CMakeLists.txt`
- [x] Task 4: Write unit tests (AC: #6, #7)
  - [x] 4.1 Create `tests/renderer/TestMeshBuilder.cpp`
  - [x] 4.2 Test: single block in empty section → exactly 6 quads
  - [x] 4.3 Test: two adjacent blocks → exactly 10 quads (shared face culled)
  - [x] 4.4 Test: block at section boundary with null neighbor → face emitted (treated as air)
  - [x] 4.5 Test: block at section boundary with solid neighbor → face culled
  - [x] 4.6 Test: transparent block adjacent to opaque → face emitted on both sides
  - [x] 4.7 Test: empty section → 0 quads (fast-path exit via `isEmpty()`)
  - [x] 4.8 Test: quad packing roundtrip — pack then unpack, verify all fields match
- [x] Task 5: Performance benchmark (AC: #8)
  - [x] 5.1 Add Catch2 BENCHMARK for dense terrain section (all blocks filled)
  - [x] 5.2 Add BENCHMARK for typical terrain (ground plane + scattered blocks)
  - [x] 5.3 Log baseline numbers for comparison with Story 5.3 (binary greedy meshing)

## Dev Notes

### Architecture Constraints

- **Exceptions disabled** — use `Result<T>` (`std::expected<T, EngineError>`) for fallible operations. `buildNaive()` itself is infallible (returns `ChunkMesh` directly, not `Result<ChunkMesh>`) since inputs are always valid.
- **No ECS for chunks** — MeshBuilder operates on `ChunkSection&` directly, not ECS components.
- **One class per file** — `ChunkMesh.h` for the struct, `MeshBuilder.h/.cpp` for the mesher.
- **Max ~500 lines per file** — MeshBuilder should stay well under this limit for naive meshing.
- **Naming**: `PascalCase` classes, `camelCase` methods, `m_` prefix members, `SCREAMING_SNAKE` constants.
- **`#pragma once`** for all headers (no include guards).

### Quad Format — 64-bit Packed Layout

**This is the canonical bit allocation for ALL meshing stories. Story 5.1 sets bits marked below.**

```
Bit range   Width   Field                  Set by 5.1?   Value for 5.1
─────────   ─────   ─────────────────────  ────────────   ─────────────
[0:5]       6       X position (0–63)      YES            block local X
[6:11]      6       Y position (0–63)      YES            block local Y
[12:17]     6       Z position (0–63)      YES            block local Z
[18:23]     6       Width - 1 (0–63)       YES            0 (width=1, no merge)
[24:29]     6       Height - 1 (0–63)      YES            0 (height=1, no merge)
[30:39]     10      Block state ID         YES            from ChunkSection
[40:42]     3       Face direction (0–5)   YES            BlockFace enum value
[43:44]     2       AO corner 0+1          YES            3 (no occlusion)
[45:46]     2       AO corner 2+3          YES            3 (no occlusion)
[47]        1       Quad diagonal flip     YES            0 (no flip)
[48]        1       Is non-cubic model     YES            0 (cubic)
[49:52]     4       Sky light (0–15)       NO             0 (Story 8.0)
[53:56]     4       Block light (0–15)     NO             0 (Story 8.0)
[57:59]     3       Tint index (0–7)       NO             0 (Story 5.5)
[60:61]     2       Waving type (0–3)      NO             0 (Story 5.5)
[62:63]     2       Reserved               NO             0
```

**Packing example (constexpr helper):**
```cpp
inline constexpr uint64_t packQuad(uint8_t x, uint8_t y, uint8_t z,
                                    uint16_t blockStateId, uint8_t face,
                                    uint8_t ao0 = 3, uint8_t ao1 = 3,
                                    uint8_t ao2 = 3, uint8_t ao3 = 3) {
    uint64_t q = 0;
    q |= static_cast<uint64_t>(x & 0x3F);
    q |= static_cast<uint64_t>(y & 0x3F) << 6;
    q |= static_cast<uint64_t>(z & 0x3F) << 12;
    // width-1=0, height-1=0 (bits 18-29 stay zero)
    q |= static_cast<uint64_t>(blockStateId & 0x3FF) << 30;
    q |= static_cast<uint64_t>(face & 0x7) << 40;
    q |= static_cast<uint64_t>(ao0 & 0x3) << 43;
    q |= static_cast<uint64_t>(ao1 & 0x3) << 45;
    // bits 47-63 stay zero for Story 5.1
    return q;
}
```

**Face direction encoding:**
```
0 = PosX (+X)    1 = NegX (-X)
2 = PosY (+Y)    3 = NegY (-Y)
4 = PosZ (+Z)    5 = NegZ (-Z)
```

### Neighbor Array Convention

The `neighbors[6]` parameter uses the same ordering as `BlockFace`:
```
neighbors[0] = +X neighbor section (or nullptr)
neighbors[1] = -X neighbor section
neighbors[2] = +Y neighbor section (section above)
neighbors[3] = -Y neighbor section (section below)
neighbors[4] = +Z neighbor section
neighbors[5] = -Z neighbor section
```

For a block at local position (x, y, z), checking adjacent blocks:
- Internal (0 < coord < 15): read from same section via `getBlock(nx, ny, nz)`
- Boundary (coord == 0 or 15): read from `neighbors[face]` via opposite edge
  - Example: block at x=15, face=PosX → read `neighbors[0]->getBlock(0, y, z)`
  - If `neighbors[face]` is `nullptr`, treat as air (emit the face)

### BlockRegistry Integration

The mesher needs a `const BlockRegistry&` to check block transparency:
```cpp
class MeshBuilder {
public:
    explicit MeshBuilder(const BlockRegistry& registry);
    ChunkMesh buildNaive(const ChunkSection& section,
                         const std::array<const ChunkSection*, 6>& neighbors) const;
private:
    const BlockRegistry& m_registry;
};
```

**Face emission rule:** For block B at position P with face F:
```cpp
uint16_t neighborId = getAdjacentBlock(section, neighbors, x, y, z, face);
const auto& neighborDef = m_registry.getBlock(neighborId);
bool shouldEmit = (neighborId == BLOCK_AIR || neighborDef.isTransparent);
```

**Important:** Use `getBlock()` on the `BlockRegistry`, NOT `getBlockType()`. The `getBlock()` method takes the raw `uint16_t` block ID from the section. The `isTransparent` field on `BlockDefinition` is what determines face culling.

### Existing API Surface (Do NOT Modify)

These files are inputs — read-only for Story 5.1:

| File | Key API | Usage |
|------|---------|-------|
| `ChunkSection.h` | `getBlock(x,y,z)`, `isEmpty()`, `SIZE=16` | Iterate blocks, fast-path empty |
| `Block.h` | `BLOCK_AIR=0`, `BlockDefinition::isTransparent` | Face culling decision |
| `BlockRegistry.h` | `getBlock(uint16_t id) → const BlockDefinition&` | Lookup transparency |

### File Locations

**Create these files:**

| File | Path |
|------|------|
| ChunkMesh header | `engine/include/voxel/renderer/ChunkMesh.h` |
| MeshBuilder header | `engine/include/voxel/renderer/MeshBuilder.h` |
| MeshBuilder source | `engine/src/renderer/MeshBuilder.cpp` |
| Unit tests | `tests/renderer/TestMeshBuilder.cpp` |

**Modify these files:**

| File | Change |
|------|--------|
| `engine/CMakeLists.txt` | Add `src/renderer/MeshBuilder.cpp` to source list |
| `tests/CMakeLists.txt` | Add `tests/renderer/TestMeshBuilder.cpp` to test sources |

### Iteration Strategy

Iterate blocks in Y-Z-X order (matches the flat array layout `y*256 + z*16 + x`) for cache-friendly access:
```cpp
for (int y = 0; y < ChunkSection::SIZE; ++y)
    for (int z = 0; z < ChunkSection::SIZE; ++z)
        for (int x = 0; x < ChunkSection::SIZE; ++x)
```

Skip air blocks immediately (no faces to emit for air). Use `section.isEmpty()` for fast-path: if the entire section is empty, return an empty `ChunkMesh` immediately.

### Performance Notes

- **Baseline target:** ~500μs/chunk for a fully dense 16³ section
- This is the **naive** mesher — no merging, no optimization. It exists as:
  1. A correctness baseline for the binary greedy mesher (Story 5.3)
  2. A fallback for debugging mesh issues
- Do NOT optimize prematurely. The binary greedy mesher (Story 5.3) will replace this for production use.
- `reserve()` the quads vector: worst case is `16*16*16*6 = 24576` quads for a fully exposed section, but typical terrain is ~30-50% of that.

### What NOT To Do

- Do NOT create a Renderer integration — that's Story 5.7 (upload) and Epic 6 (shaders)
- Do NOT add async/threading — that's Story 5.6 (enkiTS jobs)
- Do NOT implement greedy merging — that's Story 5.3
- Do NOT compute AO values — set all AO to 3 (no occlusion). Story 5.2 adds real AO.
- Do NOT handle transparent-to-transparent face culling rules — that's Story 6.7
- Do NOT add tint index or waving — that's Story 5.5
- Do NOT add light data packing — that's Story 8.0
- Do NOT modify ChunkSection, ChunkColumn, Block, or BlockRegistry
- Do NOT create a ChunkMeshManager or MeshCache — keep it simple, just the builder

### Project Structure Notes

- New files go in `engine/include/voxel/renderer/` and `engine/src/renderer/` (meshing is part of the renderer subsystem per architecture)
- Tests go in `tests/renderer/` directory (create it if it doesn't exist — this will be the first renderer test file)
- CMake uses explicit file listing (no GLOB) — add each new source file manually

### Previous Story Intelligence

**From Story 4.4 (3D Caves and Overhangs):**
- FastNoiseLite wrapped with `#pragma warning(push, 0)` for MSVC `/W4` compat — follow same pattern if needed for any third-party headers
- Test naming convention: `TEST_CASE("description", "[module][feature]")` with `SECTION` blocks
- CMake pattern: add `.cpp` files explicitly, not via GLOB
- Seed cascading: `seed + offset` for distinct noise instances (established offsets up to 5)
- All 105 existing tests must continue to pass — zero regressions allowed

**From recent git history:**
- Story 4.5 (Tree & Decoration) is in-progress with new `StructureGenerator.h/.cpp` files
- Code follows established patterns: utility classes in `voxel::world`, no heavy abstractions
- WorldGenerator calls utility classes (CaveCarver, BiomeSystem, StructureGenerator) as post-passes

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md — Story 5.1]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 5: Vulkan Renderer, ADR-005, ADR-009]
- [Source: _bmad-output/planning-artifacts/architecture.md — Vertex Format: 8 Bytes Per Quad]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Threading]
- [Source: _bmad-output/implementation-artifacts/4-4-3d-caves-and-overhangs.md — Test patterns, CMake patterns]
- [Source: engine/include/voxel/world/ChunkSection.h — Block access API]
- [Source: engine/include/voxel/world/Block.h — BLOCK_AIR, BlockDefinition, isTransparent]
- [Source: engine/include/voxel/world/BlockRegistry.h — getBlock() lookup]
- [Source: engine/include/voxel/renderer/Gigabuffer.h — Future upload target]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- MSVC C4100 (unreferenced parameter): `ao2`/`ao3` in `packQuad()` — reduced to 2 AO params (`ao01`, `ao23`) matching the 2x2-bit format
- MSVC C4099 (class/struct mismatch): forward declaration of `ChunkSection` as `class` → fixed to `struct`

### Completion Notes List
- Created `ChunkMesh.h` with `ChunkMesh` struct, `BlockFace` enum (6 values), and constexpr `packQuad`/`unpackX`/`unpackY`/`unpackZ`/`unpackWidth`/`unpackHeight`/`unpackBlockStateId`/`unpackFace`/`unpackAO01`/`unpackAO23` helpers
- Created `MeshBuilder.h/.cpp` with `buildNaive()` — iterates Y-Z-X order, skips air, checks 6 faces per block, handles boundary via neighbor array (null = air), packs into 64-bit quad format
- All 8 unit test cases pass: single block (6 quads), two adjacent (10 quads), boundary null (face emitted), boundary solid (face culled), transparent adjacent to opaque (correct face emission), empty section (0 quads), quad packing roundtrip
- 2 Catch2 BENCHMARK cases: dense terrain (16^3 all solid), typical terrain (half filled + scattered)
- Full regression suite: 474,076 assertions in 116 test cases — all pass, zero regressions

### File List
- `engine/include/voxel/renderer/ChunkMesh.h` (new)
- `engine/include/voxel/renderer/MeshBuilder.h` (new)
- `engine/src/renderer/MeshBuilder.cpp` (new)
- `tests/renderer/TestMeshBuilder.cpp` (new)
- `engine/CMakeLists.txt` (modified — added MeshBuilder.cpp)
- `tests/CMakeLists.txt` (modified — added TestMeshBuilder.cpp)
- `game/src/GameApp.cpp` (modified — replaced inline block registration with `loadFromJson()`)

### Change Log
- 2026-03-27: Implemented Story 5.1 — Naive Face Culling (Baseline Mesher). All ACs satisfied, all tests pass.
- 2026-03-27: [Code Review] Documented GameApp.cpp change (inline block registration → `loadFromJson()`).