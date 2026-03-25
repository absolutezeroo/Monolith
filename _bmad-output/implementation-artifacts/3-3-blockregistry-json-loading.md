# Story 3.3: BlockRegistry + JSON Loading

Status: review

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want a registry mapping block names to IDs and storing block properties,
so that the engine knows what each block type is.

## Acceptance Criteria

1. `BlockDefinition` struct per architecture: `stringId`, `numericId`, `isSolid`, `isTransparent`, `hasCollision`, `lightEmission`, `lightFilter`, `hardness`, `textureIndices[6]`, `dropItem`
2. `BlockRegistry` class: `registerBlock(BlockDefinition def) → uint16_t`, `getBlock(uint16_t id) → const BlockDefinition&`, `getIdByName(std::string_view name) → uint16_t`, `blockCount() → uint16_t`
3. ID 0 is always `BLOCK_AIR` — registered automatically in constructor
4. Load base block definitions from `assets/scripts/base/blocks.json`
5. JSON format: array of objects with `stringId`, properties, texture names
6. Namespace `"base:blockname"` enforced — reject names not matching `namespace:name` pattern
7. Unit tests: register, lookup by ID, lookup by name, ID 0 is air, duplicate name rejected

## Tasks / Subtasks

- [x] Task 1: Add `nlohmann-json` to vcpkg.json and CMake (AC: 4, 5)
  - [x] Add `"nlohmann-json"` to `vcpkg.json` dependencies array
  - [x] Add `find_package(nlohmann_json CONFIG REQUIRED)` to `engine/CMakeLists.txt`
  - [x] Add `PRIVATE nlohmann_json::nlohmann_json` to `target_link_libraries`
- [x] Task 2: Create `Block.h` header with `BlockDefinition` struct (AC: 1)
  - [x] Create `engine/include/voxel/world/Block.h`
  - [x] Define `BlockDefinition` struct with all fields per architecture
  - [x] Define `BLOCK_AIR` constant as `uint16_t{0}` (move from `ChunkSection.h` or re-export)
- [x] Task 3: Create `BlockRegistry.h` header (AC: 2, 3, 6)
  - [x] Create `engine/include/voxel/world/BlockRegistry.h`
  - [x] Declare `BlockRegistry` class with public API
  - [x] Constructor auto-registers air block (ID 0)
- [x] Task 4: Implement `BlockRegistry.cpp` (AC: 2, 3, 6)
  - [x] Create `engine/src/world/BlockRegistry.cpp`
  - [x] `registerBlock()` — assigns next ID, validates namespace format, rejects duplicates
  - [x] `getBlock(uint16_t)` — bounds-checked access to `m_blocks` vector
  - [x] `getIdByName(string_view)` — lookup in `m_nameToId` map
  - [x] `blockCount()` — returns `m_blocks.size()`
- [x] Task 5: Implement `loadFromJson()` (AC: 4, 5, 6)
  - [x] Create `engine/src/world/BlockRegistry.cpp` method `loadFromJson(const std::filesystem::path&) → Result<uint16_t>`
  - [x] Parse JSON array, construct `BlockDefinition` per entry, call `registerBlock`
  - [x] Return count of loaded blocks on success, `EngineError::FileNotFound` or `EngineError::InvalidFormat` on failure
- [x] Task 6: Create sample `blocks.json` (AC: 4, 5)
  - [x] Create directory `assets/scripts/base/`
  - [x] Create `assets/scripts/base/blocks.json` with base block definitions (stone, dirt, grass, sand, water, wood, leaves + glass, glowstone, torch)
- [x] Task 7: Register sources in CMake (AC: all)
  - [x] Add `src/world/BlockRegistry.cpp` to `engine/CMakeLists.txt`
  - [x] Add `world/TestBlockRegistry.cpp` to `tests/CMakeLists.txt`
- [x] Task 8: Write unit tests (AC: 7)
  - [x] Create `tests/world/TestBlockRegistry.cpp`
  - [x] Test: constructor auto-registers air at ID 0
  - [x] Test: registerBlock assigns sequential IDs
  - [x] Test: getBlock returns correct definition by ID
  - [x] Test: getIdByName returns correct ID by string name
  - [x] Test: getIdByName returns sentinel/error for unknown name
  - [x] Test: duplicate name registration is rejected
  - [x] Test: invalid namespace format rejected (no colon, empty parts, multiple colons)
  - [x] Test: loadFromJson parses sample blocks.json correctly
  - [x] Test: loadFromJson returns FileNotFound for missing file
  - [x] Test: loadFromJson returns InvalidFormat for malformed JSON
