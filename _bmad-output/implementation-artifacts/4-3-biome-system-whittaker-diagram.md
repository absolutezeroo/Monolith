# Story 4.3: Biome System (Whittaker Diagram)

Status: ready-for-dev

## Story

As a **developer**,
I want biome selection based on temperature and humidity noise maps,
so that different areas of the world have distinct surface blocks and features.

## Acceptance Criteria

1. **AC-1: Climate noise maps** — Two independent 2D noise maps added to `WorldGenerator`: temperature (OpenSimplex2,
   freq 0.005, 4 octaves FBm) and humidity (OpenSimplex2, freq 0.005, 4 octaves FBm), each with a unique deterministic
   seed offset from the world seed.
2. **AC-2: BiomeType enum** — `enum class BiomeType : uint8_t` with minimum 8 biomes: Desert, Savanna, Plains, Forest,
   Jungle, Taiga, Tundra, IcePlains.
3. **AC-3: Whittaker diagram lookup** — `BiomeType getBiome(float temperature, float humidity)` maps normalized climate
   values to biome type via a discretized Whittaker-style lookup table.
4. **AC-4: Per-biome definitions** — Each biome defines: surface block, sub-surface block, filler block (stone variant),
   surface depth (dirt layer thickness), and a height modifier (spline weight or additive offset).
5. **AC-5: Biome blending** — At each column (x,z), sample biomes in a 5x5 area, compute distance-weighted
   contributions, blend height values so biome boundaries produce smooth terrain transitions. Weights normalized to sum
   to 1.0.
6. **AC-6: WorldGenerator integration** — `WorldGenerator::generateChunkColumn()` uses biome data to select
   surface/sub-surface blocks instead of hardcoded grass/dirt. Height distribution modified per biome. Existing
   bedrock/stone fill logic preserved.
7. **AC-7: Determinism** — Same `(seed, chunkCoord)` produces byte-identical output. Temperature/humidity noise seeded
   deterministically. Biome selection is a pure function of noise values.
8. **AC-8: Unit tests** — Catch2 tests for: biome selection determinism, Whittaker lookup coverage (all 8 biomes
   reachable), blending weight normalization, surface block correctness per biome, terrain determinism preserved.

## Tasks / Subtasks

- [ ] **Task 1: Create BiomeTypes header** (AC: 2, 4)
    - [ ] Create `engine/include/voxel/world/BiomeTypes.h`
    - [ ] Define
      `enum class BiomeType : uint8_t { Desert, Savanna, Plains, Forest, Jungle, Taiga, Tundra, IcePlains, Count }`
    - [ ] Define `struct BiomeDefinition` with fields:
        - `BiomeType type`
        - `std::string_view surfaceBlock` — string ID, e.g. `"base:sand"`
        - `std::string_view subSurfaceBlock` — e.g. `"base:sand"` or `"base:dirt"`
        - `std::string_view fillerBlock` — e.g. `"base:stone"` (below sub-surface)
        - `int surfaceDepth` — how many sub-surface layers (default 3)
        - `float heightModifier` — additive offset to blended terrain height
        - `float heightScale` — multiplicative factor on detail noise amplitude per biome
    - [ ] Define `const BiomeDefinition& getBiomeDefinition(BiomeType type)` — returns from static array

- [ ] **Task 2: Create BiomeSystem class** (AC: 1, 3, 5)
    - [ ] Create `engine/include/voxel/world/BiomeSystem.h`
    - [ ] Create `engine/src/world/BiomeSystem.cpp`
    - [ ] Constructor: `explicit BiomeSystem(uint64_t seed)`
    - [ ] Members: `FastNoiseLite m_temperatureNoise`, `FastNoiseLite m_humidityNoise`
    - [ ] Temperature noise config: `NoiseType_OpenSimplex2`, `FractalType_FBm`, freq 0.005, octaves 4, seed =
      `static_cast<int>(seed + 2)` (offset from continent=seed, detail=seed+1)
    - [ ] Humidity noise config: same params, seed = `static_cast<int>(seed + 3)`
    - [ ] Method: `BiomeType getBiomeAt(float worldX, float worldZ) const` — samples temperature + humidity, calls
      Whittaker lookup
    - [ ] Method: `BiomeType classifyBiome(float temperature, float humidity) const` — pure Whittaker diagram function
    - [ ] Method: `BlendedBiome getBlendedBiomeAt(float worldX, float worldZ) const` — 5x5 sampling + distance-weighted
      blending

