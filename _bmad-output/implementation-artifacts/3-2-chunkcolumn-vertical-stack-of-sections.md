# Story 3.2: ChunkColumn (Vertical Stack of Sections)

Status: done

## Story

As a developer,
I want a vertical stack of 16 sections representing a full chunk column (256 blocks tall),
so that the world has vertical extent.

## Acceptance Criteria

1. `ChunkColumn` class owns an array of 16 `std::unique_ptr<ChunkSection>` (null = empty sky)
2. `getSection(int sectionY) → ChunkSection*` returns null if section not yet allocated
3. `getOrCreateSection(int sectionY) → ChunkSection&` allocates on first write
4. `getBlock(int x, int y, int z) → uint16_t` translates world-local y to section index + local y; returns `BLOCK_AIR` if section is null
5. `setBlock(int x, int y, int z, uint16_t id)` auto-allocates section if needed, sets per-section dirty flag
6. `chunkCoord` member: `glm::ivec2` (x, z world chunk coordinate)
7. `isDirty` flag per section — set on `setBlock`, cleared after remesh via `clearDirty(int sectionY)`
8. Unit tests: cross-section boundary access (y=15→16), null section returns AIR, setBlock triggers allocation, dirty flag behavior

## Tasks / Subtasks

- [x] Task 1: Create `ChunkColumn.h` header (AC: 1, 2, 3, 4, 5, 6, 7)
  - [x] Define `ChunkColumn` class in `voxel::world` namespace
  - [x] Add constants: `SECTIONS_PER_COLUMN = 16`, `COLUMN_HEIGHT = 256`
  - [x] `std::array<std::unique_ptr<ChunkSection>, 16> m_sections`
  - [x] `std::array<bool, SECTIONS_PER_COLUMN> m_dirty` (or `uint16_t` bitmask)
  - [x] `glm::ivec2 m_chunkCoord`
  - [x] Public API declarations
- [x] Task 2: Implement `ChunkColumn.cpp` (AC: 1–7)
  - [x] Constructor takes `glm::ivec2 chunkCoord`, zero-initializes dirty flags
  - [x] `getSection(int sectionY)` — bounds-checked, returns raw pointer (null if not allocated)
  - [x] `getOrCreateSection(int sectionY)` — allocates via `std::make_unique<ChunkSection>()` if null
  - [x] `getBlock(int x, int y, int z)` — decompose y: `sectionIndex = y / 16`, `localY = y % 16`
  - [x] `setBlock(int x, int y, int z, uint16_t id)` — calls `getOrCreateSection`, delegates to `ChunkSection::setBlock`, sets dirty
  - [x] `clearDirty(int sectionY)` and `isSectionDirty(int sectionY)`
  - [x] `isAllEmpty()` — true if all 16 sections are null or empty
  - [x] `getHighestNonEmptySection()` — returns highest section index with non-null non-empty section (-1 if all empty)
- [x] Task 3: Register in CMake (AC: all)
  - [x] Add `src/world/ChunkColumn.cpp` to `engine/CMakeLists.txt`
- [x] Task 4: Write unit tests `tests/world/TestChunkColumn.cpp` (AC: 8)
  - [x] Test: default construction — all sections null, all dirty flags false
  - [x] Test: getBlock on null section returns `BLOCK_AIR`
  - [x] Test: setBlock auto-allocates section
  - [x] Test: get/set roundtrip at various Y levels
  - [x] Test: cross-section boundary (y=15 and y=16 land in different sections)
  - [x] Test: dirty flag set on setBlock, cleared by clearDirty
  - [x] Test: getOrCreateSection idempotent (second call returns same section)
  - [x] Test: bounds (y=0 and y=255 valid, sectionY=0 and sectionY=15 valid)
  - [x] Test: getHighestNonEmptySection correctness
  - [x] Register test in `tests/CMakeLists.txt`
- [x] Task 5: Verify build and all tests pass

## Dev Notes

### Architecture Constraints