- [x] Task 9: Handle BLOCK_AIR migration from ChunkSection.h
  - [x] Move `BLOCK_AIR` to `Block.h` (canonical location)
  - [x] Update `ChunkSection.h` to `#include "voxel/world/Block.h"` and remove local `BLOCK_AIR`
  - [x] Verify all existing tests still compile and pass

## Dev Notes

### Architecture Constraints

- **Namespace**: `voxel::world` — same as `ChunkSection` and `ChunkColumn`
- **ADR-008**: No exceptions. Use `Result<T>` for `loadFromJson` (file I/O is fallible). Use `VX_ASSERT` for debug bounds checks on `getBlock(uint16_t id)`
- **ADR-004**: BlockRegistry is NOT an ECS component. It's a standalone service owned by the game/engine layer
- **Data-driven content**: Blocks defined in JSON + Lua, never hardcoded in C++ (per Mandatory Patterns in project-context.md)
- **Numeric IDs are session-stable but NOT persistent** — palette compression (Story 3.5) handles save/load mapping
- **uint16_t** supports 65,535 block types — ID 0 is always air

### CRITICAL: JSON Library Dependency

**No JSON library exists in the project yet.** You MUST add `nlohmann-json` to vcpkg before implementing `loadFromJson`:

1. Add to `vcpkg.json`:
```json
"nlohmann-json"
```

2. Add to `engine/CMakeLists.txt`:
```cmake
find_package(nlohmann_json CONFIG REQUIRED)
```
Then add to `target_link_libraries`:
```cmake
PRIVATE nlohmann_json::nlohmann_json
```

`nlohmann-json` is the standard C++ JSON library — header-only, MIT license, exception-free mode available via `JSON_NOEXCEPTION` define. Since this project compiles with exceptions disabled, you MUST define:
```cmake
target_compile_definitions(VoxelEngine PRIVATE JSON_NOEXCEPTION)
```
With `JSON_NOEXCEPTION`, nlohmann-json calls `std::abort()` on parse errors instead of throwing. Use `nlohmann::json::parse(input, nullptr, false)` to get a `discarded` value on parse failure instead of aborting, then check `.is_discarded()`.

### BlockDefinition Struct — Follow Architecture Exactly

```cpp
// Block.h
#pragma once

#include <cstdint>
#include <string>

namespace voxel::world
{

constexpr uint16_t BLOCK_AIR = 0;

struct BlockDefinition
{
    std::string stringId;              // "base:stone", "mymod:crystal"
    uint16_t numericId = 0;            // Assigned at runtime by registry
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    uint8_t lightEmission = 0;         // 0-15
    uint8_t lightFilter = 15;          // 0 = fully opaque to light, 15 = transparent
    float hardness = 1.0f;
    uint16_t textureIndices[6] = {};   // Per face [+X, -X, +Y, -Y, +Z, -Z]
    std::string dropItem;              // String ID of item dropped on break
};

} // namespace voxel::world
```

This matches `architecture.md` System 4 exactly. Do NOT add extra fields. Do NOT remove fields. The `breakTime` field in architecture is omitted from the epic's AC — follow the epic's AC (no `breakTime`).

### BlockRegistry Class — Follow Architecture Exactly

```cpp
// BlockRegistry.h
#pragma once

#include "voxel/world/Block.h"
#include "voxel/core/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

class BlockRegistry
{
public:
    BlockRegistry();

    /// Register a new block definition. Returns assigned numeric ID.
    /// Rejects duplicate stringId or invalid namespace format.
    [[nodiscard]] core::Result<uint16_t> registerBlock(BlockDefinition def);

    /// Get block definition by numeric ID.
    [[nodiscard]] const BlockDefinition& getBlock(uint16_t id) const;

    /// Get numeric ID by string name. Returns BLOCK_AIR (0) if not found.
    [[nodiscard]] uint16_t getIdByName(std::string_view name) const;

    /// Number of registered blocks (including air).
    [[nodiscard]] uint16_t blockCount() const;

    /// Load block definitions from a JSON file. Returns count of blocks loaded.
    [[nodiscard]] core::Result<uint16_t> loadFromJson(const std::filesystem::path& filePath);

private:
    std::vector<BlockDefinition> m_blocks;                    // Indexed by numericId
    std::unordered_map<std::string, uint16_t> m_nameToId;     // "base:stone" -> 42

    [[nodiscard]] static bool isValidNamespace(std::string_view name);
};

} // namespace voxel::world
```

