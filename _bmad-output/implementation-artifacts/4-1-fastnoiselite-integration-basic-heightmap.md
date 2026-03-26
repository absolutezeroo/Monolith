# Story 4.1: FastNoiseLite Integration + Basic Heightmap

Status: review

## Story

As a **player**,
I want the world to generate varied terrain from noise when chunks load,
so that exploration reveals hills, valleys, and distinct surface layers instead of empty air.

## Acceptance Criteria

1. **AC-1: FastNoiseLite integration** — FastNoiseLite single-header (`FastNoiseLite.h`) is vendored into `engine/include/voxel/world/` and compiles without warnings under MSVC `/W4`.
2. **AC-2: WorldGenerator class** — `WorldGenerator` class in `voxel::world` namespace accepts a `uint64_t seed`, exposes `generateChunkColumn(glm::ivec2 chunkCoord) → ChunkColumn`.
3. **AC-3: Basic heightmap** — 2D Simplex noise (frequency 0.01, 6 octaves FBm) maps to height range [40, 120]. Surface composition: grass on top, 3 dirt below, stone below that, bedrock at y=0.
4. **AC-4: Determinism** — Same `(seed, chunkCoord)` always produces byte-identical `ChunkColumn`. Unit test verifies this.
5. **AC-5: Seed management** — Priority: CLI arg (`--seed 12345`) > `config.json` > random (system clock + PID). Seed saved to `config.json`. Displayed in F3 debug overlay.
6. **AC-6: Spawn point** — `WorldGenerator::findSpawnPoint(seed) → glm::dvec3` finds highest solid block at (0,0), spiral-walks if unsuitable. Spawn Y = top solid + 2.
7. **AC-7: ChunkManager integration** — `ChunkManager::loadChunk()` calls `WorldGenerator::generateChunkColumn()` instead of creating empty columns. `GameApp` owns `WorldGenerator` and injects it into `ChunkManager`.
8. **AC-8: Unit tests** — Catch2 tests for determinism, height bounds, surface composition, spawn point calculation.

## Tasks / Subtasks

- [x] **Task 1: Vendor FastNoiseLite** (AC: 1)
  - [x] Download `FastNoiseLite.h` v1.1.1 into `engine/include/voxel/world/FastNoiseLite.h`
  - [x] Add MSVC warning suppression wrapper if needed (push/pop `#pragma warning`)
  - [x] Verify it compiles in both Debug and Release presets from CLion

- [x] **Task 2: Create WorldGenerator class** (AC: 2, 3, 4)
  - [x] Create `engine/include/voxel/world/WorldGenerator.h`
  - [x] Create `engine/src/world/WorldGenerator.cpp`
  - [x] Constructor: `explicit WorldGenerator(uint64_t seed)`
  - [x] Method: `ChunkColumn generateChunkColumn(glm::ivec2 chunkCoord)`
  - [x] Internal: configure `FastNoiseLite` with seed, Simplex type, FBm fractal, 6 octaves, freq 0.01
  - [x] Height mapping: `noise(worldX, worldZ)` remapped from [-1,1] to [40,120]
  - [x] Surface fill: iterate (x,z) per column, compute height, fill bedrock(y=0), stone(y=1..h-4), dirt(h-3..h-1), grass(h)
  - [x] Register required blocks (stone, dirt, grass, bedrock) in BlockRegistry or assume pre-registered

- [x] **Task 3: Seed management** (AC: 5)
  - [x] Add CLI argument parsing for `--seed <value>` in `main.cpp`
  - [x] Extend `ConfigManager` seed flow: CLI > config.json > random
  - [x] Random seed: `std::chrono::high_resolution_clock` XOR `getpid()` (or `GetCurrentProcessId()` on Windows)
  - [x] Persist chosen seed back to config.json via `ConfigManager::save()`
  - [x] Display seed in F3 debug overlay (`buildDebugOverlay()` in `GameApp`)

- [x] **Task 4: Spawn point calculation** (AC: 6)
  - [x] Method: `glm::dvec3 findSpawnPoint()` on `WorldGenerator`
  - [x] Start at world (0,0), compute heightmap, take highest solid +2
  - [x] Spiral-walk outward if height < 1 or > 200 (unsuitable)
  - [x] Return `glm::dvec3{x + 0.5, spawnY, z + 0.5}` (block center)