- [ ] **Task 3: Implement Whittaker diagram lookup** (AC: 3)
    - [ ] Both temperature and humidity noise return [-1, 1] — remap to [0, 1] for lookup: `t = (raw + 1) * 0.5f`
    - [ ] Discretized lookup table approach (rectangular grid, not triangle):
      ```
      Humidity →  Low (0–0.25)    Med-Low (0.25–0.5)  Med-High (0.5–0.75)  High (0.75–1.0)
      Temp ↓
      Cold  (0–0.25)    IcePlains        Tundra              Tundra              Taiga
      Cool  (0.25–0.5)  Tundra           Plains              Taiga               Taiga
      Warm  (0.5–0.75)  Desert           Savanna             Plains              Forest
      Hot   (0.75–1.0)  Desert           Savanna             Jungle              Jungle
      ```
    - [ ] Implement as `static constexpr BiomeType WHITTAKER_TABLE[4][4]` — index by `int(temp * 3.99f)` and
      `int(humidity * 3.99f)`, clamped to [0,3]
    - [ ] This ensures all 8 biome types are reachable from some (temp, humidity) combination

- [ ] **Task 4: Implement biome blending** (AC: 5)
    - [ ] Define `struct BlendedBiome`:
        - `BiomeType primaryBiome` — biome with highest weight (for surface block selection)
        - `float blendedHeightModifier` — weighted average of height modifiers
        - `float blendedHeightScale` — weighted average of height scales
        - `float blendedSurfaceDepth` — weighted average of surface depths (round to int when applied)
    - [ ] `getBlendedBiomeAt(worldX, worldZ)` algorithm:
        1. Sample 5x5 grid of biomes centered on (worldX, worldZ), step size = 4 blocks (covers ~20-block radius)
        2. For each sample: get `BiomeType`, fetch `BiomeDefinition`, compute weight = `1.0f / (distSq + 1.0f)` where
           `distSq` = squared distance from center
        3. Accumulate per-biome weights; the center sample gets `distSq = 0` so weight = 1.0 (strongest)
        4. Normalize all weights so they sum to 1.0
        5. Compute weighted averages of `heightModifier`, `heightScale`, `surfaceDepth`
        6. `primaryBiome` = biome type with highest total accumulated weight
    - [ ] Center sample dominates (weight 1.0 vs ~0.06 for corners) — transitions are gradual, not jumpy

- [ ] **Task 5: Define per-biome block palettes** (AC: 4)
    - [ ] Static array `BIOME_DEFINITIONS[static_cast<size_t>(BiomeType::Count)]`:
      ```
      Desert:    surface=sand,       subSurface=sand,       filler=sandstone, depth=4, heightMod=-5, heightScale=0.5
      Savanna:   surface=grass_block, subSurface=dirt,       filler=stone,     depth=3, heightMod=0,  heightScale=0.8
      Plains:    surface=grass_block, subSurface=dirt,       filler=stone,     depth=3, heightMod=0,  heightScale=1.0
      Forest:    surface=grass_block, subSurface=dirt,       filler=stone,     depth=3, heightMod=3,  heightScale=1.2
      Jungle:    surface=grass_block, subSurface=dirt,       filler=stone,     depth=4, heightMod=5,  heightScale=1.5
      Taiga:     surface=grass_block, subSurface=dirt,       filler=stone,     depth=3, heightMod=2,  heightScale=1.0
      Tundra:    surface=snow_block,  subSurface=dirt,       filler=stone,     depth=2, heightMod=-2, heightScale=0.6
      IcePlains: surface=snow_block,  subSurface=snow_block, filler=stone,     depth=3, heightMod=-3, heightScale=0.4
      ```
    - [ ] Block string IDs use `base:` namespace prefix — must match what's registered in BlockRegistry
    - [ ] New block types needed (if not already registered): `base:sand`, `base:sandstone`, `base:snow_block`
    - [ ] Add JSON block definitions for any missing blocks in `assets/blocks/`