Key decisions:
- `registerBlock` returns `Result<uint16_t>` — failure for duplicate name or invalid namespace (use `EngineError::InvalidArgument`)
- `getIdByName` returns `BLOCK_AIR` (0) for unknown names — this is safe because air is the default
- `loadFromJson` returns `Result<uint16_t>` — count of loaded blocks, or `FileNotFound`/`InvalidFormat`
- `isValidNamespace` validates `"namespace:name"` format (contains exactly one colon, non-empty parts)

### Namespace Validation

The `"namespace:name"` format is enforced to prevent mod collisions (FR-6.6). Validation rules:
- Must contain exactly one `:` separator
- Both parts must be non-empty
- Namespace part should be lowercase alphanumeric + underscores
- Name part should be lowercase alphanumeric + underscores

```cpp
bool BlockRegistry::isValidNamespace(std::string_view name)
{
    auto colon = name.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon == name.size() - 1)
        return false;
    // Optionally validate character set (a-z, 0-9, underscore)
    return true;
}
```

### JSON Format for blocks.json

```json
[
    {
        "stringId": "base:stone",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 15,
        "hardness": 1.5,
        "textureIndices": [1, 1, 1, 1, 1, 1],
        "dropItem": "base:cobblestone"
    },
    {
        "stringId": "base:dirt",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 15,
        "hardness": 0.5,
        "textureIndices": [2, 2, 2, 2, 2, 2],
        "dropItem": "base:dirt"
    },
    {
        "stringId": "base:grass_block",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 15,
        "hardness": 0.6,
        "textureIndices": [4, 4, 3, 2, 4, 4],
        "dropItem": "base:dirt"
    },
    {
        "stringId": "base:sand",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 15,
        "hardness": 0.5,
        "textureIndices": [5, 5, 5, 5, 5, 5],
        "dropItem": "base:sand"
    },
    {
        "stringId": "base:water",
        "isSolid": false,
        "isTransparent": true,
        "hasCollision": false,
        "lightEmission": 0,
        "lightFilter": 2,
        "hardness": 100.0,
        "textureIndices": [6, 6, 6, 6, 6, 6],
        "dropItem": ""
    },
    {
        "stringId": "base:oak_log",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 15,
        "hardness": 2.0,
        "textureIndices": [8, 8, 7, 7, 8, 8],
        "dropItem": "base:oak_log"
    },
    {
        "stringId": "base:oak_leaves",
        "isSolid": true,
        "isTransparent": true,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 1,
        "hardness": 0.2,
        "textureIndices": [9, 9, 9, 9, 9, 9],
        "dropItem": ""
    },
    {
        "stringId": "base:glass",
        "isSolid": true,
        "isTransparent": true,
        "hasCollision": true,
        "lightEmission": 0,
        "lightFilter": 0,
        "hardness": 0.3,
        "textureIndices": [10, 10, 10, 10, 10, 10],
        "dropItem": ""
    },
    {
        "stringId": "base:glowstone",
        "isSolid": true,
        "isTransparent": false,
        "hasCollision": true,
        "lightEmission": 15,
        "lightFilter": 15,
        "hardness": 0.3,
        "textureIndices": [11, 11, 11, 11, 11, 11],
        "dropItem": "base:glowstone"
    },
    {
        "stringId": "base:torch",
        "isSolid": false,
        "isTransparent": true,
        "hasCollision": false,
        "lightEmission": 14,
        "lightFilter": 0,
        "hardness": 0.0,
        "textureIndices": [12, 12, 12, 12, 12, 12],
        "dropItem": "base:torch"
    }
]
```