- [x] **Task 5: ChunkManager integration** (AC: 7)
  - [x] Add `WorldGenerator*` member to `ChunkManager` (nullable, injected via setter or constructor param)
  - [x] Modify `ChunkManager::loadChunk(glm::ivec2)` to call `m_worldGen->generateChunkColumn(coord)` when generator is set, instead of creating empty `ChunkColumn`
  - [x] `GameApp` creates `WorldGenerator` with resolved seed, passes pointer to `ChunkManager`
  - [x] Generation is synchronous on main thread (async deferred to Story 5.6)

- [x] **Task 6: Unit tests** (AC: 8)
  - [x] Create `tests/world/TestWorldGenerator.cpp`
  - [x] Test: determinism — generate same chunk twice with same seed, compare block-by-block
  - [x] Test: height bounds — all surface blocks within [40, 120]
  - [x] Test: surface composition — grass on top, dirt below, stone further, bedrock at y=0
  - [x] Test: spawn point — returns valid Y above ground
  - [x] Test: different seeds produce different terrain

- [x] **Task 7: CMake wiring** (AC: 1, 2)
  - [x] Add `src/world/WorldGenerator.cpp` to `engine/CMakeLists.txt` source list
  - [x] Add `tests/world/TestWorldGenerator.cpp` to `tests/CMakeLists.txt`
  - [x] No new vcpkg dependency — FastNoiseLite is header-only, vendored

## Dev Notes

### Architecture Constraints

- **No exceptions** (ADR-008). Use `Result<T>` (`std::expected<T, EngineError>`) for any fallible operations. FastNoiseLite does not throw, so this is mostly relevant for file I/O in seed persistence.
- **Chunks NOT in ECS** — `ChunkManager` owns `ChunkColumn` objects directly in its spatial hashmap. WorldGenerator returns `ChunkColumn` by value (move semantics).
- **Namespace**: `voxel::world` for WorldGenerator, same as all chunk/block types.
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, `SCREAMING_SNAKE` for constants.
- **One class per file**, max ~500 lines. WorldGenerator should stay focused.

### FastNoiseLite Integration Details

