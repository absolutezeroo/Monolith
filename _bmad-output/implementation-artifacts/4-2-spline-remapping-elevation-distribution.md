# Story 4.2: Spline Remapping + Elevation Distribution

Status: ready-for-dev

## Story

As a **developer**,
I want spline curves to control how noise values map to terrain height,
so that I get distinct plains, hills, and mountains instead of uniform noise.

## Acceptance Criteria

1. **AC-1: SplineCurve class** — `SplineCurve` class in `voxel::world` namespace implements cubic Hermite interpolation with configurable control points `{noiseValue, heightValue, tangent}`.
2. **AC-2: Evaluate method** — `float evaluate(float noiseValue)` maps input `[-1, 1]` to height output. Values outside the control point range clamp to first/last point height.
3. **AC-3: Default spline profile** — Default control points produce: flat at low noise (plains, ~62–66), gentle rise (hills, ~70–90), steep rise (mountains, ~100–140), plateau at extreme (~145–150). Sea level conceptually at ~62.
4. **AC-4: Continent noise** — Replace Story 4.1's single-noise heightmap with continent noise: 2D Simplex, frequency 0.001, 4 octaves FBm. This produces the large-scale continental shape.
5. **AC-5: Detail noise** — Add a second detail noise layer: 2D Simplex, frequency 0.02, 4 octaves FBm, amplitude ~5–10 blocks. Added on top of spline-remapped continent height for local terrain variation.
6. **AC-6: Visual result** — Walking around the world shows distinct flat plains, rolling hills, and mountain ranges rather than uniform undulation.
7. **AC-7: Determinism preserved** — Same `(seed, chunkCoord)` still produces byte-identical output. Detail noise seeded deterministically from world seed (offset from continent noise seed).
8. **AC-8: Unit tests** — Catch2 tests for: SplineCurve evaluation correctness, monotonic output for monotonic control points, clamping at boundaries, full terrain determinism with new pipeline, height distribution statistics (verify plains/hills/mountains exist).

## Tasks / Subtasks

- [ ] **Task 1: Create SplineCurve class** (AC: 1, 2)
  - [ ] Create `engine/include/voxel/world/SplineCurve.h`
  - [ ] Create `engine/src/world/SplineCurve.cpp`
  - [ ] Define `ControlPoint` struct: `{ float noise; float height; float tangent; }`
  - [ ] Constructor: `explicit SplineCurve(std::vector<ControlPoint> points)` — sorted by noise ascending
  - [ ] `float evaluate(float noiseValue) const` — cubic Hermite interpolation between adjacent control points
  - [ ] Clamp behavior: values below first point return first height, above last return last height
  - [ ] Static factory: `SplineCurve createDefault()` — returns the standard terrain profile
  - [ ] Validate: assert >= 2 control points, sorted order

- [ ] **Task 2: Implement cubic Hermite interpolation** (AC: 1, 2)
  - [ ] Find surrounding control points for input value (binary search or linear scan — few points)
  - [ ] Compute `t` = local parameter in `[0, 1]` between adjacent points
  - [ ] Apply Hermite basis: `h00(t)*p0 + h10(t)*m0*dx + h01(t)*p1 + h11(t)*m1*dx`
  - [ ] Where `h00 = 2t³-3t²+1`, `h10 = t³-2t²+t`, `h01 = -2t³+3t²`, `h11 = t³-t²`
  - [ ] `dx` = distance between control point noise values (interval scaling for tangents)

- [ ] **Task 3: Define default terrain spline** (AC: 3)
  - [ ] Control points (approximate — tune visually):
    - `{-1.0, 62.0, 0.0}` — deep ocean floor / lowest plains
    - `{-0.4, 64.0, 5.0}` — sea level plains (flat region)
    - `{0.0, 68.0, 15.0}` — gentle rise starts (plains → hills transition)
    - `{0.3, 90.0, 40.0}` — hills
    - `{0.6, 120.0, 60.0}` — steep mountain rise
    - `{0.8, 140.0, 20.0}` — mountain peaks
    - `{1.0, 150.0, 0.0}` — plateau at extreme (flat mountaintops)
  - [ ] These values are starting points — visual tuning expected