The `textureIndices` values are placeholder integers — actual texture loading happens in Epic 6 (TextureArray). The indices just need to be valid `uint16_t` values for now.

### loadFromJson Implementation Pattern

```cpp
core::Result<uint16_t> BlockRegistry::loadFromJson(const std::filesystem::path& filePath)
{
    // 1. Check file exists
    if (!std::filesystem::exists(filePath))
        return std::unexpected(core::EngineError::FileNotFound);

    // 2. Read file contents
    std::ifstream file(filePath);
    if (!file.is_open())
        return std::unexpected(core::EngineError::FileNotFound);

    // 3. Parse JSON (no-throw mode)
    auto json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_array())
        return std::unexpected(core::EngineError::InvalidFormat);

    // 4. Iterate and register
    uint16_t count = 0;
    for (const auto& entry : json)
    {
        BlockDefinition def;
        // Parse fields with defaults...
        def.stringId = entry.value("stringId", "");
        def.isSolid = entry.value("isSolid", true);
        def.isTransparent = entry.value("isTransparent", false);
        def.hasCollision = entry.value("hasCollision", true);
        def.lightEmission = entry.value("lightEmission", static_cast<uint8_t>(0));
        def.lightFilter = entry.value("lightFilter", static_cast<uint8_t>(15));
        def.hardness = entry.value("hardness", 1.0f);
        def.dropItem = entry.value("dropItem", "");

        if (entry.contains("textureIndices") && entry["textureIndices"].is_array())
        {
            const auto& texArr = entry["textureIndices"];
            for (size_t i = 0; i < 6 && i < texArr.size(); ++i)
                def.textureIndices[i] = texArr[i].get<uint16_t>();
        }

        auto result = registerBlock(std::move(def));
        if (result.has_value())
            ++count;
        else
            VX_LOG_WARN("Failed to register block '{}': skipping", def.stringId);
    }

    return count;
}
```

### BLOCK_AIR Migration

`BLOCK_AIR` is currently defined in `ChunkSection.h`. It belongs in `Block.h` as the canonical location since it's a block identity constant. Migration:

1. Define `BLOCK_AIR` in `Block.h`
2. In `ChunkSection.h`: replace `constexpr uint16_t BLOCK_AIR = 0;` with `#include "voxel/world/Block.h"`
3. All existing code using `BLOCK_AIR` continues to work (same namespace, same value)

**Verify**: `TestChunkSection.cpp` and `TestChunkColumn.cpp` (if already implemented) still compile after the move.

### File Locations

| File | Path |
|------|------|
| Block definition struct | `engine/include/voxel/world/Block.h` |
| Registry header | `engine/include/voxel/world/BlockRegistry.h` |
| Registry source | `engine/src/world/BlockRegistry.cpp` |
| Sample JSON | `assets/scripts/base/blocks.json` |
| Tests | `tests/world/TestBlockRegistry.cpp` |

### Dependencies

- `nlohmann-json` — NEW vcpkg dependency (must be added)
- `voxel/core/Result.h` — `Result<T>`, `EngineError` enum
- `voxel/core/Assert.h` — `VX_ASSERT` for debug bounds checks
- `voxel/core/Log.h` — `VX_LOG_WARN` for non-fatal JSON parsing issues
- `std::filesystem` — for file path handling in `loadFromJson`
- `<fstream>` — for reading JSON file
- `<string>`, `<string_view>`, `<vector>`, `<unordered_map>` — standard containers

### Code Patterns from Stories 3.1 and 3.2

- `#pragma once` header guard
- Namespace `voxel::world`
- `[[nodiscard]]` on all const query methods and on `registerBlock`/`loadFromJson`
- `VX_ASSERT` with descriptive messages for debug bounds checks
- Include order: associated header → project headers → third-party → standard
- Test file: `catch2/catch_test_macros.hpp` with `SECTION`-based organization

### Testing Guidance

