# Story 3.5: Palette Compression Codec

Status: ready-for-dev

## Story

As a **game engine developer**,
I want to **compress chunk sections using palette encoding with variable bits-per-entry**,
so that **serialization (Story 3.6) and distant chunk storage use minimal memory, achieving 2x-30x compression ratios**.

## Acceptance Criteria

1. **`CompressedSection`** struct with: `std::vector<uint16_t> palette`, `std::vector<uint64_t> data` (packed bits), `uint8_t bitsPerEntry`.
2. **`PaletteCompression`** class with static methods: `compress(const ChunkSection&) -> CompressedSection`, `decompress(const CompressedSection&) -> ChunkSection`.
3. **Bits-per-entry tiers**: 0 (single value, uniform section), 1, 2, 4, 8, 16 (direct — no palette).
4. **Automatic tier selection**: `compress()` counts unique block types, selects the smallest tier that fits (see tier table below).
5. **`CompressedSection::memoryUsage()`** returns actual bytes used (palette vector + data vector + bitsPerEntry field).
6. **Roundtrip correctness**: `decompress(compress(section))` produces bit-identical output to the original section for all tier transitions.
7. **Unit tests**: roundtrip identity, single-type = 0 bits, two types = 1 bit, 3-4 types = 2 bits, 5-16 types = 4 bits, 17-256 types = 8 bits, 257+ types = 16 bits direct, memory usage matches expectations.

## Tasks / Subtasks