- **Source**: https://github.com/Auburn/FastNoiseLite — single header `FastNoiseLite.h`
- **Version**: v1.1.1 (latest stable as of 2026-03)
- **Integration pattern**: Vendor as `engine/include/voxel/world/FastNoiseLite.h` — do NOT add to vcpkg. This matches the project pattern (stb is similarly header-only via vcpkg, but FastNoiseLite isn't in vcpkg).
- **MSVC warnings**: FastNoiseLite may trigger `/W4` warnings. Wrap include with `#pragma warning(push, 0)` / `#pragma warning(pop)` in WorldGenerator.cpp, NOT in the header itself.
- **Configuration**: Use `SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2)`, `SetFractalType(FastNoiseLite::FractalType_FBm)`, `SetFractalOctaves(6)`, `SetFrequency(0.01f)`.
- **Coordinate mapping**: `GetNoise(float x, float z)` with world coordinates = `chunkCoord.x * 16 + localX`, `chunkCoord.y * 16 + localZ` (note: `glm::ivec2` for chunk coords uses `.y` for Z axis, matching ChunkManager convention).

### Block IDs — What Exists vs What's Needed

The BlockRegistry currently has blocks loaded from JSON (`assets/blocks/*.json`). For this story, the generator needs these blocks registered:
- `minecraft:stone` (or `voxelforge:stone`)
- `minecraft:dirt` (or `voxelforge:dirt`)
- `minecraft:grass_block` (or `voxelforge:grass_block`)
- `minecraft:bedrock` (or `voxelforge:bedrock`)

**Approach**: Use `BlockRegistry::getIdByName()` to resolve string IDs to numeric IDs at WorldGenerator construction time. Cache these as member variables (`m_stoneId`, `m_dirtId`, etc.) for fast access during generation. If a block isn't registered, log a warning and fall back to numeric ID 1 (first non-air block).

### ChunkManager Integration Pattern

Current `ChunkManager::loadChunk()` creates an empty `ChunkColumn`:
```cpp
void ChunkManager::loadChunk(glm::ivec2 coord)
{
    // Currently: creates empty column
    m_chunks.emplace(coord, std::make_unique<ChunkColumn>(coord));
}
```

**Modified pattern** — WorldGenerator injection:
```cpp
// Option A: Constructor injection (preferred for clarity)
explicit ChunkManager(WorldGenerator* worldGen = nullptr);

// In loadChunk:
void ChunkManager::loadChunk(glm::ivec2 coord)
{
    if (m_chunks.contains(coord)) return;
    if (m_worldGen) {
        auto col = m_worldGen->generateChunkColumn(coord);
        m_chunks.emplace(coord, std::make_unique<ChunkColumn>(std::move(col)));
    } else {
        m_chunks.emplace(coord, std::make_unique<ChunkColumn>(coord));
    }
}
```

**ChunkColumn must be movable** — verify `ChunkColumn` has move constructor (it has `std::array<std::unique_ptr<ChunkSection>, 16>` which is movable). If not defaulted, add `ChunkColumn(ChunkColumn&&) = default;`.

### Seed Management Flow

`ConfigManager` already has `getSeed()` / `setSeed()` (returns `int64_t`, default `8675309`). The seed pipeline:

1. Parse CLI args in `main.cpp` before `GameApp` construction
2. `ConfigManager::load("config.json")` — loads saved seed
3. If CLI `--seed` provided → override ConfigManager seed
4. If no CLI and no config file existed → generate random seed, set it
5. `ConfigManager::save("config.json")` — persist
6. Pass `static_cast<uint64_t>(m_config.getSeed())` to `WorldGenerator` constructor

**CLI parsing**: Keep it simple — manual `argv` scan for `--seed` in `main.cpp`. No external arg-parsing library. The architecture doesn't specify one and this is a single flag.

### F3 Debug Overlay

`GameApp::buildDebugOverlay()` already uses Dear ImGui to render debug info. Add a line:
```cpp
ImGui::Text("Seed: %lld", static_cast<long long>(m_config.getSeed()));
```

### Existing Code Patterns (from Epic 3 stories)

- **Test file convention**: `tests/world/TestXxx.cpp`, uses `#include <catch2/catch_test_macros.hpp>`, `TEST_CASE("description", "[tag]")` with `SECTION` blocks.
- **CMake**: Source files explicitly listed in `engine/CMakeLists.txt` (no GLOB).
- **Header guards**: `#pragma once`.
- **Includes**: Project headers first (`"voxel/..."`), then library headers (`<glm/...>`), then STL (`<cstdint>`).

### Height Mapping Math

```
rawNoise = fnl.GetNoise(worldX, worldZ);   // range [-1, 1]
normalized = (rawNoise + 1.0f) * 0.5f;      // range [0, 1]
height = static_cast<int>(40.0f + normalized * 80.0f);  // range [40, 120]
```

### Surface Composition (per column x,z)

```
y = 0         → bedrock
y = 1..h-4    → stone
y = h-3..h-1  → dirt (3 layers)
y = h         → grass_block
y > h         → air (default, no setBlock needed)
```

Where `h` = computed heightmap value for that (x,z).

Edge case: if `h < 4`, clamp dirt layers. Minimum: bedrock at 0, stone fills minimal gap, at least 1 dirt, grass on top.

### Project Structure Notes

- All new files go in existing `voxel::world` module — no new directories needed
- `FastNoiseLite.h` vendored into `engine/include/voxel/world/` (third-party header alongside project headers — acceptable for single-header libs)
- Alignment with project structure in CLAUDE.md: `engine/include/voxel/world/` for headers, `engine/src/world/` for sources, `tests/world/` for tests

### What NOT To Do

- **Do NOT add FastNoiseLite to vcpkg.json** — it's not in the vcpkg registry. Vendor the header directly.
- **Do NOT make WorldGenerator an ECS system** — chunks are NOT in ECS (CLAUDE.md rule 6).
- **Do NOT add async/threading** — generation is synchronous for this story. enkiTS async comes in Story 5.6.
- **Do NOT create a `worlds/` directory or `world.json`** — that's future work. Seed lives in `config.json` via existing ConfigManager for now.
- **Do NOT modify ChunkSerializer** — disk load priority over generation is Story 3.7's concern and will be wired later.
- **Do NOT register blocks inside WorldGenerator** — blocks should already be registered in BlockRegistry. WorldGenerator only looks them up.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-04-terrain-generation.md — Story 4.1]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 6: World Generation]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-008: No Exceptions]
- [Source: engine/include/voxel/world/ChunkManager.h — loadChunk(), getBlock(), spatial hash]
- [Source: engine/include/voxel/world/ChunkColumn.h — SECTIONS_PER_COLUMN=16, COLUMN_HEIGHT=256]
- [Source: engine/include/voxel/world/ChunkSection.h — SIZE=16, setBlock(), fill()]
- [Source: engine/include/voxel/world/BlockRegistry.h — getIdByName(), registerBlock()]
- [Source: engine/include/voxel/core/ConfigManager.h — getSeed()/setSeed(), load()/save()]
- [Source: game/src/GameApp.h — buildDebugOverlay(), m_config member]
- [Source: engine/CMakeLists.txt — explicit source list, find_package patterns]
- [Source: CLAUDE.md — naming conventions, project structure, critical rules]

