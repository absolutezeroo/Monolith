# Story 1.5: Math Types and Coordinate Utilities

Status: done

## Story

As a developer,
I want math primitives and voxel coordinate conversion helpers,
so that all spatial code uses consistent, tested utilities.

## Acceptance Criteria

1. **MathTypes.h** — `voxel/math/MathTypes.h` with GLM aliases: `Vec3`, `DVec3`, `IVec2`, `IVec3`, `Mat4` etc. Thin aliases, not wrappers. Includes GLM configuration defines.
2. **AABB.h** — `voxel/math/AABB.h` — struct with `min`/`max` (`Vec3`); methods: `contains(point)`, `intersects(other)`, `expand(point)`, `center()`, `extents()`
3. **Ray.h** — `voxel/math/Ray.h` — struct with `origin` (`Vec3`) + `direction` (`Vec3`, normalized)
4. **CoordUtils.h** — `voxel/math/CoordUtils.h` — free functions: `worldToChunk(DVec3) → IVec2`, `worldToLocal(DVec3) → IVec3`, `localToWorld(IVec2 chunk, IVec3 local) → DVec3`, `blockToIndex(int x, int y, int z) → int32_t`, `indexToBlock(int32_t) → IVec3`
5. **Namespace** — All types in `namespace voxel::math`
6. **Unit Tests** — AABB intersection/contains, coordinate roundtrips (world→chunk→local→world identity), `blockToIndex`/`indexToBlock` inverse, boundary values (0,0,0 and 15,15,15), negative coordinate handling

## Tasks / Subtasks

- [x] Task 1: Create `engine/include/voxel/math/MathTypes.h` (AC: 1, 5)
  - [x] 1.1 Add GLM configuration defines (`GLM_FORCE_DEPTH_ZERO_TO_ONE`, `GLM_FORCE_LEFT_HANDED`) via CMake
  - [x] 1.2 Define `using` aliases in `voxel::math`: `Vec3 = glm::vec3`, `DVec3 = glm::dvec3`, `IVec2 = glm::ivec2`, `IVec3 = glm::ivec3`, `Mat4 = glm::mat4`
  - [x] 1.3 Include `voxel/core/Types.h` for fixed-width type access
- [x] Task 2: Create `engine/include/voxel/math/AABB.h` (AC: 2, 5)
  - [x] 2.1 Define `struct AABB` with `Vec3 min` and `Vec3 max` members
  - [x] 2.2 Implement `contains(Vec3 point) const → bool`
  - [x] 2.3 Implement `intersects(const AABB& other) const → bool`
  - [x] 2.4 Implement `expand(Vec3 point) → AABB&` (grows to include point)
  - [x] 2.5 Implement `center() const → Vec3`
  - [x] 2.6 Implement `extents() const → Vec3` (half-widths)
- [x] Task 3: Create `engine/include/voxel/math/Ray.h` (AC: 3, 5)
  - [x] 3.1 Define `struct Ray` with `Vec3 origin` and `Vec3 direction`
  - [x] 3.2 Direction should be assumed normalized (document contract, do not auto-normalize)
- [x] Task 4: Create `engine/include/voxel/math/CoordUtils.h` (AC: 4, 5)
  - [x] 4.1 Define `SECTION_SIZE = 16` constant
  - [x] 4.2 Implement `worldToChunk(DVec3) → IVec2` using `std::floor` (XZ only, drops Y)
  - [x] 4.3 Implement `worldToLocal(DVec3) → IVec3` using bitmask `& 0xF` for XZ, `static_cast<int32_t>(std::floor(pos.y))` for Y
  - [x] 4.4 Implement `localToWorld(IVec2 chunk, IVec3 local) → DVec3`
  - [x] 4.5 Implement `blockToIndex(int x, int y, int z) → int32_t` with Y-major layout: `y*256 + z*16 + x`
  - [x] 4.6 Implement `indexToBlock(int32_t) → IVec3` as inverse
