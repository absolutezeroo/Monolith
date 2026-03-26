# Story 3.6: Chunk Serialization (Disk I/O)

Status: review

## Story

As a **game engine developer**,
I want to **save and load chunks to/from disk using region files with palette compression and LZ4**,
so that **the world persists between sessions and modified chunks are not lost on unload**.

## Acceptance Criteria

1. **Region file format**: one file per 32×32 chunk region, 8-byte indexed header per chunk (uint32_t offset + uint32_t size) for random access. Filename: `r.{rx}.{rz}.vxr`.
2. **`ChunkSerializer::save(const ChunkColumn&, const BlockRegistry&, const fs::path& regionDir)`** — palette-compresses each non-null section, serializes with string IDs for persistence, LZ4-compresses the result, writes to region file.
3. **`ChunkSerializer::load(glm::ivec2 coord, const BlockRegistry&, const fs::path& regionDir) → Result<ChunkColumn>`** — reads from region file, LZ4-decompresses, deserializes string IDs back to current session numeric IDs, palette-decompresses sections.
4. **String ID persistence**: palette entries stored as string IDs (via `BlockRegistry::getBlock(id).stringId`) on disk, resolved back to current numeric IDs (via `BlockRegistry::getIdByName()`) on load. This ensures world saves survive block ID reassignment across sessions.
5. **Graceful missing-file handling**: returns `EngineError::FileNotFound` if region file doesn't exist (caller triggers world generation). Returns `EngineError::ChunkNotLoaded` if chunk has no entry in region header (offset=0, size=0).
6. **Corrupt data handling**: returns `EngineError::InvalidFormat` if LZ4 decompression fails, section data is truncated, or string IDs are unrecognized (log warning, substitute `BLOCK_AIR`).
7. **Empty chunk optimization**: null sections (all air) are NOT serialized — a section bitmask tracks which sections have data.
8. **Unit tests**: save/load roundtrip identity, corrupt file handling, empty chunk serialization, missing file returns correct error, cross-session ID remapping.

## Tasks / Subtasks

