# Story 3.4: ChunkManager + Spatial HashMap

Status: ready-for-dev

## Story

As a **game engine developer**,
I want a **ChunkManager that spatially indexes ChunkColumns via a hash map and translates world coordinates to chunk-local coordinates**,
so that **all higher-level systems (terrain gen, meshing, physics, rendering) can load, unload, and query voxel data by world position with O(1) chunk lookup**.

## Acceptance Criteria

1. **ChunkCoordHash** struct hashes `glm::ivec2` using XOR-shift combination (not `std::hash` default).
2. **ChunkManager** class stores `std::unordered_map<glm::ivec2, std::unique_ptr<ChunkColumn>, ChunkCoordHash>`.
3. `getChunk(glm::ivec2 coord)` returns `ChunkColumn*` (null if not loaded).
4. `getBlock(glm::ivec3 worldPos)` translates world→chunk+local, returns `BLOCK_AIR` if chunk not loaded.
5. `setBlock(glm::ivec3 worldPos, uint16_t id)` translates, creates section if needed, marks section dirty.
6. `loadChunk(glm::ivec2 coord)` inserts a new empty `ChunkColumn` (no-op if already loaded).
7. `unloadChunk(glm::ivec2 coord)` removes from map (no-op if not loaded).
8. `loadedChunkCount()` and `dirtyChunkCount()` return current counts.
9. All coordinate translation handles **negative world coordinates** correctly (floor division, not truncation).
10. Unit tests cover: load/unload lifecycle, getBlock across chunk boundaries, setBlock marks dirty, negative coordinate translation, hash distribution sanity.

## Tasks / Subtasks