```cpp
TEST_CASE("BlockRegistry", "[world]")
{
    BlockRegistry registry;

    SECTION("constructor registers air at ID 0")
    {
        REQUIRE(registry.blockCount() == 1);
        REQUIRE(registry.getBlock(0).stringId == "base:air");
        REQUIRE(registry.getIdByName("base:air") == BLOCK_AIR);
    }

    SECTION("registerBlock assigns sequential IDs")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        auto result = registry.registerBlock(stone);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 1);

        BlockDefinition dirt;
        dirt.stringId = "base:dirt";
        result = registry.registerBlock(dirt);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 2);
    }

    SECTION("getBlock returns correct definition")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        stone.hardness = 1.5f;
        auto id = registry.registerBlock(stone);
        REQUIRE(registry.getBlock(id.value()).hardness == 1.5f);
    }

    SECTION("getIdByName returns correct ID")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        auto id = registry.registerBlock(stone);
        REQUIRE(registry.getIdByName("base:stone") == id.value());
    }

    SECTION("getIdByName returns BLOCK_AIR for unknown name")
    {
        REQUIRE(registry.getIdByName("base:nonexistent") == BLOCK_AIR);
    }

    SECTION("duplicate name rejected")
    {
        BlockDefinition stone;
        stone.stringId = "base:stone";
        REQUIRE(registry.registerBlock(stone).has_value());
        REQUIRE_FALSE(registry.registerBlock(stone).has_value());
    }

    SECTION("invalid namespace format rejected")
    {
        BlockDefinition bad;
        bad.stringId = "nonamespace";
        REQUIRE_FALSE(registry.registerBlock(bad).has_value());

        bad.stringId = ":empty_ns";
        REQUIRE_FALSE(registry.registerBlock(bad).has_value());

        bad.stringId = "empty_name:";
        REQUIRE_FALSE(registry.registerBlock(bad).has_value());
    }
}

TEST_CASE("BlockRegistry JSON loading", "[world]")
{
    BlockRegistry registry;

    SECTION("loadFromJson parses sample file")
    {
        auto result = registry.loadFromJson("assets/scripts/base/blocks.json");
        REQUIRE(result.has_value());
        REQUIRE(result.value() >= 7); // At least the base blocks
        REQUIRE(registry.getIdByName("base:stone") != BLOCK_AIR);
        REQUIRE(registry.getBlock(registry.getIdByName("base:stone")).isSolid);
    }

    SECTION("loadFromJson returns FileNotFound for missing file")
    {
        auto result = registry.loadFromJson("nonexistent/path.json");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == core::EngineError::FileNotFound);
    }

    SECTION("loadFromJson returns InvalidFormat for bad JSON")
    {
        // Create a temp file with invalid JSON for this test
        // or test with a known bad path
    }
}
```

**Important for JSON test**: The `loadFromJson` test reads from a real file. The test working directory when run from CLion/CTest is typically the build directory. Either:
- Use a relative path from the project root and set `WORKING_DIRECTORY` in CTest, or
- Use `std::filesystem::path` relative to `CMAKE_SOURCE_DIR` passed via a compile definition

Recommended: Add a compile definition to tests CMake:
```cmake
target_compile_definitions(VoxelTests PRIVATE
    VOXELFORGE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
```
Then in tests: `registry.loadFromJson(std::filesystem::path(VOXELFORGE_ASSETS_DIR) / "scripts/base/blocks.json")`

### CMake Integration

Add to `engine/CMakeLists.txt` source list:
```cmake
src/world/BlockRegistry.cpp
```

Add to `engine/CMakeLists.txt` dependencies:
```cmake
find_package(nlohmann_json CONFIG REQUIRED)
# In target_link_libraries:
PRIVATE nlohmann_json::nlohmann_json
```

Add no-exception mode for nlohmann-json:
```cmake
target_compile_definitions(VoxelEngine PRIVATE JSON_NOEXCEPTION)
```

Add to `tests/CMakeLists.txt`:
```cmake
world/TestBlockRegistry.cpp
```

Add assets path define to tests:
```cmake
target_compile_definitions(VoxelTests PRIVATE
    VOXELFORGE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
```

### What NOT to Do

