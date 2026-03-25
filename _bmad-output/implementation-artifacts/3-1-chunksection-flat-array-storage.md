# Story 3.1: ChunkSection Flat Array Storage

Status: ready-for-dev

## Story

As a developer,
I want a 16³ block storage unit with fast get/set access,
so that chunks can store voxel data efficiently.

## Acceptance Criteria

1. `ChunkSection` struct in namespace `voxel::world` with `uint16_t blocks[4096]`, indexed `y*256 + z*16 + x`
2. `getBlock(int x, int y, int z) → uint16_t` with `VX_ASSERT` bounds check in debug
3. `setBlock(int x, int y, int z, uint16_t id)` with `VX_ASSERT` bounds check in debug
4. `fill(uint16_t id)` fills entire section with given block ID
5. `isEmpty() → bool` returns true if all blocks are `BLOCK_AIR` (ID 0)
6. `countNonAir() → int32_t` returns count of non-air blocks
7. Constructor zero-initializes all blocks to `BLOCK_AIR` (0)
8. Unit tests: get/set roundtrip, fill, boundary values (0,0,0 and 15,15,15), out-of-bounds assert fires in debug, isEmpty, countNonAir

## Tasks / Subtasks

- [ ] Task 1: Create `ChunkSection` header (AC: #1, #2, #3, #4, #5, #6, #7)
  - [ ] Create `engine/include/voxel/world/ChunkSection.h`
  - [ ] Define `BLOCK_AIR` constant (constexpr uint16_t = 0)
  - [ ] Define `ChunkSection` struct with `SIZE = 16`, `VOLUME = 4096`
  - [ ] Implement `toIndex(int x, int y, int z)` private/static helper: `y * 256 + z * 16 + x`
  - [ ] Implement `getBlock`, `setBlock`, `fill`, `isEmpty`, `countNonAir`
  - [ ] Zero-initialize `blocks` array in default constructor
- [ ] Task 2: Create `ChunkSection` implementation (AC: #1–#7)
  - [ ] Create `engine/src/world/ChunkSection.cpp`
  - [ ] Implement all methods (move non-trivial logic out of header)
  - [ ] Use `std::memset` or `std::fill` for `fill()`
  - [ ] Use `std::all_of` or loop for `isEmpty()`
  - [ ] Use `std::count_if` or loop with `std::accumulate` for `countNonAir()`
- [ ] Task 3: Register in CMake (AC: all)
  - [ ] Add `src/world/ChunkSection.cpp` to `engine/CMakeLists.txt` source list
  - [ ] Add `tests/world/TestChunkSection.cpp` to `tests/CMakeLists.txt`
- [ ] Task 4: Unit tests (AC: #8)
  - [ ] Create `tests/world/TestChunkSection.cpp`
  - [ ] Test: default construction → all blocks are AIR
  - [ ] Test: get/set roundtrip at multiple positions
  - [ ] Test: boundary corners (0,0,0) and (15,15,15)
  - [ ] Test: fill sets all blocks to target ID
  - [ ] Test: isEmpty returns true for fresh section, false after setBlock
  - [ ] Test: countNonAir accuracy after various operations
  - [ ] Test: index calculation correctness (y*256 + z*16 + x)

## Dev Notes

### Architecture Constraints

- **Namespace**: `voxel::world` — all world storage types live here
- **ADR-004**: Chunks are NOT ECS entities. ChunkSection is standalone storage, managed by ChunkManager (Story 3.4)
- **ADR-008**: No exceptions. Use `VX_ASSERT` for bounds checking (debug only). No Result<T> needed here — get/set are infallible for valid coordinates
- **Indexing formula**: `y * 256 + z * 16 + x` — this is Y-major for cache-friendly meshing iteration (meshing sweeps along Y slices). Do NOT change this formula
- **Data type**: `uint16_t` supports 65,535 block types via BlockRegistry (Story 3.3)
- **Memory**: 8 KB per section (4096 × 2 bytes). This is the runtime format — palette compression (Story 3.5) handles serialized format

### Implementation Guidance

**Header structure** — keep it simple, this is a POD-like struct:
```cpp
#pragma once
#include "voxel/core/Types.h"
#include "voxel/core/Assert.h"
#include <cstdint>
#include <algorithm>

namespace voxel::world {

constexpr uint16_t BLOCK_AIR = 0;

struct ChunkSection {
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE; // 4096

    uint16_t blocks[VOLUME];

    ChunkSection();

    [[nodiscard]] uint16_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint16_t id);
    void fill(uint16_t id);
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] int32_t countNonAir() const;

private:
    [[nodiscard]] static constexpr int toIndex(int x, int y, int z) {
        return y * 256 + z * 16 + x;
    }
};

} // namespace voxel::world
```

**Bounds checking** — use `VX_ASSERT` (stripped in Release for zero overhead):
```cpp
uint16_t ChunkSection::getBlock(int x, int y, int z) const {
    VX_ASSERT(x >= 0 && x < SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < SIZE, "y out of bounds");
    VX_ASSERT(z >= 0 && z < SIZE, "z out of bounds");
    return blocks[toIndex(x, y, z)];
}
```

**Constructor** — zero-initialize:
```cpp
ChunkSection::ChunkSection() {
    std::fill(std::begin(blocks), std::end(blocks), BLOCK_AIR);
}
```

**countNonAir** — use `std::count_if`:
```cpp
int32_t ChunkSection::countNonAir() const {
    return static_cast<int32_t>(
        std::count_if(std::begin(blocks), std::end(blocks),
                      [](uint16_t b) { return b != BLOCK_AIR; }));
}
```

### Existing Code Patterns to Follow

- **Factory pattern**: Not needed here (simple struct with no resource ownership)
- **Include order**: Associated header → project (`voxel/core/Assert.h`) → third-party → standard (`<algorithm>`, `<cstdint>`)
- **File naming**: PascalCase — `ChunkSection.h` / `ChunkSection.cpp`
- **[[nodiscard]]**: Apply to all const query methods (getBlock, isEmpty, countNonAir)
- **constexpr**: Use for SIZE, VOLUME, BLOCK_AIR, and toIndex helper

### Testing Pattern — Follow Established Catch2 v3 Style

Reference: `tests/renderer/TestGigabuffer.cpp`, `tests/renderer/TestStagingBuffer.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "voxel/world/ChunkSection.h"

using namespace voxel::world;

TEST_CASE("ChunkSection", "[world]") {
    ChunkSection section;

    SECTION("default construction fills with AIR") {
        REQUIRE(section.getBlock(0, 0, 0) == BLOCK_AIR);
        REQUIRE(section.getBlock(15, 15, 15) == BLOCK_AIR);
        REQUIRE(section.isEmpty());
        REQUIRE(section.countNonAir() == 0);
    }

    SECTION("get/set roundtrip") {
        section.setBlock(5, 8, 3, 42);
        REQUIRE(section.getBlock(5, 8, 3) == 42);
    }

    SECTION("boundary corners") {
        section.setBlock(0, 0, 0, 1);
        REQUIRE(section.getBlock(0, 0, 0) == 1);
        section.setBlock(15, 15, 15, 2);
        REQUIRE(section.getBlock(15, 15, 15) == 2);
    }

    SECTION("fill") {
        section.fill(7);
        REQUIRE(section.getBlock(0, 0, 0) == 7);
        REQUIRE(section.getBlock(15, 15, 15) == 7);
        REQUIRE_FALSE(section.isEmpty());
        REQUIRE(section.countNonAir() == ChunkSection::VOLUME);
    }

    SECTION("isEmpty and countNonAir") {
        REQUIRE(section.isEmpty());
        section.setBlock(3, 7, 11, 1);
        REQUIRE_FALSE(section.isEmpty());
        REQUIRE(section.countNonAir() == 1);
    }

    SECTION("index correctness y*256 + z*16 + x") {
        // Set block at (x=3, y=5, z=7) → index = 5*256 + 7*16 + 3 = 1395
        section.setBlock(3, 5, 7, 99);
        REQUIRE(section.blocks[1395] == 99);
    }
}
```

### Directory Creation

The `engine/include/voxel/world/` and `engine/src/world/` directories do NOT exist yet. Create them:
- `engine/include/voxel/world/ChunkSection.h`
- `engine/src/world/ChunkSection.cpp`
- `tests/world/TestChunkSection.cpp`

### Project Structure Notes

- First file in `voxel::world` namespace — establishes directory structure for Stories 3.2–3.6
- Aligns with architecture plan: `engine/include/voxel/world/` for all world types
- Mirror structure: header in `include/voxel/world/`, implementation in `src/world/`
- Tests in `tests/world/` (new directory)

### CMake Integration

Add to `engine/CMakeLists.txt` source list:
```cmake
src/world/ChunkSection.cpp
```

Add to `tests/CMakeLists.txt` source list:
```cmake
world/TestChunkSection.cpp
```

### What NOT To Do

- Do NOT use `std::array<uint16_t, 4096>` — use raw C array `uint16_t blocks[VOLUME]` per architecture spec (direct memory layout control, no wrapper overhead)
- Do NOT add palette compression — that's Story 3.5
- Do NOT add dirty flags — that's Story 3.2 (ChunkColumn)
- Do NOT create BlockRegistry or BLOCK_STONE constants — that's Story 3.3
- Do NOT throw exceptions for out-of-bounds — use `VX_ASSERT` only (stripped in release, per ADR-008)
- Do NOT add thread safety — ChunkSection is accessed via immutable snapshots in worker threads (threading concern is ChunkManager's, Story 3.4)
- Do NOT use `new`/`delete` — ChunkSection is value-type, owned by ChunkColumn (Story 3.2) via `unique_ptr`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.1]
- [Source: _bmad-output/planning-artifacts/architecture.md — Section 4: World Storage, ADR-004, ADR-008]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Code Organization]
- [Source: _bmad-output/planning-artifacts/PRD.md — FR-1: Infinite Voxel World]
- [Source: _bmad-output/planning-artifacts/technical-research.md — Flat Array Performance]

### Git Intelligence

Recent commits show established patterns:
- `feat(renderer): implement Gigabuffer...` — factory pattern, Result<T>, RAII
- `feat(renderer): implement StagingBuffer...` — per-frame state, Vulkan sync
- Commit format: `feat(world): implement ChunkSection flat array storage with unit tests`

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List