- **ADR-004**: Chunks are NOT ECS entities. `ChunkColumn` is standalone storage, will be owned by `ChunkManager` (Story 3.4) via `std::unique_ptr` in an `unordered_map<glm::ivec2, unique_ptr<ChunkColumn>, ChunkCoordHash>`.
- **ADR-008**: No exceptions. Use `VX_ASSERT` for debug bounds checking (stripped in release).
- **Null sections = lazy allocation**: Unwritten sections stay null (saves memory for sky). `getBlock` on a null section returns `BLOCK_AIR` without allocating. Only `setBlock` and `getOrCreateSection` allocate.
- **Dirty flags**: Per-section granularity. Meshing system (Epic 5) will query dirty flags to know which sections need remesh. After meshing, it calls `clearDirty(sectionY)`.
- **Thread safety**: Not required here. Per architecture, worker threads operate on immutable snapshots, not live `ChunkColumn` objects.
- **Memory**: Each allocated section = 8 KB. Full column (all 16 sections) = 128 KB. Empty column = ~128 bytes (pointers + flags + coord).

### Coordinate Decomposition

World-local Y coordinate [0, 255] decomposes to:
```cpp
int sectionIndex = y / ChunkSection::SIZE;  // y >> 4
int localY       = y % ChunkSection::SIZE;  // y & 0xF
```
x and z pass through directly [0, 15] — they are already section-local.

Bounds:
- `x` ∈ [0, 15], `z` ∈ [0, 15] (same as ChunkSection)
- `y` ∈ [0, 255] (spans all 16 sections)
- `sectionY` ∈ [0, 15] (section index within column)

### File Locations

| File | Path |
|------|------|
| Header | `engine/include/voxel/world/ChunkColumn.h` |
| Source | `engine/src/world/ChunkColumn.cpp` |
| Tests | `tests/world/TestChunkColumn.cpp` |

Mirror structure matches Story 3.1 (`ChunkSection.h/cpp`, `TestChunkSection.cpp`).

### Dependencies

- `ChunkSection` from Story 3.1 — `#include "voxel/world/ChunkSection.h"`
- `glm::ivec2` — `#include <glm/vec2.hpp>`
- `VX_ASSERT` — `#include "voxel/core/Assert.h"`
- `std::unique_ptr`, `std::array` — standard library

### Code Patterns from Story 3.1

Follow the exact patterns established in `ChunkSection`:
- `#pragma once` header guard
- Namespace `voxel::world`
- `[[nodiscard]]` on all const query methods
- `VX_ASSERT` with descriptive message strings for bounds checks
- No `auto` for return types — explicit types
- Include order: associated header → project headers → third-party → standard library
- Test file uses `catch2/catch_test_macros.hpp` with `SECTION`-based organization

### Class Skeleton

```cpp
// ChunkColumn.h
#pragma once

#include "voxel/world/ChunkSection.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <memory>

namespace voxel::world
{

class ChunkColumn
{
public:
    static constexpr int32_t SECTIONS_PER_COLUMN = 16;
    static constexpr int32_t COLUMN_HEIGHT = SECTIONS_PER_COLUMN * ChunkSection::SIZE; // 256

    explicit ChunkColumn(glm::ivec2 chunkCoord);

    [[nodiscard]] glm::ivec2 getChunkCoord() const;

    // Section access
    [[nodiscard]] ChunkSection* getSection(int sectionY);
    [[nodiscard]] const ChunkSection* getSection(int sectionY) const;
    ChunkSection& getOrCreateSection(int sectionY);

    // Block access (y spans full column [0, 255])
    [[nodiscard]] uint16_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint16_t id);

    // Dirty tracking
    [[nodiscard]] bool isSectionDirty(int sectionY) const;
    void clearDirty(int sectionY);

    // Queries
    [[nodiscard]] bool isAllEmpty() const;
    [[nodiscard]] int getHighestNonEmptySection() const;

private:
    glm::ivec2 m_chunkCoord;
    std::array<std::unique_ptr<ChunkSection>, SECTIONS_PER_COLUMN> m_sections;
    std::array<bool, SECTIONS_PER_COLUMN> m_dirty;
};

} // namespace voxel::world
```

### Testing Guidance

Use Catch2 v3 SECTION-based pattern (same as `TestChunkSection.cpp`):

```cpp
TEST_CASE("ChunkColumn", "[world]")
{
    ChunkColumn column({4, -7});

    SECTION("default construction") { /* all sections null, no dirty flags */ }
    SECTION("getBlock on null section returns AIR") { /* ... */ }
    SECTION("setBlock auto-allocates section") { /* ... */ }
    SECTION("cross-section boundary y=15 and y=16") { /* ... */ }
    SECTION("dirty flag lifecycle") { /* ... */ }
    // etc.
}
```