- [ ] **Task 4: Modify WorldGenerator noise pipeline** (AC: 4, 5, 7)
  - [ ] Add second `FastNoiseLite` instance for detail noise (or reconfigure single instance)
  - [ ] Continent noise config: `NoiseType_OpenSimplex2`, `FractalType_FBm`, freq 0.001, octaves 4
  - [ ] Detail noise config: `NoiseType_OpenSimplex2`, `FractalType_FBm`, freq 0.02, octaves 4
  - [ ] Seed detail noise differently: use `seed + 1` or `seed ^ 0x12345` (deterministic offset)
  - [ ] New height calculation per (x, z):
    ```
    continentNoise = m_continentNoise.GetNoise(worldX, worldZ);  // [-1, 1]
    baseHeight = m_spline.evaluate(continentNoise);               // [62, 150]
    detailNoise = m_detailNoise.GetNoise(worldX, worldZ);        // [-1, 1]
    finalHeight = baseHeight + detailNoise * DETAIL_AMPLITUDE;    // ±5–10 blocks
    finalHeight = std::clamp(finalHeight, 1.0f, 254.0f);         // stay in column bounds
    ```
  - [ ] Replace the existing linear height mapping from Story 4.1
  - [ ] Keep surface composition logic unchanged (grass/dirt/stone/bedrock layering)

- [ ] **Task 5: Add SplineCurve member to WorldGenerator** (AC: 4)
  - [ ] Add `SplineCurve m_spline` member, initialized with `SplineCurve::createDefault()`
  - [ ] Add `FastNoiseLite m_continentNoise` and `FastNoiseLite m_detailNoise` members
  - [ ] Rename or repurpose the existing single `FastNoiseLite` from Story 4.1
  - [ ] Store `DETAIL_AMPLITUDE` as `static constexpr float` (start with 7.0f)

- [ ] **Task 6: Unit tests** (AC: 8)
  - [ ] Create `tests/world/TestSplineCurve.cpp`
  - [ ] Test: evaluate at exact control point returns that point's height
  - [ ] Test: evaluate between two linear points interpolates smoothly
  - [ ] Test: evaluate below min clamps to first height
  - [ ] Test: evaluate above max clamps to last height
  - [ ] Test: monotonically increasing control points produce monotonically increasing output (sample many points)
  - [ ] Test: default spline maps -1.0 → ~62, 0.0 → ~68, 1.0 → ~150
  - [ ] Add to `tests/world/TestWorldGenerator.cpp`:
    - Test: determinism still holds with new pipeline
    - Test: height distribution — generate 1000 columns, verify some are in [60–70] (plains), some in [80–100] (hills), some in [120+] (mountains)
    - Test: heights stay within [1, 254] bounds

- [ ] **Task 7: CMake wiring** (AC: 1)
  - [ ] Add `src/world/SplineCurve.cpp` to `engine/CMakeLists.txt` source list
  - [ ] Add `tests/world/TestSplineCurve.cpp` to `tests/CMakeLists.txt`

## Dev Notes

### Architecture Constraints

- **No exceptions** (ADR-008). SplineCurve constructor should use `VX_ASSERT` for validation (programmer error if control points are wrong), not Result<T>.
- **Chunks NOT in ECS** — WorldGenerator remains outside ECS.
- **Namespace**: `voxel::world` for both `SplineCurve` and modified `WorldGenerator`.
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, `SCREAMING_SNAKE` for constants.
- **One class per file**, max ~500 lines. SplineCurve in its own file pair.

### Noise Parameter Discrepancy

The epic specifies continent noise at "freq 0.001, 4 octaves" while the architecture doc specifies "freq 0.001, 6 octaves". **Use the epic's values (4 octaves)** — the architecture describes the full pipeline vision, and 4 octaves for continent noise is sufficient at this stage. The architecture's "6 octaves" may refer to a later-refined version. If the result looks too smooth, increase to 6 octaves during visual tuning.

### Cubic Hermite Spline — Implementation Reference

This is a standard **Catmull-Rom variant** with explicit tangents (not derived from neighboring points). The Hermite basis functions are:

```
h00(t) = 2t³ - 3t² + 1       // value at start
h10(t) = t³ - 2t² + t         // tangent at start
h01(t) = -2t³ + 3t²           // value at end
h11(t) = t³ - t²              // tangent at end

result = h00*p0 + h10*m0*dx + h01*p1 + h11*m1*dx
```

Where `p0/p1` are heights, `m0/m1` are tangents, `dx = noise1 - noise0` (interval width, scales tangent influence). Tangents are specified in "height units per noise unit" at each control point.

The control point tangents control the curve shape:
- tangent = 0 → flat at that point (plateau)
- small tangent → gentle slope
- large tangent → steep slope

### What Story 4.1 Creates (Prerequisite)