- [ ] **Task 6: Integrate BiomeSystem into WorldGenerator** (AC: 6, 7)
    - [ ] Add `BiomeSystem m_biomeSystem` member to `WorldGenerator`, initialized with same seed
    - [ ] In `generateChunkColumn()`, for each (x, z) column:
        1. Compute world coords: `worldX = chunkCoord.x * 16 + x`, `worldZ = chunkCoord.y * 16 + z`
        2. Call `m_biomeSystem.getBlendedBiomeAt(worldX, worldZ)` → `BlendedBiome`
        3. Existing pipeline: `continentNoise → spline → baseHeight`, `detailNoise → variation`
        4. Apply biome:
           `finalHeight = baseHeight + blendedBiome.blendedHeightModifier + detailNoise * DETAIL_AMPLITUDE * blendedBiome.blendedHeightScale`
        5. Clamp `finalHeight` to [1, 254]
        6. Surface fill: use `primaryBiome`'s surface/subSurface/filler blocks instead of hardcoded grass/dirt/stone
        7. Surface depth: use `blendedSurfaceDepth` (rounded to int) instead of hardcoded 3
        8. Bedrock at y=0 unchanged
    - [ ] Cache biome block IDs at WorldGenerator construction: resolve all biome string IDs to numeric IDs via
      `BlockRegistry::getIdByName()`. Store in a lookup array indexed by `BiomeType`.
    - [ ] Keep `m_stoneId`, `m_bedrockId` from Story 4.1 — these are still used for filler/bedrock
    - [ ] Add per-biome cached IDs: `struct BiomeBlockIds { uint16_t surface; uint16_t subSurface; uint16_t filler; };`
      array of size `BiomeType::Count`

- [ ] **Task 7: Register new block types** (AC: 4, 6)
    - [ ] Create `assets/blocks/sand.json`, `assets/blocks/sandstone.json`, `assets/blocks/snow_block.json` if not
      already present
    - [ ] Minimal JSON: `{ "stringId": "base:sand", "isSolid": true, "hasCollision": true, "hardness": 0.5 }` (follow
      existing block JSON patterns)
    - [ ] These blocks need to exist before WorldGenerator resolves IDs — BlockRegistry loaded before WorldGenerator
      construction

- [ ] **Task 8: Unit tests** (AC: 8)
    - [ ] Create `tests/world/TestBiomeSystem.cpp`
    - [ ] Test: `classifyBiome()` returns all 8 biome types for appropriate (temp, humidity) values
    - [ ] Test: `getBiomeAt()` determinism — same seed + coords = same biome, always
    - [ ] Test: `getBlendedBiomeAt()` weights sum to ~1.0 (within float tolerance)
    - [ ] Test: blended height modifier is within range of defined biome modifiers
    - [ ] Test: different seeds produce different biome maps
    - [ ] Add to `tests/world/TestWorldGenerator.cpp`:
        - Test: surface blocks match expected biome (generate columns at known biome locations, verify top block type)
        - Test: terrain determinism preserved with biome system
        - Test: biome boundaries produce smooth height transitions (sample across a boundary, verify no height jumps > ~
          5 blocks between adjacent columns)
    - [ ] Tag all biome tests with `[world][biome]`

- [ ] **Task 9: CMake wiring** (AC: all)
    - [ ] Add `src/world/BiomeSystem.cpp` to `engine/CMakeLists.txt` source list
    - [ ] Add `tests/world/TestBiomeSystem.cpp` to `tests/CMakeLists.txt`

## Dev Notes

### Architecture Constraints

- **No exceptions** (ADR-008). BiomeSystem uses `VX_ASSERT` for programmer errors (invalid enum values). No `Result<T>`
  needed — biome lookup is infallible.
- **Chunks NOT in ECS** (ADR-004) — WorldGenerator and BiomeSystem live outside ECS.
- **Namespace**: `voxel::world` for BiomeTypes, BiomeSystem, and all modifications.
- **Naming**: PascalCase classes/enums, camelCase methods, `m_` prefix members, `SCREAMING_SNAKE` constants.
- **One class per file**, max ~500 lines. BiomeTypes.h is a header-only definitions file (no .cpp). BiomeSystem gets its
  own .h/.cpp pair.
- **Data-driven content**: Block definitions in JSON, not hardcoded in C++. Biome block assignments use string IDs
  resolved at runtime.

### What Stories 4.1 and 4.2 Create (Prerequisites)

Story 4.1 creates:

- `WorldGenerator(uint64_t seed)` with `generateChunkColumn(glm::ivec2) → ChunkColumn`
- Single `FastNoiseLite` instance, seed management, spawn point, ChunkManager integration
- Cached block IDs: `m_stoneId`, `m_dirtId`, `m_grassId`, `m_bedrockId`
- Surface fill: bedrock(0), stone(1..h-4), dirt(h-3..h-1), grass(h)
- Test: `tests/world/TestWorldGenerator.cpp`

Story 4.2 creates:

- `SplineCurve` class with `evaluate(float) → float`, `createDefault()`
- WorldGenerator modified: `m_continentNoise` (freq 0.001, 4 oct), `m_detailNoise` (freq 0.02, 4 oct), `m_spline`
- Height = `spline.evaluate(continentNoise) + detailNoise * DETAIL_AMPLITUDE`
- `DETAIL_AMPLITUDE = 7.0f` as `static constexpr`
- Test: `tests/world/TestSplineCurve.cpp`

**This story builds on top of both. Do NOT recreate or replace existing WorldGenerator logic — extend it.**

### FastNoiseLite Seed Management

The world seed cascades to noise instances with deterministic offsets:

- Continent noise: `seed` (set by Story 4.1)
- Detail noise: `seed + 1` (set by Story 4.2)
- Temperature noise: `seed + 2` (this story)
- Humidity noise: `seed + 3` (this story)

All cast via `static_cast<int>(seed + N)` — FastNoiseLite takes `int` seeds.

### Whittaker Lookup — Design Rationale

A 4x4 rectangular grid is used instead of a true Whittaker triangle because:

1. Simpler to implement (2D array index vs polygon containment)
2. All biomes reachable by construction
3. Easy to extend — just add rows/columns for more biome granularity later
4. Matches the approach described in Azgaar's Fantasy Map Generator and the AutoBiomes paper

The noise values [-1, 1] are remapped to [0, 1] before lookup. The `* 3.99f` trick avoids out-of-bounds when value ==
1.0.

### Biome Blending — Implementation Details

The 5x5 sampling grid with 4-block spacing covers a ~20-block radius. This means:

- Deep inside a biome: 25/25 samples are the same biome → no blending (fast path possible)
- At boundaries: gradual transition over ~16–20 blocks
- Inverse-distance weighting: `w = 1 / (d² + 1)` gives center dominance while providing smooth falloff

**Performance note**: 25 noise lookups per column × 256 columns per chunk = 6400 extra noise samples per chunk.
FastNoiseLite is fast (~50ns per sample) so this adds ~0.3ms per chunk. Acceptable for synchronous generation; can be
cached per-chunk if profiling shows issues.

**Optimization opportunity (implement only if needed)**: Cache biome per 4x4 block area since biomes don't change at
sub-chunk resolution. This reduces lookups from 256 to 16 per chunk for the primary biome map.

### Surface Block Selection Logic

After biome blending, the column fill logic becomes:

```
y = 0             → bedrock (unchanged)
y = 1..h-depth-1  → filler block (biome-specific, usually stone)
y = h-depth..h-1  → sub-surface block (biome-specific, e.g. dirt or sand)
y = h             → surface block (biome-specific, e.g. grass or sand)
y > h             → air (default)
```

Where `h` = computed height, `depth` = blended surface depth (rounded to int, clamped to [1, h-1]).

Edge case: if `h < depth + 1`, reduce depth so there's at least 1 layer of filler above bedrock.

### Block Registration Requirements

Blocks that MUST exist in `BlockRegistry` before WorldGenerator runs:

- Already expected by Story 4.1: `base:stone`, `base:dirt`, `base:grass_block`, `base:bedrock`
- New for this story: `base:sand`, `base:sandstone`, `base:snow_block`

If `getIdByName()` returns `BLOCK_AIR` for a biome block, log `VX_LOG_WARN` and fall back to `m_stoneId` for filler,
`m_dirtId` for sub-surface, `m_grassId` for surface. This prevents crashes from missing block definitions.

### Existing Code Patterns (from Epic 3 & Story 4.1/4.2)

- **Test file convention**: `tests/world/TestXxx.cpp`, `#include <catch2/catch_test_macros.hpp>`,
  `TEST_CASE("desc", "[world][tag]")` + `SECTION` blocks.
