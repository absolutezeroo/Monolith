# Story 4.4: 3D Caves and Overhangs

Status: ready-for-dev

## Story

As a **developer**,
I want 3D noise carving to create caves and overhangs,
so that the underground is interesting to explore.

## Acceptance Criteria

1. **AC-1: Primary cave noise** — A 3D Simplex noise layer (OpenSimplex2, freq 0.02, 3 octaves FBm) samples every block position within solid terrain. Seed = `static_cast<int>(seed + 4)`.
2. **AC-2: Cave carving** — For each solid block, if `caveDensity(x, y, z) > threshold(y)`, set block to `BLOCK_AIR`. Only solid blocks are carved (air stays air). Bedrock at y=0 is NEVER carved.
3. **AC-3: Depth-dependent threshold** — Threshold varies with Y: high near bedrock (fewer caves), lowest at mid-depth y~50-80 (most caves), rising near surface (fewer surface breaches). Implemented as a smooth curve, not hard steps.
4. **AC-4: Spaghetti caves** — A second 3D noise layer with axis-stretched frequency (e.g. x/z freq 0.03, y freq 0.01) produces elongated, tunnel-like cavities. Seed = `static_cast<int>(seed + 5)`. Combined with the primary noise to create both large chambers (cheese) and narrow tunnels (spaghetti).
5. **AC-5: Overhangs** — Near the terrain surface, 3D noise naturally diverges from the 2D heightmap, creating overhangs and cliff faces. No special code needed beyond the cave carving pass — the 3D noise at surface-adjacent blocks already produces this effect.
6. **AC-6: Surface openings** — Cave openings to the surface are allowed. No artificial capping or surface protection layer. The depth-dependent threshold rise near the surface reduces but does not eliminate surface cave entrances.
7. **AC-7: Determinism** — Same `(seed, chunkCoord)` produces byte-identical output. All noise instances seeded deterministically from the world seed.
8. **AC-8: Unit tests** — Catch2 tests for: cave carving determinism, bedrock never carved, caves exist at mid-depth, fewer caves near bedrock, fewer caves near surface, spaghetti caves produce elongated shapes, terrain determinism preserved with cave pass.

## Tasks / Subtasks

- [ ] **Task 1: Create CaveCarver class** (AC: 1, 4)
  - [ ] Create `engine/include/voxel/world/CaveCarver.h`
  - [ ] Create `engine/src/world/CaveCarver.cpp`
  - [ ] Constructor: `explicit CaveCarver(uint64_t seed)`
  - [ ] Members:
    - `FastNoiseLite m_cheeseNoise` — primary large-cavity noise
    - `FastNoiseLite m_spaghettiNoise` — elongated tunnel noise
  - [ ] Cheese noise config: `NoiseType_OpenSimplex2`, `FractalType_FBm`, freq 0.02, octaves 3, seed = `static_cast<int>(seed + 4)`
  - [ ] Spaghetti noise config: `NoiseType_OpenSimplex2`, `FractalType_FBm`, freq 0.03, octaves 2, seed = `static_cast<int>(seed + 5)`
    - Spaghetti uses a coordinate-stretch trick, not a separate FastNoiseLite frequency per axis (FastNoiseLite only has a single frequency). Instead, scale input coords: `GetNoise(x * 1.0f, y * 0.33f, z * 1.0f)` — this stretches caves along the Y axis, creating horizontal tunnels
  - [ ] Method: `void carveColumn(ChunkColumn& column, glm::ivec2 chunkCoord, int surfaceHeights[16][16]) const`
  - [ ] Method: `bool shouldCarve(float worldX, float worldY, float worldZ, int surfaceHeight) const` — evaluates both noise layers + threshold