- [x] Task 1: Create `RegionFile` helper class (AC: #1, #5, #6)
  - [x] 1.1 Define region constants (REGION_SIZE=32, HEADER_ENTRIES=1024, HEADER_BYTES=8192)
  - [x] 1.2 Implement `RegionFile::writeChunk(localIndex, span<const uint8_t> data)`
  - [x] 1.3 Implement `RegionFile::readChunk(localIndex) → Result<vector<uint8_t>>`
  - [x] 1.4 Implement header read/write with proper offset bookkeeping
- [x] Task 2: Create `ChunkSerializer` with binary serialization (AC: #2, #4, #7)
  - [x] 2.1 Implement `serializeColumn(const ChunkColumn&, const BlockRegistry&) → vector<uint8_t>` (pre-LZ4 binary)
  - [x] 2.2 Implement per-section binary format: bitsPerEntry + string palette + packed data
  - [x] 2.3 Implement section bitmask for null-section skipping
- [x] Task 3: Implement `ChunkSerializer::save()` (AC: #2)
  - [x] 3.1 Palette-compress each non-null section via `PaletteCompression::compress()`
  - [x] 3.2 Serialize to binary, LZ4-compress, write via `RegionFile`
- [x] Task 4: Implement deserialization and `ChunkSerializer::load()` (AC: #3, #4, #6)
  - [x] 4.1 Implement `deserializeColumn(span<const uint8_t>, const BlockRegistry&) → Result<ChunkColumn>`
  - [x] 4.2 Resolve string IDs → numeric IDs, substitute BLOCK_AIR for unknown blocks
  - [x] 4.3 LZ4-decompress, deserialize, palette-decompress
- [x] Task 5: Write unit tests (AC: #8)
  - [x] 5.1 Roundtrip test: fill chunk → save → load → verify block-identical
  - [x] 5.2 Empty chunk test: all-air column serializes minimally and roundtrips
  - [x] 5.3 Corrupt data test: truncated/garbage data returns InvalidFormat
  - [x] 5.4 Missing file test: non-existent region returns FileNotFound
  - [x] 5.5 Missing chunk test: valid region but unwritten chunk returns ChunkNotLoaded
  - [x] 5.6 ID remapping test: save with IDs [0,1,2], load with different numeric IDs for same string names
- [x] Task 6: Update CMakeLists.txt (link LZ4, add source/test files)

## Dev Notes

### Dependency: Story 3.5 (PaletteCompression) MUST be implemented first

This story consumes `PaletteCompression::compress()` and `PaletteCompression::decompress()` from Story 3.5. Those files do NOT exist yet. If Story 3.5 is not done, implement it first or stub the interface.

**Expected files from 3.5:**
- `engine/include/voxel/world/PaletteCompression.h` — `CompressedSection` struct + `PaletteCompression` class
- `engine/src/world/PaletteCompression.cpp`

**CompressedSection struct (from Story 3.5 spec):**
```cpp
struct CompressedSection {
    std::vector<uint16_t> palette;   // localIndex -> globalBlockId
    std::vector<uint64_t> data;      // packed bit entries
    uint8_t bitsPerEntry = 0;        // 0, 1, 2, 4, 8, or 16
    [[nodiscard]] size_t memoryUsage() const;
};
```

### Region File Binary Format

```
REGION FILE LAYOUT (r.{rx}.{rz}.vxr):
┌─────────────────────────────────────────────┐
│ Header: 1024 entries × 8 bytes = 8192 bytes │
│   [uint32_t offset][uint32_t size] per chunk │
│   offset=0, size=0 means chunk not saved     │
├─────────────────────────────────────────────┤
│ Data: concatenated LZ4-compressed blobs      │
│   Each blob = one ChunkColumn's data         │
└─────────────────────────────────────────────┘

Chunk index in header: (localX * REGION_SIZE + localZ)
  where localX = euclideanMod(chunkX, 32)
        localZ = euclideanMod(chunkZ, 32)

Region coords: rx = floorDiv(chunkX, 32), rz = floorDiv(chunkZ, 32)
```

### Per-ChunkColumn Binary Format (before LZ4 compression)

```
[uint16_t] sectionBitmask    — bit N set = section N is non-null
For each non-null section (ordered by section index 0..15):
    [uint8_t]  bitsPerEntry
    [uint16_t] paletteSize   — number of string ID entries
    For each palette entry:
        [uint16_t] stringLength
        [char[]]   stringId (UTF-8, NOT null-terminated)
    [uint32_t] dataWordCount — number of uint64_t words
    [uint64_t[]] packed data words (little-endian bit order)
```

**Why string IDs on disk**: numeric IDs are session-assigned by `BlockRegistry` and may differ across sessions (e.g., mod load order changes). String IDs (like `"base:stone"`) are the stable persistent identity. The palette in `CompressedSection` stores numeric IDs; on save, convert each via `BlockRegistry::getBlock(numericId).stringId`. On load, convert back via `BlockRegistry::getIdByName(stringId)`.

### LZ4 Compression API

```cpp
#include <lz4.h>

// Compress:
int maxDstSize = LZ4_compressBound(srcSize);
std::vector<uint8_t> compressed(maxDstSize);
int compressedSize = LZ4_compress_default(
    reinterpret_cast<const char*>(src.data()),
    reinterpret_cast<char*>(compressed.data()),
    static_cast<int>(srcSize),
    maxDstSize
);
compressed.resize(compressedSize);

// Decompress (need to store uncompressed size alongside compressed data):
// Prepend uint32_t uncompressedSize before the LZ4 blob in the region file entry.
int result = LZ4_decompress_safe(
    reinterpret_cast<const char*>(compressedData),
    reinterpret_cast<char*>(output.data()),
    compressedSize,
    uncompressedSize
);
if (result < 0) → return EngineError::InvalidFormat
```

**Important**: LZ4 decompression needs to know the uncompressed size. Store it as a `uint32_t` prefix before the LZ4 blob in the region file data section:
```
Region data entry = [uint32_t uncompressedSize][LZ4 compressed bytes...]
```

### Coordinate Translation (reuse from ChunkManager.h)

```cpp
// Already implemented in ChunkManager.h as inline free functions:
inline int floorDiv(int a, int b);
inline int euclideanMod(int a, int b);

// Region coordinate:
glm::ivec2 chunkToRegionCoord(glm::ivec2 chunkCoord) {
    return {floorDiv(chunkCoord.x, 32), floorDiv(chunkCoord.y, 32)};
}

// Local index within region:
int chunkToRegionIndex(glm::ivec2 chunkCoord) {
    int localX = euclideanMod(chunkCoord.x, 32);
    int localZ = euclideanMod(chunkCoord.y, 32); // ivec2.y = chunkZ
    return localX * 32 + localZ;
}
```

**Reuse `floorDiv` and `euclideanMod` from `ChunkManager.h`** — do NOT reimplement. Consider moving them to a shared utility header (e.g., `voxel/math/CoordUtils.h`) if they aren't already, or just include `ChunkManager.h`.

### File I/O Pattern

Use `std::fstream` with binary mode. Do NOT use C `fopen`/`fwrite`. Use RAII file handles.

```cpp
std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
if (!file.is_open()) {
    // For save: create new file, write empty header
    // For load: return EngineError::FileNotFound
}
```

**Creating new region files**: write 8192 zero bytes as empty header, then start appending chunk data. When updating an existing chunk, append new data at end of file and update header offset/size (do NOT attempt in-place overwrite of variable-size data).

### Error Handling

| Scenario | Error | Behavior |
|----------|-------|----------|
| Region file missing (load) | `FileNotFound` | Caller triggers world generation |
| Chunk not in region (load) | `ChunkNotLoaded` | Header entry offset=0, size=0 |
| LZ4 decompress fails | `InvalidFormat` | Corrupt or truncated data |
| Unknown string ID on load | N/A (not an error) | Log warning, substitute `BLOCK_AIR` |
| Disk write failure | `InvalidFormat` | fstream write check fails |

### Existing EngineError Variants (from `voxel/core/Result.h`)

```cpp
enum class EngineError : uint8 {
    FileNotFound,    // ← use for missing region file
    InvalidFormat,   // ← use for corrupt data / LZ4 failure
    ShaderCompileError,
    VulkanError,
    ChunkNotLoaded,  // ← use for chunk not in region header
    OutOfMemory,
    InvalidArgument,
    ScriptError
};
```

All needed error codes already exist — no changes to Result.h required.

### Header Skeleton

```cpp
// engine/include/voxel/world/ChunkSerializer.h
#pragma once

#include "voxel/core/Result.h"
#include "voxel/world/ChunkColumn.h"

#include <filesystem>

namespace voxel::world
{

class BlockRegistry; // forward declare

class ChunkSerializer
{
public:
    /// Save a chunk column to the appropriate region file.
    /// Creates the region file if it doesn't exist.
    static core::Result<void> save(
        const ChunkColumn& column,
        const BlockRegistry& registry,
        const std::filesystem::path& regionDir
    );

    /// Load a chunk column from its region file.
    /// Returns FileNotFound if region file missing, ChunkNotLoaded if chunk unwritten.
    [[nodiscard]] static core::Result<ChunkColumn> load(
        glm::ivec2 chunkCoord,
        const BlockRegistry& registry,
        const std::filesystem::path& regionDir
    );
};

} // namespace voxel::world
```

**Note**: Check if `Result<void>` compiles with `std::expected<void, EngineError>`. C++23 `std::expected<void, E>` is valid. If the compiler doesn't support it, use `Result<bool>` and return `true` on success.

### What NOT to Do

- Do NOT implement chunk streaming or async I/O — this is synchronous save/load only
- Do NOT add world-level save management (save-all, world metadata) — that's a future story
- Do NOT modify `ChunkColumn`, `ChunkSection`, or `PaletteCompression` — consume their existing APIs
- Do NOT add region caching or keep region file handles open — open/close per operation for simplicity
- Do NOT use `nlohmann-json` for the binary format — this is raw binary serialization
- Do NOT use exceptions or `try`/`catch` — exceptions are disabled project-wide
- Do NOT use `reinterpret_cast` on structs for serialization — serialize fields individually for portability
- Do NOT compress null (all-air) sections — skip them via the section bitmask

### Project Structure Notes

**New files:**
```
engine/include/voxel/world/ChunkSerializer.h
engine/src/world/ChunkSerializer.cpp
tests/world/TestChunkSerializer.cpp
```

**Files to modify:**
```
engine/CMakeLists.txt       — add ChunkSerializer.cpp, link lz4::lz4
tests/CMakeLists.txt        — add TestChunkSerializer.cpp
```

**LZ4 vcpkg dependency**: already declared in `vcpkg.json` as `"lz4"`. CMake find:
```cmake
find_package(lz4 CONFIG REQUIRED)
target_link_libraries(VoxelEngine PRIVATE lz4::lz4)
```

### Testing Patterns (follow existing Catch2 style)

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/world/ChunkSerializer.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/PaletteCompression.h"

#include <filesystem>

using namespace voxel::world;

// Use a temporary directory for test region files
// std::filesystem::temp_directory_path() / "voxelforge_test_regions"
// Clean up in each test section or use RAII wrapper

TEST_CASE("ChunkSerializer", "[world][serialization]") {

    // Setup: create BlockRegistry with known blocks
    BlockRegistry registry;
    auto stoneResult = registry.registerBlock({.stringId = "base:stone", .isSolid = true});
    auto dirtResult = registry.registerBlock({.stringId = "base:dirt", .isSolid = true});
    REQUIRE(stoneResult.has_value());
    REQUIRE(dirtResult.has_value());
    uint16_t stoneId = stoneResult.value();
    uint16_t dirtId = dirtResult.value();

    auto tempDir = std::filesystem::temp_directory_path() / "vf_test_regions";
    std::filesystem::create_directories(tempDir);

    SECTION("roundtrip: filled chunk saves and loads identically") {
        ChunkColumn column({0, 0});
        column.setBlock(5, 64, 3, stoneId);
        column.setBlock(0, 0, 0, dirtId);

        auto saveResult = ChunkSerializer::save(column, registry, tempDir);
        REQUIRE(saveResult.has_value());

        auto loadResult = ChunkSerializer::load({0, 0}, registry, tempDir);
        REQUIRE(loadResult.has_value());

        auto& loaded = loadResult.value();
        REQUIRE(loaded.getBlock(5, 64, 3) == stoneId);
        REQUIRE(loaded.getBlock(0, 0, 0) == dirtId);
        REQUIRE(loaded.getBlock(8, 128, 8) == BLOCK_AIR);
    }

    SECTION("missing region file returns FileNotFound") { ... }
    SECTION("empty chunk roundtrip") { ... }

    // Cleanup
    std::filesystem::remove_all(tempDir);
}
```

**Test cleanup**: use `std::filesystem::remove_all()` on the temp directory after each TEST_CASE. Consider wrapping in a RAII struct if cleanup is needed per-section.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.6]
- [Source: _bmad-output/planning-artifacts/architecture.md — Palette Compression, Block Registry, Chunk Storage sections]
- [Source: _bmad-output/planning-artifacts/technical-research.md — Compression Ratios, Minecraft palette format]
- [Source: _bmad-output/planning-artifacts/PRD.md — FR-1.3 chunk serialization, NFR-1 performance]
- [Source: _bmad-output/implementation-artifacts/3-5-palette-compression-codec.md — CompressedSection spec]
- [Source: engine/include/voxel/core/Result.h — EngineError enum]
- [Source: engine/include/voxel/world/ChunkColumn.h — ChunkColumn API]
- [Source: engine/include/voxel/world/BlockRegistry.h — BlockRegistry API]
- [Source: engine/include/voxel/world/ChunkSection.h — ChunkSection struct]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition, BLOCK_AIR]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- SIGSEGV in "unknown block" test: `VX_LOG_WARN` called with null logger (tests don't call `Log::init()`). Fixed by guarding log call with `if (core::Log::getLogger())`.
- Missing `#include <fstream>` in test file caused MSVC C2079. Fixed.
- Pre-existing `[[nodiscard]]` warnings-as-errors in TestBlockRegistry.cpp (lines 595, 605, 610) — fixed by capturing `registerBlock()` return values.

### Completion Notes List

- **RegionFile**: Static helper class with `writeChunk`/`readChunk`. Creates region files on demand with 8192-byte empty header. Appends new chunk data at EOF and updates header entry (no in-place overwrite). Uses `std::fstream` with RAII.
- **ChunkSerializer::serializeColumn**: Builds section bitmask, palette-compresses each non-null section via `PaletteCompression::compress()`, writes string IDs from `BlockRegistry::getBlockType()` for cross-session persistence. Little-endian binary format matching spec exactly.
- **ChunkSerializer::save**: Serializes → LZ4 compresses (with uint32_t uncompressed size prefix) → writes via RegionFile. Creates directories as needed.
- **ChunkSerializer::load**: Reads from RegionFile → LZ4 decompresses → deserializes with string ID→numeric ID remapping via `BlockRegistry::getIdByName()`. Unknown blocks substituted with BLOCK_AIR (logged when logger available).
- **BinaryWriter/BinaryReader**: Internal helper classes for safe little-endian serialization with bounds checking. Reader returns `Result<T>` for truncation detection.
- **Tests**: 10 test sections across 5 TEST_CASEs covering all 8 ACs. RAII TempDir for cleanup.

### Change Log

- 2026-03-26: Initial implementation — all tasks complete, all tests passing.

### File List

**New files:**
- `engine/include/voxel/world/RegionFile.h`
- `engine/src/world/RegionFile.cpp`
- `engine/include/voxel/world/ChunkSerializer.h`
- `engine/src/world/ChunkSerializer.cpp`
- `tests/world/TestChunkSerializer.cpp`

**Modified files:**
- `engine/CMakeLists.txt` — added RegionFile.cpp, ChunkSerializer.cpp, linked lz4::lz4
- `tests/CMakeLists.txt` — added TestChunkSerializer.cpp
- `tests/world/TestBlockRegistry.cpp` — fixed pre-existing [[nodiscard]] warnings