Key test scenarios:
- **Cross-section boundary**: Set block at y=15 (section 0, localY=15) and y=16 (section 1, localY=0). Verify different sections, both accessible.
- **Null section safety**: `getBlock(x, 100, z)` returns `BLOCK_AIR` without allocating section 6.
- **Allocation idempotency**: `getOrCreateSection(5)` twice returns same pointer.
- **Dirty isolation**: Setting block in section 3 only dirties section 3, not others.
- **Bounds extremes**: y=0 (section 0, localY 0) and y=255 (section 15, localY 15).
- **getHighestNonEmptySection**: Returns -1 for empty column, correct index after writes.

### CMake Integration

Add to `engine/CMakeLists.txt` source list (alongside existing `ChunkSection.cpp`):
```cmake
src/world/ChunkColumn.cpp
```

Add test to `tests/CMakeLists.txt` (alongside existing `TestChunkSection.cpp`):
```cmake
world/TestChunkColumn.cpp
```

### Project Structure Notes

- Aligns with existing `engine/include/voxel/world/` and `engine/src/world/` directories created in Story 3.1
- No new directories needed — files slot into established structure
- `ChunkColumn` is the second class in the `voxel::world` module, following `ChunkSection`

### What NOT to Do

- Do NOT add thread synchronization (mutexes, atomics) — not needed per architecture
- Do NOT implement palette compression — that is Story 3.5
- Do NOT implement serialization — that is Story 3.6
- Do NOT add ChunkManager or spatial hashing — that is Story 3.4
- Do NOT add terrain generation — that is Epic 4
- Do NOT add meshing concerns — that is Epic 5
- Do NOT use `std::shared_ptr` — `unique_ptr` is the correct ownership model
- Do NOT use exceptions or `Result<T>` for bounds errors — use `VX_ASSERT` only
- Do NOT add `using namespace` in the header file

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.2]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 1: Chunk Storage]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-004: Voxel Data Outside ECS]
- [Source: _bmad-output/project-context.md — Memory & Ownership, Error Handling, Naming Conventions]
- [Source: engine/include/voxel/world/ChunkSection.h — Established patterns]
- [Source: tests/world/TestChunkSection.cpp — Test patterns]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
No issues encountered. Build and all tests pass on first attempt.

### Completion Notes List
- Implemented `ChunkColumn` class following the exact skeleton from the story spec and patterns from `ChunkSection` (Story 3.1)
- Header uses `#pragma once`, `[[nodiscard]]`, `VX_ASSERT` bounds checks — matching established conventions
- Lazy section allocation: `getBlock` returns `BLOCK_AIR` for null sections without allocating; only `setBlock` and `getOrCreateSection` allocate
- Per-section dirty flags via `std::array<bool, 16>` — set on `setBlock`, cleared by `clearDirty`
- Coordinate decomposition: `sectionIndex = y / 16`, `localY = y % 16` for translating column-level y [0,255] to section-level
- 17 Catch2 SECTION-based test cases covering all 8 acceptance criteria: null sections, auto-allocation, cross-section boundary, dirty flag isolation, bounds extremes, `getHighestNonEmptySection`, `isAllEmpty`, constants
- CMake integration: source added to `engine/CMakeLists.txt`, test added to `tests/CMakeLists.txt`

### Change Log
- 2026-03-25: Story 3.2 implemented — ChunkColumn class, unit tests, CMake registration

### File List
- `engine/include/voxel/world/ChunkColumn.h` (new)
- `engine/src/world/ChunkColumn.cpp` (new)
- `tests/world/TestChunkColumn.cpp` (new)
- `engine/CMakeLists.txt` (modified — added ChunkColumn.cpp)
- `tests/CMakeLists.txt` (modified — added TestChunkColumn.cpp)
- `engine/include/voxel/world/ChunkSection.h` (modified — removed unused `#include <algorithm>`)
- `tests/world/TestChunkSection.cpp` (modified — added comment re: VX_ASSERT death-test limitation)
- `_bmad-output/implementation-artifacts/sprint-status.yaml` (modified — 3.2 status update)