- [x] Task 5: Update `engine/CMakeLists.txt` (AC: 1)
  - [x] 5.1 Add GLM compile definitions: `GLM_FORCE_DEPTH_ZERO_TO_ONE`, `GLM_FORCE_LEFT_HANDED`
  - [x] 5.2 No new .cpp sources needed (all header-only/inline for math utilities)
- [x] Task 6: Create `tests/math/TestAABB.cpp` (AC: 6)
  - [x] 6.1 Test `contains()` with interior point, boundary point, exterior point
  - [x] 6.2 Test `intersects()` with overlapping, adjacent, separated AABBs
  - [x] 6.3 Test `expand()` grows AABB to include new point
  - [x] 6.4 Test `center()` and `extents()` compute correct values
- [x] Task 7: Create `tests/math/TestCoordUtils.cpp` (AC: 6)
  - [x] 7.1 Roundtrip test: `worldToChunk(localToWorld(chunk, local)) == chunk`
  - [x] 7.2 `blockToIndex(indexToBlock(i)) == i` for all i in [0, 4095]
  - [x] 7.3 Boundary values: (0,0,0), (15,15,15), (16,0,0), (-1,0,0), (-16,0,0), (-17,0,0)
  - [x] 7.4 Negative coordinate handling: world coord -1 maps to chunk -1, local 15
- [x] Task 8: Update `tests/CMakeLists.txt`
  - [x] 8.1 Add `math/TestAABB.cpp` and `math/TestCoordUtils.cpp` to VoxelTests

## Dev Notes

### Critical: Negative Coordinate Floor Division

C++ integer `/` truncates toward zero, which is WRONG for chunk coordinates:
- `-7 / 16 = 0` in C++ (should be -1)
- `-17 / 16 = -1` in C++ (should be -2)

**For `worldToChunk(DVec3)`**: use `std::floor(pos.x / 16.0)` then cast to `int32_t`. This correctly floors for all values.

**For integer versions (if needed later)**: use arithmetic right shift `>> 4` which C++20 guarantees floors for signed integers, OR use bitmask approach.

**For `worldToLocal`**: use `static_cast<int32_t>(std::floor(pos.x)) & 0xF` for X and Z. The bitmask `& 0xF` always yields [0, 15] for two's complement (mandated by C++20). For Y, `static_cast<int32_t>(std::floor(pos.y))` directly since Y is not modular (sections stack vertically).

### Critical: blockToIndex Layout is Y-Major

The architecture specifies flat array indexing as `y*256 + z*16 + x`. This is Y-major ordering where iterating Y for a fixed (X,Z) column is sequential in memory. This is critical for:
- Sky light propagation (top-down column iteration)
- Heightmap calculation
- WorldGen surface decoration

```cpp
// Forward
inline int32_t blockToIndex(int x, int y, int z)
{
    return y * (SECTION_SIZE * SECTION_SIZE) + z * SECTION_SIZE + x;
}

// Inverse
inline IVec3 indexToBlock(int32_t index)
{
    int32_t y = index >> 8;           // index / 256
    int32_t z = (index >> 4) & 0xF;  // (index % 256) / 16
    int32_t x = index & 0xF;         // index % 16
    return {x, y, z};
}
```

### Critical: AABB Methods Must Use Correct Comparison

- `contains(point)`: All components `point >= min && point <= max` (inclusive on both ends)
- `intersects(other)`: `min.x <= other.max.x && max.x >= other.min.x` for all 3 axes. Touching AABBs (shared face) count as intersecting.
- `center()`: `(min + max) * 0.5f`
- `extents()`: `(max - min) * 0.5f` (half-widths, NOT full widths)
- `expand(point)`: `min = glm::min(min, point); max = glm::max(max, point);`

### Header-Only Implementation

All math types are trivial enough to be header-only with `inline` functions. No `.cpp` files needed under `engine/src/math/`. This matches the architecture: Core Math layer has no external dependencies beyond GLM (which is header-only).