## Change Log

- **2026-03-26**: Story 4.1 implemented — FastNoiseLite vendored, WorldGenerator class created with heightmap generation, seed management (CLI/config/random), spawn point calculation, ChunkManager integration, and 5 Catch2 unit tests.

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Initial build: FastNoiseLite include path used `<FastNoiseLite.h>` (angle brackets) but file is vendored in project — fixed to `"voxel/world/FastNoiseLite.h"`.
- Test: `registerBlock()` returns `[[nodiscard]] Result<uint16_t>` — MSVC `/WX` flagged discarded return. Fixed with `(void)` cast.
- Test: `dirtCount == 3` assertion failed — `stoneTop` was `h - 3` instead of `h - 4`, producing only 2 dirt layers. Fixed to `height - DIRT_LAYERS - 1`.

### Completion Notes List

- AC-1: FastNoiseLite.h v1.1.1 vendored into `engine/include/voxel/world/`. MSVC warning suppression via `#pragma warning(push, 0)` in WorldGenerator.cpp. Compiles clean under `/W4 /WX`.
- AC-2: `WorldGenerator` class in `voxel::world` namespace. Constructor takes `uint64_t seed` + `const BlockRegistry&`. Method `generateChunkColumn(glm::ivec2) -> ChunkColumn`.
- AC-3: 2D OpenSimplex2 FBm noise (6 octaves, freq 0.01) mapped to height [40, 120]. Surface: bedrock(y=0), stone(1..h-4), dirt(h-3..h-1), grass(h).
- AC-4: Determinism verified — same seed+coord produces byte-identical columns. Unit test compares all 256*16*16 blocks.
- AC-5: Seed priority: `--seed` CLI arg > config.json > random (high_resolution_clock XOR pid). Persisted to config.json. Displayed in F3 overlay.
- AC-6: `findSpawnPoint()` computes height at (0,0), spiral-walks up to 256 attempts if unsuitable. Returns block center `(x+0.5, h+2, z+0.5)`.
- AC-7: `ChunkManager::setWorldGenerator()` injection. `loadChunk()` calls `generateChunkColumn()` when generator is set. `GameApp` owns WorldGenerator and injects into ChunkManager.
- AC-8: 5 Catch2 tests: determinism, height bounds [40,120], surface composition, spawn point validity, different seeds diverge.
- ChunkColumn: added explicit move ctor/assignment (`= default`) to support return-by-value from WorldGenerator.
- GameApp: registers 4 terrain blocks (stone, dirt, grass_block, bedrock) in BlockRegistry during init.

### File List

- `engine/include/voxel/world/FastNoiseLite.h` (new — vendored third-party header)
- `engine/include/voxel/world/WorldGenerator.h` (new)
- `engine/src/world/WorldGenerator.cpp` (new)
- `engine/include/voxel/world/ChunkColumn.h` (modified — added move ctor/assignment)
- `engine/include/voxel/world/ChunkManager.h` (modified — added WorldGenerator* member, setWorldGenerator(), forward decl)
- `engine/src/world/ChunkManager.cpp` (modified — loadChunk uses WorldGenerator)
- `engine/CMakeLists.txt` (modified — added WorldGenerator.cpp)
- `game/src/main.cpp` (modified — argc/argv, --seed parsing, pass cliSeed to init)
- `game/src/GameApp.h` (modified — added BlockRegistry, WorldGenerator, ChunkManager members; init signature)
- `game/src/GameApp.cpp` (modified — seed resolution, block registration, WorldGenerator creation, overlay seed display)
- `tests/world/TestWorldGenerator.cpp` (new)
- `tests/CMakeLists.txt` (modified — added TestWorldGenerator.cpp)