- [ ] Task 1: Create `CompressedSection` struct (AC: #1, #5)
  - [ ] 1.1 Define struct in `PaletteCompression.h` with `palette`, `data`, `bitsPerEntry`
  - [ ] 1.2 Implement `memoryUsage()` method
- [ ] Task 2: Implement `compress()` (AC: #2, #3, #4)
  - [ ] 2.1 Scan ChunkSection to build unique block set and local palette mapping
  - [ ] 2.2 Select bits-per-entry tier from unique count
  - [ ] 2.3 Handle tier 0 (single value) — store palette only, empty data vector
  - [ ] 2.4 Handle tier 16 (direct) — copy raw `uint16_t` blocks packed into `uint64_t` words
  - [ ] 2.5 Handle tiers 1/2/4/8 — pack palette indices into `uint64_t` words at the chosen bit width
- [ ] Task 3: Implement `decompress()` (AC: #2, #6)
  - [ ] 3.1 Handle tier 0 — fill section with single palette value
  - [ ] 3.2 Handle tier 16 — unpack raw `uint16_t` values from `uint64_t` words
  - [ ] 3.3 Handle tiers 1/2/4/8 — extract palette indices from packed data, map back through palette
- [ ] Task 4: Write unit tests (AC: #7)
  - [ ] 4.1 Roundtrip: random fill → compress → decompress → compare all 4096 blocks
  - [ ] 4.2 Tier 0: uniform section (all stone) → bitsPerEntry == 0, palette size 1
  - [ ] 4.3 Tier 1: two-type section (air + stone) → bitsPerEntry == 1
  - [ ] 4.4 Tier 2: 3 or 4 types → bitsPerEntry == 2
  - [ ] 4.5 Tier 4: 5-16 types → bitsPerEntry == 4
  - [ ] 4.6 Tier 8: 17-256 types → bitsPerEntry == 8
  - [ ] 4.7 Tier 16: 257+ types → bitsPerEntry == 16, palette empty (direct)
  - [ ] 4.8 Memory usage matches expected values from architecture table
  - [ ] 4.9 Edge: empty section (all AIR) → tier 0
  - [ ] 4.10 Edge: every block unique (4096 unique IDs) → tier 16
- [ ] Task 5: Update CMakeLists.txt
  - [ ] 5.1 Add `src/world/PaletteCompression.cpp` to `engine/CMakeLists.txt`
  - [ ] 5.2 Add `world/TestPaletteCompression.cpp` to `tests/CMakeLists.txt`

## Dev Notes

### Palette Compression Tier Table (from architecture.md)

| Unique block types | Bits per entry | `uint64_t` words needed | Memory per section |
|-------------------|----------------|------------------------|--------------------|
| 1 (uniform) | 0 | 0 | ~8 bytes (palette only) |
| 2 | 1 | 64 (4096/64) | ~512 bytes |
| 3-4 | 2 | 128 (4096/32) | ~1 KB |
| 5-16 | 4 | 256 (4096/16) | ~2 KB |
| 17-256 | 8 | 512 (4096/8) | ~4 KB |
| 257+ | 16 (direct) | 1024 (4096/4) | ~8 KB (no palette) |

**Tier selection formula**: find the smallest bits-per-entry `b` such that `2^b >= uniqueCount`, then snap to the next allowed tier `{0, 1, 2, 4, 8, 16}`.

### Bit Packing Strategy

Pack palette indices into `uint64_t` words, little-endian bit order. Each word holds `64 / bitsPerEntry` entries:

```cpp
// Packing (compress):
int entriesPerWord = 64 / bitsPerEntry;
uint64_t mask = (1ULL << bitsPerEntry) - 1;
for (int i = 0; i < VOLUME; ++i) {
    int wordIndex = i / entriesPerWord;
    int bitOffset = (i % entriesPerWord) * bitsPerEntry;
    data[wordIndex] |= (static_cast<uint64_t>(paletteIndex) & mask) << bitOffset;
}

// Unpacking (decompress):
for (int i = 0; i < VOLUME; ++i) {
    int wordIndex = i / entriesPerWord;
    int bitOffset = (i % entriesPerWord) * bitsPerEntry;
    uint16_t paletteIndex = static_cast<uint16_t>((data[wordIndex] >> bitOffset) & mask);
    section.blocks[i] = palette[paletteIndex];
}
```

**For tier 16 (direct, no palette)**: Pack raw `uint16_t` block IDs directly. Each `uint64_t` holds exactly 4 entries at 16 bits each. Palette vector is **empty** — there is no indirection.

**For tier 0 (single value)**: Data vector is **empty**. Palette has exactly 1 entry. Decompress fills entire section with `palette[0]`.

### Data Vector Size Calculation

```cpp
int totalBits = VOLUME * bitsPerEntry;       // 4096 * bitsPerEntry
int wordCount = (totalBits + 63) / 64;       // ceiling division
data.resize(wordCount, 0);                   // zero-initialize
```

### Building the Palette Map

During `compress()`, scan all 4096 blocks to build a mapping from global block ID to local palette index:

```cpp
std::unordered_map<uint16_t, uint16_t> blockToPaletteIndex;
std::vector<uint16_t> palette;

for (int i = 0; i < VOLUME; ++i) {
    uint16_t blockId = section.blocks[i];
    if (!blockToPaletteIndex.contains(blockId)) {
        blockToPaletteIndex[blockId] = static_cast<uint16_t>(palette.size());
        palette.push_back(blockId);
    }
}
uint32_t uniqueCount = static_cast<uint32_t>(palette.size());
```

Then select the tier based on `uniqueCount` and pack using the chosen `bitsPerEntry`.

### Tier Selection Logic

```cpp
uint8_t selectBitsPerEntry(uint32_t uniqueCount) {
    if (uniqueCount <= 1) return 0;
    if (uniqueCount <= 2) return 1;
    if (uniqueCount <= 4) return 2;
    if (uniqueCount <= 16) return 4;
    if (uniqueCount <= 256) return 8;
    return 16;  // direct, no palette
}
```

### Project Structure

**New files to create:**
```
engine/include/voxel/world/PaletteCompression.h    — CompressedSection + PaletteCompression
engine/src/world/PaletteCompression.cpp            — Implementation
tests/world/TestPaletteCompression.cpp             — Unit tests
```

**Files to modify:**
```
engine/CMakeLists.txt     — Add PaletteCompression.cpp to sources
tests/CMakeLists.txt      — Add world/TestPaletteCompression.cpp to tests
```

Both `CompressedSection` and `PaletteCompression` go in the same header — the struct is small and tightly coupled to the compression class. Do NOT create separate files.

### Header Skeleton

```cpp
// PaletteCompression.h
#pragma once

#include "voxel/world/ChunkSection.h"

#include <cstdint>
#include <vector>

namespace voxel::world
{

struct CompressedSection
{
    std::vector<uint16_t> palette;   // localIndex -> globalBlockId
    std::vector<uint64_t> data;      // packed bit entries
    uint8_t bitsPerEntry = 0;        // 0, 1, 2, 4, 8, or 16

    /// Returns actual memory footprint in bytes.
    [[nodiscard]] size_t memoryUsage() const;
};

class PaletteCompression
{
public:
    [[nodiscard]] static CompressedSection compress(const ChunkSection& section);
    [[nodiscard]] static ChunkSection decompress(const CompressedSection& compressed);
};

} // namespace voxel::world
```

### memoryUsage() Implementation

```cpp
size_t CompressedSection::memoryUsage() const
{
    return sizeof(bitsPerEntry)
         + palette.size() * sizeof(uint16_t)
         + data.size() * sizeof(uint64_t);
}
```

This measures payload bytes only (palette entries + packed data + the bitsPerEntry field). Does NOT include `std::vector` overhead (capacity, size pointers) — that's container metadata, not payload.

### Dependencies

**Existing code this story depends on:**
- `ChunkSection` (Story 3.1) — `blocks[4096]` array, `VOLUME` constant, `BLOCK_AIR`
- `voxel/core/Assert.h` — `VX_ASSERT` for debug checks in decompress

**No dependency on:**
- `ChunkColumn` (Story 3.2) — compression operates per-section
- `ChunkManager` (Story 3.4) — compression is a standalone utility
- `BlockRegistry` (Story 3.3) — works with raw `uint16_t` IDs

**No new vcpkg dependencies needed.** Uses only standard library types (`std::vector`, `std::unordered_map`, `<cstdint>`).

### Code Patterns (match Stories 3.1-3.2)

- `#pragma once` header guard
- `namespace voxel::world { }` wrapping
- `[[nodiscard]]` on all query/factory methods
- `m_` prefix not needed — `PaletteCompression` has no members (static-only class)
- `constexpr` for compile-time constants if any are needed
- `VX_ASSERT` for debug-mode invariant checks (e.g., valid bitsPerEntry in decompress)
- Include order: associated header -> project headers -> third-party -> std
- No exceptions — all operations are infallible (compression always succeeds)

### Testing Patterns (match TestChunkColumn.cpp)

```cpp
#include <catch2/catch_test_macros.hpp>
#include "voxel/world/PaletteCompression.h"

using namespace voxel::world;

TEST_CASE("PaletteCompression: roundtrip identity", "[world][palette]") {
    ChunkSection section;
    // ... fill with known pattern ...
    CompressedSection compressed = PaletteCompression::compress(section);
    ChunkSection decompressed = PaletteCompression::decompress(compressed);
    for (int i = 0; i < ChunkSection::VOLUME; ++i) {
        REQUIRE(section.blocks[i] == decompressed.blocks[i]);
    }
}

TEST_CASE("PaletteCompression: tier selection", "[world][palette]") {
    SECTION("uniform section -> tier 0") { /* fill all with stone, verify bitsPerEntry == 0 */ }
    SECTION("two types -> tier 1") { /* ... */ }
    SECTION("3-4 types -> tier 2") { /* ... */ }
    // etc.
}
```

Use `[world][palette]` tags. Compare all 4096 blocks in roundtrip tests to catch off-by-one bit packing errors.

### Memory Usage Validation in Tests

Expected values to assert:
| Scenario | bitsPerEntry | palette size | data words | memoryUsage() |
|----------|-------------|-------------|------------|---------------|
| All stone (1 type) | 0 | 1 | 0 | 1 + 2 + 0 = 3 bytes |
| Air + stone (2 types) | 1 | 2 | 64 | 1 + 4 + 512 = 517 bytes |
| 4 types | 2 | 4 | 128 | 1 + 8 + 1024 = 1033 bytes |
| 16 types | 4 | 16 | 256 | 1 + 32 + 2048 = 2081 bytes |
| 256 types | 8 | 256 | 512 | 1 + 512 + 4096 = 4609 bytes |
| 4096 unique (direct) | 16 | 0 | 1024 | 1 + 0 + 8192 = 8193 bytes |

### Architecture Compliance

| Rule | How This Story Complies |
|------|------------------------|
| Chunks outside ECS (ADR-004) | Operates on `ChunkSection` data, no ECS involvement |
| No exceptions (ADR-008) | All operations are infallible; `VX_ASSERT` for invariants |
| One class per file | `PaletteCompression.h` contains `CompressedSection` struct + `PaletteCompression` class |
| Max ~500 lines | Target: ~40 lines header, ~100 lines impl, well under limit |
| RAII ownership | `std::vector` manages palette/data memory automatically |

### What NOT to Do

- Do NOT implement RLE or LZ4 compression — that is Story 3.6 (Chunk Serialization)
- Do NOT add disk I/O — this is a pure in-memory codec
- Do NOT make this thread-safe — compress/decompress are pure functions operating on immutable input
- Do NOT store `CompressedSection` inside `ChunkSection` or `ChunkColumn` — this is a standalone utility, not embedded state
- Do NOT add a "resize/transition" mechanism for live palette growth — this is a snapshot codec (compress the full section at once), not a live-editing data structure. Story 3.6 will call `compress()` when saving
- Do NOT use `std::bitset` — `std::vector<uint64_t>` with manual bit manipulation is more flexible and matches architecture spec
- Do NOT use exceptions or `Result<T>` — compression cannot fail

### Previous Story Intelligence

**From Story 3.2 (ChunkColumn):**
- Zero issues on implementation; build and tests passed on first attempt
- 17 Catch2 SECTION-based test cases — comprehensive coverage pattern to follow
- `ChunkSection` `blocks[]` array is public, can be accessed directly for compression

**From Story 3.1 (ChunkSection):**
- `ChunkSection::VOLUME = 4096` and `ChunkSection::SIZE = 16` are `static constexpr`
- Constructor fills with `BLOCK_AIR` — important for decompress: newly created section starts all-air
- `blocks[]` is a raw `uint16_t` array (not `std::array`) — direct index access

**From Story 3.4 (ChunkManager — ready-for-dev):**
- Coordinate helpers and hash function patterns established
- Compression is independent from ChunkManager — no coupling needed

### Git Intelligence

Recent commit patterns:
- `feat(world): implement ChunkColumn for vertical voxel storage with unit tests` — single commit per story
- `feat(world): add ChunkSection flat storage and tests` — same pattern
- Commit message format: `feat(world): <description>` for new world features

### References

- [Source: _bmad-output/planning-artifacts/architecture.md — Palette Compression table, lines 146-159]
- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.5 acceptance criteria]
- [Source: engine/include/voxel/world/ChunkSection.h — blocks[] array, VOLUME constant]
- [Source: _bmad-output/implementation-artifacts/3-2-chunkcolumn-vertical-stack-of-sections.md — Code patterns, test patterns]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing strategy]
- [Source: engine/CMakeLists.txt — Source registration pattern]
- [Source: tests/CMakeLists.txt — Test registration pattern]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