- **CMake**: Source files explicitly listed — no GLOB. Add to `engine/CMakeLists.txt` and `tests/CMakeLists.txt`.
- **Headers**: `#pragma once`, include order: project → library → STL.
- **FastNoiseLite include**: wrap with `#pragma warning(push, 0)` / `#pragma warning(pop)` in .cpp files (MSVC `/W4`
  compat, established in Story 4.1).
- **`using namespace voxel::world;`**: OK in .cpp files and test files, never in headers.
- **Block JSON pattern**: See existing files in `assets/blocks/` for field names and format.

### What NOT To Do

- **Do NOT create a separate BiomeManager class** — BiomeSystem is sufficient. No need for another manager layer.
- **Do NOT add per-biome spline curves yet** — use the global spline from Story 4.2 with per-biome height modifiers and
  scale factors. Per-biome splines are a future enhancement.
- **Do NOT add trees, structures, or decorations** — that's Story 4.5. Biome `decoration rules` in the struct are
  placeholder fields for 4.5 to populate.
- **Do NOT add water/ocean fill** — even though Desert biomes exist and some heights are low. Water is a separate
  concern.
- **Do NOT add 3D cave carving** — that's Story 4.4. Only 2D heightmap + biome surface is this story's scope.
- **Do NOT modify ChunkManager, ChunkSection, ChunkColumn, or PaletteCompression** — only WorldGenerator, BiomeSystem,
  and BiomeTypes are touched.
- **Do NOT add async/threading** — generation stays synchronous (async in Story 5.6).
- **Do NOT make BiomeSystem configurable via Lua** — that's Epic 9 scripting territory.
- **Do NOT add biome-dependent noise parameters to continent/detail noise** — keep the existing noise pipeline from 4.2
  intact. Biome modifies height via additive offset + detail amplitude scale only.

### Project Structure Notes

New files:

- `engine/include/voxel/world/BiomeTypes.h` — enum + BiomeDefinition struct (header-only)
- `engine/include/voxel/world/BiomeSystem.h` — class declaration
- `engine/src/world/BiomeSystem.cpp` — implementation
- `tests/world/TestBiomeSystem.cpp` — biome-specific tests
- `assets/blocks/sand.json` — new block definition
- `assets/blocks/sandstone.json` — new block definition
- `assets/blocks/snow_block.json` — new block definition

Modified files:

- `engine/include/voxel/world/WorldGenerator.h` — add `BiomeSystem m_biomeSystem` member, `BiomeBlockIds` array
- `engine/src/world/WorldGenerator.cpp` — use biome data in surface fill logic
- `engine/CMakeLists.txt` — add `src/world/BiomeSystem.cpp`
- `tests/CMakeLists.txt` — add `tests/world/TestBiomeSystem.cpp`
- `tests/world/TestWorldGenerator.cpp` — add biome integration tests

No new directories needed. All files fit the existing `voxel::world` module structure.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-04-terrain-generation.md — Story 4.3]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 6: World Generation, Noise Pipeline]
- [Source: _bmad-output/implementation-artifacts/4-1-fastnoiselite-integration-basic-heightmap.md — Story 4.1 context]
- [Source: _bmad-output/implementation-artifacts/4-2-spline-remapping-elevation-distribution.md — Story 4.2 context]
- [Source: engine/include/voxel/world/ChunkColumn.h — SECTIONS_PER_COLUMN=16, COLUMN_HEIGHT=256]
- [Source: engine/include/voxel/world/ChunkSection.h — SIZE=16, setBlock(), fill()]
- [Source: engine/include/voxel/world/BlockRegistry.h — getIdByName(), loadFromJson()]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition, BLOCK_AIR]
- [Source: CLAUDE.md — naming conventions, project structure, critical rules]
- [Reference: Whittaker Diagram — PCG Wiki](http://pcg.wikidot.com/pcg-algorithm:whittaker-diagram)
- [Reference: Azgaar — Biomes Generation and Rendering](https://azgaar.wordpress.com/2017/06/30/biomes-generation-and-rendering/)
- [Reference: Fast Biome Blending Without Squareness — NoisePosti.ng](https://noiseposti.ng/posts/2021-03-13-Fast-Biome-Blending-Without-Squareness.html)
- [Reference: Red Blob Games — Making Maps with Noise](https://www.redblobgames.com/maps/terrain-from-noise/)

## Dev Agent Record

### Agent Model Used

(to be filled by dev agent)

### Debug Log References

### Completion Notes List

### File List