AABB and CoordUtils methods should be `inline` (defined in the header). They're small, hot-path functions that benefit from inlining.

### GLM Configuration Defines

Add to `engine/CMakeLists.txt` as PUBLIC compile definitions on VoxelEngine:

```cmake
target_compile_definitions(VoxelEngine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_LEFT_HANDED
)
```

- `GLM_FORCE_DEPTH_ZERO_TO_ONE` — Vulkan uses [0,1] depth range (not OpenGL's [-1,1])
- `GLM_FORCE_LEFT_HANDED` — matches project-context.md specification

Do NOT add `GLM_FORCE_CTOR_INIT` — prefer explicit initialization to catch bugs.
Do NOT add `GLM_FORCE_INTRINSICS` yet — `constexpr` compatibility is more valuable at this stage.

### Project Structure Notes

Files to create (no existing math/ directories exist yet):

```
engine/include/voxel/math/
├── MathTypes.h      ← CREATE (GLM aliases)
├── AABB.h           ← CREATE (AABB struct)
├── Ray.h            ← CREATE (Ray struct)
└── CoordUtils.h     ← CREATE (coordinate free functions)

tests/math/
├── TestAABB.cpp        ← CREATE
└── TestCoordUtils.cpp  ← CREATE
```

Modified files:
```
engine/CMakeLists.txt   ← MODIFY (add GLM defines)
tests/CMakeLists.txt    ← MODIFY (add math test files)
```

No new `engine/src/math/` directory is needed — everything is header-only.

### Established Patterns from Previous Stories

- **Header style**: `#pragma once`, namespace `voxel::math`, Allman braces, 120 char column limit
- **Include order**: associated header → project headers (`voxel/...`) → third-party (`<glm/...>`) → std library (`<cstdint>`)
- **Naming**: PascalCase structs (`AABB`, `Ray`), camelCase functions (`worldToChunk`, `blockToIndex`), `m_` prefix for members (none needed here — these are simple POD structs with public members)
- **Testing**: Catch2 v3, `TEST_CASE("name", "[math][aabb]")` + `SECTION` blocks. Tags: `[math]` + specific like `[aabb]`, `[ray]`, `[coords]`
- **CMake**: source files listed explicitly (no GLOBs), `PRIVATE`/`PUBLIC` scopes intentional

### Coordinate Domain Reference

| Domain | Type | Range | Usage |
|--------|------|-------|-------|
| World position | `DVec3` (double) | Unbounded | Player position, entity positions |
| Chunk coordinate | `IVec2` (int) | Unbounded XZ pair | ChunkManager spatial key |
| Local block coordinate | `IVec3` (int) | [0,15] per axis in section | Block access within ChunkSection |
| Block index | `int32_t` | [0, 4095] | Flat array index into ChunkSection |

`worldToChunk` returns only XZ because ChunkColumns span all Y values (16 stacked sections × 16 blocks = 256 blocks tall).

### Test Boundary Values Reference

| World Coord | Expected Chunk | Expected Local |
|------------|---------------|----------------|
| (0, 0, 0) | (0, 0) | (0, 0, 0) |
| (15, 0, 15) | (0, 0) | (15, 0, 15) |
| (16, 0, 0) | (1, 0) | (0, 0, 0) |
| (-1, 0, 0) | (-1, 0) | (15, 0, 0) |
| (-16, 0, 0) | (-1, 0) | (0, 0, 0) |
| (-17, 0, 0) | (-2, 0) | (15, 0, 0) |

### Potential Pitfalls

1. **GLM default constructors do NOT zero-initialize** (since GLM 1.0). Always use explicit initialization: `Vec3{0.0f}`, not `Vec3{}`. This is intentional — do not add `GLM_FORCE_CTOR_INIT`.

2. **`AABB min/max` naming conflict** — `min` and `max` are common macros in `<windows.h>`. The project uses `#pragma once` and targeted includes, but if Windows headers leak, this could be an issue. GLM internally handles this via `#undef min`/`#undef max`. If problems arise, use `NOMINMAX` define (already common practice on Windows builds).

3. **`worldToLocal` Y component** — Unlike X and Z which wrap within [0,15], Y maps to the absolute block Y coordinate (0–255 across all sections). The section index is `y / 16`, the local Y within a section is `y % 16`. The `worldToLocal` function as specified returns the full integer Y, not the section-local Y. The ChunkManager later splits this into section index + local Y.

4. **No validation in CoordUtils** — These are hot-path functions called millions of times. Do not add bounds checking. Document valid input ranges in comments. Use `VX_ASSERT` only in debug if desired for development-time validation.

5. **Ray direction normalization** — The `Ray` struct documents that `direction` must be normalized, but does NOT enforce it (no auto-normalize in constructor). The caller is responsible. This matches the DDA algorithm requirements where a normalized direction is a precondition.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.5]
- [Source: _bmad-output/planning-artifacts/architecture.md — ChunkSection indexing, Coordinate system, AABB collision]
- [Source: _bmad-output/project-context.md — Naming conventions, GLM 0.9.9+, testing strategy]
- [Source: _bmad-output/implementation-artifacts/1-4-logging-via-spdlog.md — Previous story patterns, file structure]
- [Source: GLM 1.0.3 release — Latest stable, default constructors NOT zero-initialized]
- [Source: Catch2 v3.13.0 — WithinAbs matcher for floating-point comparisons, BDD macros]
- [Source: C++20 spec — Arithmetic right shift guaranteed for signed integers, two's complement mandated]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6 (claude-opus-4-6)

### Debug Log References

- Build: MSVC 2022 v18.4.0, msvc-debug preset, 0 warnings
- Tests: 19 test cases, 16511 assertions, all passed (0 regressions)

### Completion Notes List

- Created `voxel::math` namespace with GLM type aliases (Vec2/3/4, DVec2/3/4, IVec2/3/4, UVec2/3/4, Mat3/4)
- Implemented AABB struct with contains, intersects, expand, center, extents — all header-only inline
- Implemented Ray struct with documented normalization contract (caller responsibility)
- Implemented CoordUtils with worldToChunk, worldToLocal, localToWorld, blockToIndex, indexToBlock
- Used std::floor for correct negative coordinate floor division (not integer truncation)
- Used bitmask & 0xF for local XZ wrapping (C++20 two's complement guarantee)
- Y-major block indexing layout (y*256 + z*16 + x) per architecture spec
- Added GLM_FORCE_DEPTH_ZERO_TO_ONE and GLM_FORCE_LEFT_HANDED compile definitions
- AABB tests: contains (interior/boundary/exterior), intersects (overlapping/adjacent/separated/contained/identical), expand, center, extents
- CoordUtils tests: worldToChunk/worldToLocal for all 6 boundary values from spec, roundtrip identity, blockToIndex/indexToBlock inverse for all 4096 indices, negative coordinate handling
- All existing tests continue to pass (no regressions)

### Change Log

- 2026-03-24: Initial implementation of Story 1.5 — Math Types and Coordinate Utilities
- 2026-03-24: Code review — added NOMINMAX compile definition to engine/CMakeLists.txt (LOW fix: prevent Windows min/max macro conflicts with AABB members)

### File List

- engine/include/voxel/math/MathTypes.h (NEW)
- engine/include/voxel/math/AABB.h (NEW)
- engine/include/voxel/math/Ray.h (NEW)
- engine/include/voxel/math/CoordUtils.h (NEW)
- engine/CMakeLists.txt (MODIFIED — added GLM compile definitions + NOMINMAX)
- tests/math/TestAABB.cpp (NEW)
- tests/math/TestCoordUtils.cpp (NEW)
- tests/CMakeLists.txt (MODIFIED — added math test files)