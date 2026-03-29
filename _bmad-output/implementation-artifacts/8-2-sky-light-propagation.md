# Story 8.2: Sky Light Propagation

Status: ready-for-dev

## Story

As a developer,
I want sky light to propagate from the surface downward and horizontally,
so that outdoor areas and caves near the surface are naturally lit.

## Acceptance Criteria

1. Sky light seeded at 15 for all blocks directly exposed to sky (no solid block above in the column)
2. Downward propagation: sky light travels straight down without attenuation through air/transparent blocks
3. Horizontal propagation: BFS with -1 attenuation per horizontal step (like block light)
4. Under overhangs: sky light attenuates horizontally from the overhang edge
5. In caves: sky light does NOT penetrate unless there is an opening to the surface
6. Heightmap per column: tracks highest solid block per (x,z) for fast sky exposure check
7. Cross-section boundary propagation within a column (e.g., y=15 in section 5 → y=0 in section 6)
8. Cross-chunk boundary propagation at chunk borders (when neighbor chunks are loaded)
9. Unit tests: open air = 15, 3 blocks under overhang = 12 (3 horizontal steps), sealed cave = 0

## Tasks / Subtasks

- [ ] Task 1: Add Heightmap to ChunkColumn (AC: #6)
  - [ ] 1.1 Add `std::array<uint8_t, 256> m_heightMap` to ChunkColumn (16x16 = 256 entries, stores highest opaque block Y per column)
  - [ ] 1.2 Add `getHeight(int x, int z)` / `setHeight(int x, int z, uint8_t y)` accessors
  - [ ] 1.3 Add `buildHeightMap()` method that scans top-down per (x,z) to find highest opaque block
  - [ ] 1.4 Call `buildHeightMap()` once after world generation completes for the column (after `WorldGen::generateChunkColumn`)

- [ ] Task 2: Create SkyLightPropagator class (AC: #1, #2, #3, #4, #5)
  - [ ] 2.1 Create `engine/include/voxel/world/SkyLightPropagator.h`
  - [ ] 2.2 Create `engine/src/world/SkyLightPropagator.cpp`
  - [ ] 2.3 Implement `static void propagateColumn(ChunkColumn& column, const BlockRegistry& registry)`:
    - Phase 1 — Seed: For each (x,z), scan from y=255 down. Set sky=15 for every air/transparent block until hitting an opaque block (y <= heightMap[x,z] stops)
    - Phase 2 — Horizontal BFS: Queue all sky-lit blocks at light=15 that have a non-sky-lit neighbor. BFS with -1 attenuation per step. Stop at 0 or opaque blocks (lightFilter == 15)
  - [ ] 2.4 Use `std::queue<LightNode>` where `LightNode = {int16_t x, y, z; uint8_t level}` (same pattern as BlockLightPropagator from Story 8.1)
  - [ ] 2.5 Respect `BlockDefinition::lightFilter` — opaque blocks (lightFilter == 15) block sky light completely; transparent blocks (lightFilter < 15) allow propagation

- [ ] Task 3: Cross-section boundary propagation (AC: #7)
  - [ ] 3.1 Sky light seeding (Phase 1) already crosses sections naturally since it scans by world Y (0–255)
  - [ ] 3.2 BFS (Phase 2) must handle section transitions: when y crosses a 16-block boundary (y % 16 == 0 or 15), read/write the adjacent section's LightMap
  - [ ] 3.3 Helper: convert world Y → (sectionY, localY) using `sectionY = y / 16`, `localY = y % 16`

- [ ] Task 4: Cross-chunk boundary propagation (AC: #8)
  - [ ] 4.1 Add `static void propagateFromNeighbor(ChunkColumn& column, const ChunkColumn& neighbor, int face, const BlockRegistry& registry)` — propagates sky light across chunk borders
  - [ ] 4.2 For each border block on the shared face, check if neighbor has higher sky light than current -1. If so, seed BFS from the border into the column
  - [ ] 4.3 Only called when neighbor column is already loaded and sky-lit. Integrate with ChunkManager load order

- [ ] Task 5: Integrate into chunk loading pipeline (AC: #1–#8)
  - [ ] 5.1 In ChunkManager's load pipeline, call `SkyLightPropagator::propagateColumn()` AFTER `BlockLightPropagator::propagateColumn()` (from Story 8.1)
  - [ ] 5.2 After sky light propagation, check loaded neighbors. Call `propagateFromNeighbor()` for any neighbor that needs border re-propagation
  - [ ] 5.3 Mark affected sections dirty so meshing picks up the new light data

- [ ] Task 6: Unit tests (AC: #9)
  - [ ] 6.1 Create `tests/world/TestSkyLightPropagator.cpp`
  - [ ] 6.2 Test: flat surface open air — all blocks above surface = sky 15, blocks below surface = sky 0
  - [ ] 6.3 Test: overhang (solid roof 3 blocks from edge) — blocks under overhang at edge = 14, two steps in = 13, three steps = 12
  - [ ] 6.4 Test: sealed cave (no opening to surface) — all blocks inside = sky 0
  - [ ] 6.5 Test: vertical shaft — sky light propagates straight down through shaft = 15
  - [ ] 6.6 Test: cross-section boundary — sky light propagates from section Y=10 into section Y=9 correctly
  - [ ] 6.7 Test: transparent blocks (leaves, water) allow sky light through with attenuation based on lightFilter
  - [ ] 6.8 Test: heightmap accuracy — `getHeight(x,z)` matches actual highest opaque block

- [ ] Task 7: Update CMakeLists.txt
  - [ ] 7.1 Add `SkyLightPropagator.cpp` to engine sources
  - [ ] 7.2 Add `TestSkyLightPropagator.cpp` to test sources

## Dev Notes

### Critical: Builds on Story 8.1

Story 8.1 establishes foundational infrastructure this story depends on:
- **LightMap storage in ChunkColumn** — `std::array<LightMap, SECTIONS_PER_COLUMN> m_lightMaps` (always present, 64KB/column). Access via `getLightMap(sectionY)`.
- **BlockLightPropagator** class — BFS pattern to follow. Use same `LightNode` struct and queue approach.
- **Pipeline integration point** — block light runs after world gen. Sky light slots in immediately after.
- **Mesh snapshot population** — `hasLightData = true` and `hasNeighborLight` flags already set by 8.1.

### DO NOT Recreate Existing Infrastructure

| Component | Status | Location |
|-----------|--------|----------|
| `LightMap` class | DONE (8.0) | `engine/include/voxel/world/LightMap.h` (header-only) |
| `LightMap::getSkyLight/setSkyLight` | DONE | Sky nibble = high 4 bits of packed byte |
| `BlockDefinition::lightEmission/lightFilter` | DONE | `engine/include/voxel/world/Block.h` |
| `lightFilter` values in JSON | DONE | `assets/scripts/base/blocks.json` |
| `MeshJobInput` light snapshot fields | DONE (8.0) | `engine/include/voxel/renderer/MeshJobTypes.h` |
| `MeshBuilder::buildLightPad/computeFaceLight` | DONE (8.0) | `engine/src/renderer/MeshBuilder.cpp` |
| Shader `fragSkyLight`/`fragBlockLight` | DONE (8.0) | `assets/shaders/chunk.vert` (lines 251–283) |
| LightMap storage in ChunkColumn | From 8.1 | `m_lightMaps` array with `getLightMap()` accessor |
| BlockLightPropagator | From 8.1 | BFS block light — follow this pattern for sky light |
| `createMeshSnapshot()` populating light | From 8.1 | Copies LightMap data into MeshJobInput |

### Sky Light Algorithm (Two Phases)

**Phase 1 — Column Seed (per x,z column):**
```
for each (x, z) in [0..15]:
    heightY = heightMap[x * 16 + z]  // or z * 16 + x, match ChunkSection indexing
    for y = 255 down to 0:
        block = getBlock(x, y, z)
        if block is opaque (lightFilter == 15 for non-air solid):
            break  // stop seeding this column
        setSkyLight(x, y, z, 15)  // full sky light, no attenuation downward
```

**Phase 2 — Horizontal BFS:**
```
queue = all blocks with sky=15 that have at least one non-sky-lit neighbor
while queue not empty:
    node = queue.pop()
    for each of 6 neighbors (±x, ±y, ±z):
        if neighbor is opaque → skip
        newLevel = node.level - 1
        if neighbor is Y-axis and direction is downward:
            newLevel = node.level  // No attenuation going DOWN
        if newLevel > neighbor.currentSkyLight:
            neighbor.setSkyLight(newLevel)
            queue.push(neighbor)
```

**Key subtlety**: Downward propagation during BFS preserves full sky light value (no -1 attenuation going down). This means overhangs get proper shadow while open shafts remain at 15 all the way down. Horizontal and upward spread attenuate by -1 per step.

### lightFilter Semantics in blocks.json

| Block | lightFilter | Behavior |
|-------|------------|----------|
| air, glass, torch, rose, dandelion, tall_grass, short_grass, fern | 0 | Fully transparent — sky light passes unattenuated |
| leaves (oak, birch, spruce, jungle) | 1 | Slight attenuation (subtract 1 extra) |
| water | 2 | More attenuation (subtract 2 extra) |
| stone, dirt, grass, sand, gravel, log, planks, snow, bedrock, ores | 15 | Fully opaque — blocks ALL sky light |

For Story 8.2, treat `lightFilter` as: if `lightFilter == 15` → fully opaque (block light). If `lightFilter < 15` → transparent for propagation purposes. Do NOT subtract lightFilter as extra attenuation yet — keep it simple: opaque/transparent binary check plus standard -1 per horizontal/upward step. This matches Story 8.1's approach. lightFilter-based extra attenuation can be added in a follow-up.

### Heightmap Design

- `std::array<uint8_t, 256>` in ChunkColumn (16x16 grid, one byte per column position)
- Index: `z * 16 + x` (matching ChunkSection's XZ layout)
- Value: Y coordinate of highest block where `lightFilter == 15` (opaque)
- If no opaque block exists in column, value = 0 (all sky)
- Must be rebuilt after world generation. Later stories (8.3) will update incrementally on block place/break

### Cross-Chunk Propagation Order

When a new chunk loads, its sky light cannot propagate into neighbors until it is fully propagated itself. The sequence is:
1. Generate terrain for column
2. Build heightmap
3. Run block light BFS (Story 8.1)
4. Run sky light seed + BFS (this story)
5. For each loaded neighbor: propagate border sky light inward from neighbor → this column, and from this column → neighbor
6. Mark affected sections dirty for remesh

### File Structure

| Action | File | Notes |
|--------|------|-------|
| NEW | `engine/include/voxel/world/SkyLightPropagator.h` | Static methods, no state |
| NEW | `engine/src/world/SkyLightPropagator.cpp` | Implementation |
| NEW | `tests/world/TestSkyLightPropagator.cpp` | Unit tests |
| MODIFY | `engine/include/voxel/world/ChunkColumn.h` | Add heightmap array + accessors |
| MODIFY | `engine/src/world/ChunkColumn.cpp` | Implement heightmap methods |
| MODIFY | `engine/src/world/ChunkManager.cpp` | Add sky light to load pipeline |
| MODIFY | `engine/CMakeLists.txt` | Add new source files |
| MODIFY | `tests/CMakeLists.txt` | Add test file |

### Architecture Compliance

- **One class per file** — `SkyLightPropagator` in its own .h/.cpp pair
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members
- **Namespace**: `voxel::world`
- **No exceptions** — use assertions for programming errors, no runtime error paths needed (sky light always succeeds)
- **Chunks NOT in ECS** — all work through ChunkColumn/ChunkManager directly
- **Static class** — `SkyLightPropagator` has no state, all static methods (same pattern as BlockLightPropagator)
- **Max ~500 lines** per file — keep SkyLightPropagator focused

### Testing Standards

- **Framework**: Catch2 v3 with BDD-style sections
- **Test file**: `tests/world/TestSkyLightPropagator.cpp`
- **Pattern**: Create ChunkColumn, populate with known block layout, run propagation, assert exact sky light values at specific positions
- **BlockRegistry setup in tests**: Create minimal registry with air (lightFilter=0), stone (lightFilter=15), leaves (lightFilter=1) for test scenarios
- **Follow existing test patterns** from `tests/world/TestLightMap.cpp` and `tests/world/TestBlockLightPropagator.cpp` (from Story 8.1)

### Project Structure Notes

- Alignment with unified project structure: SkyLightPropagator goes in `engine/src/world/` matching the existing world module organization
- Heightmap is part of ChunkColumn (world module), not a separate class — it's a simple flat array, not worth a dedicated file
- No GPU changes needed — sky light values flow through the existing mesh → upload → shader pipeline from Story 8.0

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-08-lighting.md — Story 8.2]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 8: Lighting, BFS Propagation]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-004: Voxel Data Outside ECS]
- [Source: _bmad-output/planning-artifacts/PRD.md — FR-5.2: Sky Light]
- [Source: _bmad-output/planning-artifacts/ux-spec.md — F3 debug overlay shows sky light value]
- [Source: engine/include/voxel/world/LightMap.h — Existing light storage primitive]
- [Source: engine/include/voxel/world/ChunkColumn.h — Current column structure (no heightmap yet)]
- [Source: engine/src/world/ChunkManager.cpp:393-449 — createMeshSnapshot, pipeline integration point]
- [Source: _bmad-output/implementation-artifacts/8-0-wire-light-data-into-meshing-pipeline.md — Predecessor story]
- [Source: _bmad-output/implementation-artifacts/8-1-light-data-storage-bfs-block-light.md — Direct predecessor story]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