- [ ] **Task 2: Implement depth-dependent threshold** (AC: 3)
  - [ ] Method: `static float getThreshold(int y, int surfaceHeight)` — pure function
  - [ ] Threshold curve design:
    ```
    y in [0, 5]:           threshold = 1.0 (never carve — bedrock protection zone)
    y in [5, 20]:          threshold lerps from 0.9 to 0.55 (few caves near bedrock)
    y in [20, 50]:         threshold lerps from 0.55 to 0.45 (increasing cave density)
    y in [50, 80]:         threshold = 0.45 (peak cave zone — most open)
    y in [80, surface-10]: threshold lerps from 0.45 to 0.55 (decreasing toward surface)
    y in [surface-10, surface+5]: threshold lerps from 0.55 to 0.75 (reduced near surface but openings allowed)
    y > surface+5:         skip (already air, above terrain)
    ```
  - [ ] Use `std::lerp` (C++20) for interpolation between zones
  - [ ] Surface height passed per-column to adapt threshold near the actual surface, not a fixed Y
  - [ ] The values above are starting points — tune for visual quality

- [ ] **Task 3: Implement cave carving logic** (AC: 2, 5, 6)
  - [ ] `shouldCarve()` algorithm:
    1. Sample cheese noise: `float cheese = m_cheeseNoise.GetNoise(worldX, worldY, worldZ)`
    2. Remap cheese from [-1,1] to [0,1]: `cheese = (cheese + 1.0f) * 0.5f`
    3. Sample spaghetti noise with Y-stretch: `float spaghetti = m_spaghettiNoise.GetNoise(worldX, worldY * 0.33f, worldZ)`
    4. Remap spaghetti to [0,1]
    5. Spaghetti contributes where it's near its isosurface (narrow band): `float spaghettiCarve = 1.0f - std::abs(spaghetti - 0.5f) * 4.0f` — peaks at spaghetti=0.5, falls off to 0 at 0.25 and 0.75
    6. Clamp `spaghettiCarve` to [0,1]
    7. Combined density: `float density = std::max(cheese, spaghettiCarve)`
    8. Compare: `return density > getThreshold(y, surfaceHeight)`
  - [ ] `carveColumn()` algorithm:
    1. For each (x, z) in [0,15]:
       - Compute `worldX = chunkCoord.x * 16 + x`, `worldZ = chunkCoord.y * 16 + z`
       - `surfaceH = surfaceHeights[x][z]`
       - For y from 1 to `min(surfaceH + 5, 255)` (skip y=0 bedrock, skip air above surface+5):
         - `worldY = static_cast<float>(y)`
         - If block at (x, y, z) is not `BLOCK_AIR` and `shouldCarve(worldX, worldY, worldZ, surfaceH)`:
           - `column.setBlock(x, y, z, BLOCK_AIR)`
  - [ ] Bedrock at y=0 is always skipped (loop starts at y=1, and threshold=1.0 in [0,5] range provides additional safety)
  - [ ] Overhangs are an emergent property: where 3D noise near y=surfaceH creates air pockets, surface blocks are carved, revealing stone/dirt underneath as overhang ceilings

- [ ] **Task 4: Integrate CaveCarver into WorldGenerator** (AC: 2, 7)
  - [ ] Add `CaveCarver m_caveCarver` member to `WorldGenerator`, initialized with same seed
  - [ ] In `generateChunkColumn()`, after terrain fill + biome surface application:
    1. Collect surface heights: `int surfaceHeights[16][16]` — the Y of the topmost solid block per (x, z), already computed during the fill loop
    2. Call `m_caveCarver.carveColumn(column, chunkCoord, surfaceHeights)`
  - [ ] The carving pass is the LAST step in generation (after bedrock, stone, biome surface). This means caves carve through biome surface blocks too (intentional — caves can breach any terrain).
  - [ ] Surface height tracking: during the existing fill loop, record the height `h` computed for each (x, z) column into the `surfaceHeights` array. Pass this to `carveColumn()`. No extra noise evaluation needed for surface heights.