- Do NOT use exceptions for JSON parsing errors — use `nlohmann::json::parse(input, nullptr, false)` + `.is_discarded()` check
- Do NOT hardcode block definitions in C++ — they must come from JSON (data-driven content rule)
- Do NOT implement ItemRegistry — that is a separate story/epic
- Do NOT implement palette compression — that is Story 3.5
- Do NOT add Lua registration API — that is Epic 9
- Do NOT add texture loading logic — `textureIndices` are just `uint16_t` values, actual textures are Epic 6
- Do NOT use `std::shared_ptr` for block definitions — they live in a contiguous `std::vector`
- Do NOT use `using namespace` in header files
- Do NOT add thread safety — BlockRegistry is populated at startup, then read-only during gameplay
- Do NOT make `numericId` persistent across sessions — it's session-only (palette handles persistence)

### Project Structure Notes

- `Block.h` is the new canonical home for `BlockDefinition` and `BLOCK_AIR`
- `BlockRegistry.h/cpp` is the third class in `voxel::world`, following `ChunkSection` and `ChunkColumn`
- `assets/scripts/base/` directory is NEW — create it for the JSON file
- No new directories needed under `engine/` — files slot into existing `world/` structure

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.3]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 4: Block Registry]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-008: Exceptions Disabled]
- [Source: _bmad-output/planning-artifacts/PRD.md — FR-6.1, FR-6.3, FR-6.5, FR-6.6]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Memory & Ownership]
- [Source: engine/include/voxel/world/ChunkSection.h — BLOCK_AIR current location]
- [Source: engine/include/voxel/core/Result.h — Result<T>, EngineError enum]
- [Source: vcpkg.json — Current dependencies (no JSON library present)]

### Git Intelligence

Recent commits show established patterns:
- `feat(world): add ChunkSection flat storage and tests` — POD struct, VX_ASSERT bounds, Catch2 sections
- `feat(world): implement ChunkSection flat array storage with unit tests` — one class per file, [[nodiscard]], constexpr constants
- Story 3.1 established `engine/include/voxel/world/` and `tests/world/` directories
- Commit format: `feat(world): add BlockRegistry with JSON loading and unit tests`

### Previous Story Intelligence (Story 3.1)

- `ChunkSection` established the `voxel::world` namespace pattern and directory structure
- `BLOCK_AIR` is currently in `ChunkSection.h` — migration to `Block.h` is a housekeeping task
- `VX_ASSERT` pattern with descriptive string messages is established
- Catch2 `SECTION`-based tests with `using namespace voxel::world` in .cpp files
- `[[nodiscard]]` applied consistently to all query methods
- Fixed pre-existing `[[nodiscard]]` warnings in Story 3.1 — watch for similar issues

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- Fixed [[nodiscard]] warnings in test file (lines 120, 125, 203) — MSVC /WX treats as error
- Fixed use-after-move bug in loadFromJson warning log (captured stringId before std::move)

### Completion Notes List
- BlockDefinition struct matches architecture.md System 4 exactly (all 10 fields)
- BlockRegistry auto-registers "base:air" at ID 0 with correct properties (non-solid, transparent, no collision)
- Namespace validation enforces "namespace:name" format with single colon, non-empty parts
- JSON loading uses nlohmann-json no-throw mode (JSON_NOEXCEPTION + parse with nullptr,false)
- 10 base blocks defined in blocks.json (stone, dirt, grass_block, sand, water, oak_log, oak_leaves, glass, glowstone, torch)
- BLOCK_AIR migrated from ChunkSection.h to Block.h — all existing tests unaffected
- VOXELFORGE_ASSETS_DIR compile definition added to tests for portable JSON file path resolution
- 14 test sections covering all ACs: registration, lookup, namespace validation, JSON loading, error cases

### Change Log
- 2026-03-25: Initial implementation of BlockRegistry + JSON Loading (all 9 tasks completed)

### File List
- `engine/include/voxel/world/Block.h` (new)
- `engine/include/voxel/world/BlockRegistry.h` (new)
- `engine/src/world/BlockRegistry.cpp` (new)
- `assets/scripts/base/blocks.json` (new)
- `tests/world/TestBlockRegistry.cpp` (new)
- `vcpkg.json` (modified — added nlohmann-json)
- `engine/CMakeLists.txt` (modified — nlohmann_json find/link/define, BlockRegistry.cpp source)
- `tests/CMakeLists.txt` (modified — TestBlockRegistry.cpp source, VOXELFORGE_ASSETS_DIR define)
- `engine/include/voxel/world/ChunkSection.h` (modified — BLOCK_AIR migrated to Block.h)