Story 4.1 (not yet implemented) will create:
- `WorldGenerator` class with `generateChunkColumn(glm::ivec2) → ChunkColumn`
- Single `FastNoiseLite` instance: 2D Simplex, freq 0.01, 6 octaves, linear mapping to [40, 120]
- Surface composition: bedrock(0), stone(1..h-4), dirt(h-3..h-1), grass(h)
- Block ID lookup via `BlockRegistry::getIdByName()` cached as members (`m_stoneId`, etc.)
- Seed management, spawn point, ChunkManager integration
- Test file: `tests/world/TestWorldGenerator.cpp`

**This story modifies WorldGenerator, not replaces it.** The surface composition, block ID caching, spawn point, and ChunkManager integration all remain unchanged. Only the height calculation changes.

### Specific Modifications to WorldGenerator

1. **Replace single noise** → two noise instances (`m_continentNoise`, `m_detailNoise`)
2. **Add SplineCurve member** → `m_spline` initialized from `SplineCurve::createDefault()`
3. **Change height computation** in `generateChunkColumn()`:
   - Before (4.1): `height = 40 + (noise01) * 80` (linear)
   - After (4.2): `height = spline.evaluate(continentNoise) + detailNoise * DETAIL_AMPLITUDE`
4. **Update noise config** in constructor:
   - Continent: freq 0.001 (was 0.01), octaves 4 (was 6)
   - Detail: freq 0.02, octaves 4, seed offset
5. **Height range changes**: [40, 120] → approximately [55, 160] with clamping to [1, 254]

### Spawn Point Impact

`findSpawnPoint()` still works — it queries heightmap at (0,0) and spiral-walks. The height values will be different (likely in plains range ~62–68 near origin since continent noise at small coords is mid-range). No changes needed to spawn logic itself.

### Test Updates

- Story 4.1 tests check height bounds [40, 120] — **update these bounds** in TestWorldGenerator.cpp to match the new range
- Story 4.1 determinism test still applies — same seed must produce same output
- Add new distribution test to verify terrain variety (not just uniform elevation)

### Existing Code Patterns (from Epic 3)

- **Test file convention**: `tests/world/TestXxx.cpp`, uses `#include <catch2/catch_test_macros.hpp>`, `TEST_CASE("description", "[tag]")` with `SECTION` blocks.
- **CMake**: Source files explicitly listed in `engine/CMakeLists.txt` (no GLOB).
- **Header guards**: `#pragma once`.
- **Includes**: Project headers first (`"voxel/..."`), then library headers (`<glm/...>`), then STL.
- **std::vector for small collections**: SplineCurve control points are few (5–10), vector is fine.

### What NOT To Do

- **Do NOT make SplineCurve a template** — control points are always `float`. Keep it simple.
- **Do NOT add runtime spline editing / hot-reload** — that's future Lua scripting work (Epic 9).
- **Do NOT add biome-specific splines yet** — Story 4.3 introduces biomes. This story uses one global spline.
- **Do NOT change surface composition** — grass/dirt/stone/bedrock layering stays the same.
- **Do NOT add water/ocean generation** — even though the spline has low points near ~62, water fill is a separate concern.
- **Do NOT modify ChunkManager, ChunkSection, or ChunkColumn** — only WorldGenerator changes.
- **Do NOT add async/threading** — generation remains synchronous (async in Story 5.6).

### Project Structure Notes

- New files: `SplineCurve.h` / `SplineCurve.cpp` in `voxel/world/` — consistent with existing module
- Modified files: `WorldGenerator.h` / `WorldGenerator.cpp` (add members, change height calc)
- New test file: `TestSplineCurve.cpp` in `tests/world/`
- Modified test file: `TestWorldGenerator.cpp` (update bounds, add distribution test)
- No new directories needed

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-04-terrain-generation.md — Story 4.2]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 6: World Generation, Noise Pipeline]
- [Source: _bmad-output/implementation-artifacts/4-1-fastnoiselite-integration-basic-heightmap.md — predecessor story]
- [Source: engine/include/voxel/world/ChunkColumn.h — SECTIONS_PER_COLUMN=16, COLUMN_HEIGHT=256]
- [Source: engine/include/voxel/world/ChunkSection.h — SIZE=16, setBlock(), fill()]
- [Source: engine/include/voxel/world/ChunkManager.h — loadChunk(), spatial hash, coordinate utils]
- [Source: CLAUDE.md — naming conventions, project structure, critical rules]

## Dev Agent Record

### Agent Model Used

(to be filled by dev agent)

### Debug Log References

### Completion Notes List

### File List