- [ ] **Task 5: Unit tests** (AC: 8)
  - [ ] Create `tests/world/TestCaveCarver.cpp`
  - [ ] Test: **Determinism** — carve same column twice with same seed → identical output
  - [ ] Test: **Bedrock protection** — after carving, y=0 is never air (scan full column)
  - [ ] Test: **Caves exist** — generate a chunk column, count air blocks in y range [30, 80]. Expect at least some carved blocks (> 0 air that wasn't originally air). Use a known seed that produces caves.
  - [ ] Test: **Depth distribution** — count carved blocks per Y-band ([5-20], [40-80], [surface-10, surface]). Mid-depth band should have more carved blocks than near-bedrock or near-surface bands.
  - [ ] Test: **Threshold curve** — `getThreshold()` returns expected values at key Y positions: 1.0 at y=0, ~0.45 at y=60, higher near surface.
  - [ ] Test: **shouldCarve determinism** — same (x,y,z) + same seed = same result
  - [ ] Add to `tests/world/TestWorldGenerator.cpp`:
    - Test: terrain determinism preserved with cave carver enabled
    - Test: generated chunk has both solid and air blocks at mid-depth (caves exist in full pipeline)
  - [ ] Tag all cave tests with `[world][cave]`

- [ ] **Task 6: CMake wiring** (AC: all)
  - [ ] Add `src/world/CaveCarver.cpp` to `engine/CMakeLists.txt` source list
  - [ ] Add `tests/world/TestCaveCarver.cpp` to `tests/CMakeLists.txt`

## Dev Notes

### Architecture Constraints

- **No exceptions** (ADR-008). CaveCarver is pure compute — use `VX_ASSERT` for programmer errors only. No `Result<T>` needed.
- **Chunks NOT in ECS** (ADR-004) — CaveCarver and WorldGenerator live outside ECS.
- **Namespace**: `voxel::world` for CaveCarver and all modifications.
- **Naming**: PascalCase classes/enums, camelCase methods, `m_` prefix members, `SCREAMING_SNAKE` constants.
- **One class per file**, max ~500 lines. CaveCarver gets its own .h/.cpp pair.
- **No direct state mutation** — CaveCarver receives a `ChunkColumn&` reference and carves it in place. This is within the generation pipeline (single-threaded, owned by WorldGenerator), not shared state.

### What Stories 4.1, 4.2, and 4.3 Create (Prerequisites)

Story 4.1 creates:
- `WorldGenerator(uint64_t seed)` with `generateChunkColumn(glm::ivec2) -> ChunkColumn`
- `FastNoiseLite` integration, seed management, ChunkManager wiring
- Cached block IDs: `m_stoneId`, `m_dirtId`, `m_grassId`, `m_bedrockId`
- `findSpawnPoint()`, F3 seed display

Story 4.2 creates:
- `SplineCurve` class with `evaluate(float) -> float`, `createDefault()`
- Dual noise: `m_continentNoise` (freq 0.001, 4 oct), `m_detailNoise` (freq 0.02, 4 oct)
- Height = `spline.evaluate(continentNoise) + detailNoise * DETAIL_AMPLITUDE`
- `DETAIL_AMPLITUDE = 7.0f`

Story 4.3 creates:
- `BiomeSystem` with temperature/humidity noise and Whittaker lookup
- `BiomeTypes.h` with `enum class BiomeType` and `BiomeDefinition`
- Per-biome surface blocks, height modifiers, blending
- New block types: sand, sandstone, snow_block

**This story extends WorldGenerator by adding a cave carving post-pass. Do NOT modify the terrain fill logic, biome system, or spline — only add the CaveCarver call after fill completes.**

### FastNoiseLite Seed Management

World seed cascades to noise instances with deterministic offsets:
- `seed + 0`: continent noise (Story 4.1)
- `seed + 1`: detail noise (Story 4.2)
- `seed + 2`: temperature noise (Story 4.3)
- `seed + 3`: humidity noise (Story 4.3)
- **`seed + 4`: cheese cave noise (this story)**
- **`seed + 5`: spaghetti cave noise (this story)**

All cast via `static_cast<int>(seed + N)` — FastNoiseLite takes `int` seeds.

### Cave Generation Technique — Design Rationale

Two noise layers produce distinct cave features:

**Cheese caves** (primary): Large, open cavities. Standard 3D noise thresholding — where noise > threshold, carve to air. Produces blobby chambers connected by passages. Named after the holes in Swiss cheese.

**Spaghetti caves** (secondary): Narrow, winding tunnels. Uses the **isosurface intersection** technique — caves form where the noise value is near a specific target (0.5), creating thin worm-like tunnels instead of open blobs. The Y-axis coordinate is scaled by 0.33 before sampling, which stretches the noise vertically and produces predominantly horizontal tunnels (like real cave systems). This approach is inspired by [Danol's isosurface technique](https://blog.danol.cz/voxel-cave-generation-using-3d-perlin-noise-isosurfaces/) and [Minecraft's cave generation](https://www.minecraft.net/en-us/article/caves---cliffs--part-i-out-today).

The two systems are combined via `max(cheese, spaghettiCarve)` — a block is carved if EITHER the cheese noise opens a chamber OR the spaghetti noise creates a tunnel. This produces large caverns connected by narrow winding passages.

### Depth-Dependent Threshold — Rationale

Real caves are more common at mid-depth and rare near the surface or deep underground. The threshold curve models this:
- **Near bedrock (y < 20)**: High threshold → very few caves. Protects the structural foundation.
- **Mid-depth (y 50-80)**: Low threshold → most caves. This is the primary exploration zone.
- **Near surface**: Rising threshold reduces but doesn't eliminate surface openings. Cave entrances add interest. The threshold depends on the actual `surfaceHeight` per column, not a fixed Y, so mountain caves can extend deeper relative to the surface.

### Spaghetti Noise — Axis Stretching

FastNoiseLite only supports a single frequency parameter. To get axis-dependent scaling, scale the input coordinates before calling `GetNoise()`:
```cpp
// Y-stretched spaghetti noise — tunnels run mostly horizontal
float spaghetti = m_spaghettiNoise.GetNoise(worldX, worldY * 0.33f, worldZ);
```

The 0.33 Y-scale means vertical features are 3x larger than horizontal, creating primarily horizontal tunnel networks. This is a standard technique for procedural cave generation.

### Spaghetti Isosurface Carve Calculation

Instead of simple thresholding (noise > threshold → carve), spaghetti uses **proximity to an isosurface**:
```cpp
float spaghettiCarve = 1.0f - std::abs(spaghetti - 0.5f) * 4.0f;
spaghettiCarve = std::clamp(spaghettiCarve, 0.0f, 1.0f);
```

This creates a narrow band around `spaghetti == 0.5` where carving occurs. The `* 4.0f` controls tunnel width — larger values make thinner tunnels. The result is worm-like passages instead of open blobs.

### Performance Considerations

Cave carving samples 3D noise at every solid block in the column. Worst case: ~256 height * 256 columns/chunk = 65,536 samples * 2 noise layers = ~131K noise calls per chunk. At FastNoiseLite's ~50ns/sample, this adds ~6.5ms per chunk.

Optimizations (implement only if profiling shows issues):
1. **Skip empty sections**: If `ChunkSection::isEmpty()` is true, skip that section entirely. Sections above surface height are already air.
2. **Early Y exit**: Don't sample above `surfaceHeight + 5` per column (the threshold is 1.0 there anyway).
3. **Skip if threshold is 1.0**: In the bedrock protection zone (y < 5), skip noise evaluation entirely.

The Task 3 algorithm already incorporates optimization #2 (Y loop limit). Optimizations #1 and #3 are worth adding in the implementation for free since they're trivial conditionals.

### ChunkColumn API for Cave Carving

Cave carving uses these existing APIs:
- `column.getBlock(x, y, z)` — returns `uint16_t` block ID (0 = `BLOCK_AIR`)
- `column.setBlock(x, y, z, BLOCK_AIR)` — sets block to air (auto-creates section if needed)

No new APIs on ChunkColumn or ChunkSection are needed.

**Important**: `setBlock()` calls `getOrCreateSection()` internally. For sections that are entirely above the surface (all air), the carver won't touch them (Y loop is bounded by surface height + 5). For sections that become fully air after carving, the section still exists in memory. This is fine for V1 — future optimization could detect and destroy fully-air sections.

### Existing Code Patterns (from Stories 4.1–4.3)

- **FastNoiseLite include**: wrap with `#pragma warning(push, 0)` / `#pragma warning(pop)` in .cpp files (MSVC `/W4` compat, established in Story 4.1).
- **Test file convention**: `tests/world/TestXxx.cpp`, `#include <catch2/catch_test_macros.hpp>`, `TEST_CASE("desc", "[world][cave]")` with `SECTION` blocks.
- **CMake**: Source files explicitly listed — no GLOB. Add to `engine/CMakeLists.txt` and `tests/CMakeLists.txt`.
- **Headers**: `#pragma once`, include order: project -> library -> STL.
- **`using namespace voxel::world;`**: OK in .cpp files and test files, never in headers.
- **Block constants**: `BLOCK_AIR = 0` from `Block.h`. Use it, don't hardcode 0.

### What NOT To Do

- **Do NOT modify the terrain fill pipeline** — caves are a post-pass that carves existing terrain. Don't change how bedrock/stone/biome-surface fill works.
- **Do NOT modify BiomeSystem, SplineCurve, or biome block selection** — cave carving is biome-agnostic, carves through any solid block.
- **Do NOT modify ChunkManager, ChunkSection, ChunkColumn, or PaletteCompression** — only WorldGenerator and CaveCarver are touched.
- **Do NOT add water/lava fill to caves** — water is a separate concern for a future story.
- **Do NOT add cave-specific decorations** (stalactites, mushrooms, ores) — decorations are Story 4.5 territory.
- **Do NOT attempt to fix floating blocks** — the epic explicitly states this is acceptable for V1. Floating surface blocks above carved caves are OK.
- **Do NOT add async/threading** — generation stays synchronous (async in Story 5.6).
- **Do NOT create a CaveManager or CaveSystem class** — `CaveCarver` is a single utility class called from WorldGenerator. No manager layer needed.
- **Do NOT add configurable cave parameters to config.json or Lua** — hardcoded constants for now. Configurability comes in Epic 9 (Lua scripting).
- **Do NOT add ravines or canyons** — only 3D noise caves and overhangs as specified. Ravines could be a future enhancement.
- **Do NOT carve caves in chunks loaded from disk** — `ChunkManager::loadChunk()` already has disk-priority logic from Story 3.7/4.1. Saved chunks are loaded as-is; generation (including cave carving) only runs for new chunks.

### Project Structure Notes

New files:
- `engine/include/voxel/world/CaveCarver.h` — class declaration
- `engine/src/world/CaveCarver.cpp` — implementation
- `tests/world/TestCaveCarver.cpp` — cave-specific tests

Modified files:
- `engine/include/voxel/world/WorldGenerator.h` — add `CaveCarver m_caveCarver` member
- `engine/src/world/WorldGenerator.cpp` — collect surface heights, call `m_caveCarver.carveColumn()` after terrain fill
- `engine/CMakeLists.txt` — add `src/world/CaveCarver.cpp`
- `tests/CMakeLists.txt` — add `tests/world/TestCaveCarver.cpp`
- `tests/world/TestWorldGenerator.cpp` — add cave integration tests

No new directories needed. All files fit the existing `voxel::world` module structure.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-04-terrain-generation.md — Story 4.4]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 6: World Generation, Noise Pipeline step 6]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 2: Async Chunk Pipeline stages]
- [Source: _bmad-output/implementation-artifacts/4-1-fastnoiselite-integration-basic-heightmap.md — FastNoiseLite integration pattern]
- [Source: _bmad-output/implementation-artifacts/4-2-spline-remapping-elevation-distribution.md — noise pipeline, height calculation]
- [Source: _bmad-output/implementation-artifacts/4-3-biome-system-whittaker-diagram.md — biome surface fill, WorldGenerator integration]
- [Source: engine/include/voxel/world/ChunkColumn.h — SECTIONS_PER_COLUMN=16, COLUMN_HEIGHT=256, setBlock(), getBlock()]
- [Source: engine/include/voxel/world/ChunkSection.h — SIZE=16, isEmpty(), BLOCK_AIR]
- [Source: engine/include/voxel/world/Block.h — BLOCK_AIR=0]
- [Source: CLAUDE.md — naming conventions, project structure, critical rules]
- [Reference: Danol — Voxel Cave Generation Using 3D Perlin Noise Isosurfaces](https://blog.danol.cz/voxel-cave-generation-using-3d-perlin-noise-isosurfaces/)
- [Reference: Voxel Tools — Procedural Generation (cave noise modulation)](https://voxel-tools.readthedocs.io/en/latest/procedural_generation/)
- [Reference: Accidental Noise — 3D Cube World Level Generation](https://accidentalnoise.sourceforge.net/minecraftworlds.html)

## Dev Agent Record

### Agent Model Used

(to be filled by dev agent)

### Debug Log References

### Completion Notes List

### File List