- [ ] Task 1: Implement ChunkCoordHash (AC: #1)
  - [ ] 1.1 Create `ChunkManager.h` with `ChunkCoordHash` struct
  - [ ] 1.2 Use XOR-shift formula from architecture: `h ^= hash(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2)`
- [ ] Task 2: Implement coordinate translation helpers (AC: #4, #5, #9)
  - [ ] 2.1 `worldToChunkCoord(glm::ivec3) → glm::ivec2` using floor division
  - [ ] 2.2 `worldToLocalPos(glm::ivec3) → glm::ivec3` using Euclidean modulo
  - [ ] 2.3 These can be free functions or static methods in `voxel::world` namespace
- [ ] Task 3: Implement ChunkManager class (AC: #2–#8)
  - [ ] 3.1 `getChunk()` — simple map lookup, return raw pointer or null
  - [ ] 3.2 `getBlock()` — translate, lookup chunk, delegate to ChunkColumn::getBlock
  - [ ] 3.3 `setBlock()` — translate, loadChunk if not loaded (or just no-op?), delegate to ChunkColumn::setBlock
  - [ ] 3.4 `loadChunk()` — emplace new ChunkColumn if absent
  - [ ] 3.5 `unloadChunk()` — erase from map
  - [ ] 3.6 `loadedChunkCount()` — return `m_chunks.size()`
  - [ ] 3.7 `dirtyChunkCount()` — iterate chunks, count those with any dirty section
- [ ] Task 4: Write unit tests (AC: #10)
  - [ ] 4.1 Load/unload lifecycle (load → verify present → unload → verify absent)
  - [ ] 4.2 getBlock returns AIR for unloaded chunks
  - [ ] 4.3 setBlock on unloaded chunk (decide: no-op or auto-load)
  - [ ] 4.4 Cross-chunk-boundary getBlock (e.g., worldX=15 vs worldX=16)
  - [ ] 4.5 setBlock marks correct section dirty
  - [ ] 4.6 Negative coordinate translation (-1 → chunk -1, local 15)
  - [ ] 4.7 Hash distribution: verify no collisions for common coord pairs
  - [ ] 4.8 Multiple chunks loaded simultaneously
- [ ] Task 5: Update CMakeLists.txt (AC: all)
  - [ ] 5.1 Add ChunkManager.cpp to engine sources
  - [ ] 5.2 Add TestChunkManager.cpp to test sources

## Dev Notes

### Critical: Coordinate Translation Math

**This is the #1 source of bugs in voxel engines.** C++ integer division truncates toward zero, but chunk coordinates must use **floor division** for negative coordinates to work:

```cpp
// WRONG — C++ truncation division:
//   -1 / 16 = 0  (should be -1)
//   -17 / 16 = -1 (should be -2)

// CORRECT — floor division:
inline int floorDiv(int a, int b) {
    return a / b - (a % b != 0 && (a ^ b) < 0);
}

// CORRECT — Euclidean modulo (always non-negative):
inline int euclideanMod(int a, int b) {
    int r = a % b;
    return r + (r < 0) * b;  // branchless: if r < 0, add b
}

// Coordinate helpers:
inline glm::ivec2 worldToChunkCoord(const glm::ivec3& worldPos) {
    return { floorDiv(worldPos.x, 16), floorDiv(worldPos.z, 16) };
}

inline glm::ivec3 worldToLocalPos(const glm::ivec3& worldPos) {
    return { euclideanMod(worldPos.x, 16),
             worldPos.y,  // Y is NOT chunked horizontally
             euclideanMod(worldPos.z, 16) };
}
```

**Key insight**: Y coordinate is passed through directly — ChunkColumn handles the section decomposition (`sectionIndex = y / 16`, `localY = y % 16`) internally. ChunkManager only chunks on X/Z.

**Test cases for negative coords:**
| worldPos | chunkCoord | localPos |
|----------|-----------|----------|
| (0, 64, 0) | (0, 0) | (0, 64, 0) |
| (15, 0, 15) | (0, 0) | (15, 0, 15) |
| (16, 0, 0) | (1, 0) | (0, 0, 0) |
| (-1, 0, 0) | (-1, 0) | (15, 0, 0) |
| (-16, 0, 0) | (-1, 0) | (0, 0, 0) |
| (-17, 0, -17) | (-2, -2) | (15, 0, 15) |
| (31, 255, 31) | (1, 1) | (15, 255, 15) |

### ChunkCoordHash — Architecture-Mandated Implementation

```cpp
struct ChunkCoordHash {
    size_t operator()(const glm::ivec2& v) const noexcept {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
```

This is the **exact** hash specified in `architecture.md`. Uses the golden ratio constant `0x9e3779b9` for better bit distribution. Do NOT use a simpler hash — it causes clustering for grid coordinates.

Also need `glm::ivec2` equality operator. GLM provides `operator==` for `ivec2` by default when using `<glm/vec2.hpp>`, so no custom `ChunkCoordEqual` is needed.

### setBlock on Unloaded Chunks

**Decision**: `setBlock()` should **silently no-op** if the target chunk is not loaded. Rationale:
- The caller (world gen, player action) is responsible for ensuring chunks are loaded before mutation
- Auto-loading would hide bugs and create unexpected memory allocation
- Consistent with `getBlock()` returning AIR for unloaded chunks (safe, non-mutating default)
- Add `VX_ASSERT` in debug builds to catch programmer errors

### dirtyChunkCount() Performance Note

Iterating all loaded chunks to count dirty ones is acceptable for now (used for debug/ImGui overlay). If render distance grows large (>32), consider maintaining a dirty set instead. For Story 3.4, simple iteration is fine.

### Project Structure Notes

**New files to create:**
```
engine/include/voxel/world/ChunkManager.h    — ChunkCoordHash + ChunkManager class
engine/src/world/ChunkManager.cpp            — Implementation
tests/world/TestChunkManager.cpp             — Unit tests
```

**Files to modify:**
```
engine/CMakeLists.txt     — Add ChunkManager.cpp to sources, TestChunkManager.cpp to tests
```

**Do NOT create separate files for ChunkCoordHash** — it's a small utility struct, put it in `ChunkManager.h` before the class declaration.

**Coordinate helpers** (`floorDiv`, `euclideanMod`, `worldToChunkCoord`, `worldToLocalPos`): Place as `inline` free functions in `ChunkManager.h` within `namespace voxel::world`. They'll be needed by other systems later (meshing, world gen) — if that happens, extract to a `CoordUtils.h` in a future story. For now, keep collocated.

### Dependencies

**Existing code this story depends on:**
- `ChunkColumn` (Story 3.2) — owns sections, provides getBlock/setBlock, dirty flags
- `ChunkSection` (Story 3.1) — BLOCK_AIR constant, section SIZE constant
- `voxel/core/Assert.h` — VX_ASSERT for debug bounds checking

**No dependency on BlockRegistry** (Story 3.3) — ChunkManager works with raw `uint16_t` block IDs. BlockRegistry is a parallel concern.

**No new vcpkg dependencies needed.**

### Code Patterns from Stories 3.1–3.2

Follow these **exactly**:
- `#pragma once` header guard
- `namespace voxel::world { }` wrapping
- `[[nodiscard]]` on all query methods
- `m_` prefix for member variables
- `constexpr` for compile-time constants (e.g., `SECTION_SIZE = 16`)
- `VX_ASSERT` for debug-mode bounds checking
- Include order: associated header → project → third-party → std
- `using namespace voxel::world;` only in `.cpp` files and tests

### Testing Patterns (match TestChunkColumn.cpp)

```cpp
#include <catch2/catch_test_macros.hpp>
#include "voxel/world/ChunkManager.h"

using namespace voxel::world;

TEST_CASE("ChunkManager: load and unload lifecycle", "[world][chunkmanager]") {
    ChunkManager mgr;
    // Catch2 SECTION blocks for sub-cases
    SECTION("newly created manager has zero chunks") { ... }
    SECTION("loadChunk creates empty column") { ... }
    SECTION("unloadChunk removes column") { ... }
    SECTION("loadChunk is idempotent") { ... }
}
```

Use `[world][chunkmanager]` tags. Do **not** test out-of-bounds Y (VX_ASSERT aborts in debug — Catch2 can't catch that).

### Architecture Compliance

| Rule | How This Story Complies |
|------|------------------------|
| Chunks outside ECS (ADR-004) | ChunkManager is standalone spatial storage, no ECS involvement |
| No exceptions (ADR-008) | VX_ASSERT for invariants; no throwing code paths |
| RAII ownership | unique_ptr<ChunkColumn> in unordered_map — automatic cleanup |
| Main thread owns mutation | ChunkManager methods are NOT thread-safe; called from main thread only |
| One class per file | ChunkManager.h contains ChunkManager + ChunkCoordHash (trivial helper) |
| Max ~500 lines | Target: ~60 lines header, ~120 lines impl, well under limit |

### References

- [Source: _bmad-output/planning-artifacts/architecture.md — Spatial Key Hashing, ChunkManager Interface]
- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.4 spec]
- [Source: engine/include/voxel/world/ChunkColumn.h — API this story delegates to]
- [Source: engine/include/voxel/world/ChunkSection.h — BLOCK_AIR constant, SIZE constant]
- [Source: _bmad-output/implementation-artifacts/3-2-chunkcolumn-vertical-stack-of-sections.md — Previous story patterns]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing strategy]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List