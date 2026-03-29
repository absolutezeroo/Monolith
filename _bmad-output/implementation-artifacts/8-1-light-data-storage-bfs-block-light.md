# Story 8.1: Light Data Storage + BFS Block Light

Status: done

## Story

As a developer,
I want per-block light values stored and propagated via BFS,
so that torches and light-emitting blocks illuminate their surroundings.

## Acceptance Criteria

1. **LightMap storage in ChunkColumn** — Each ChunkSection has an associated LightMap. ChunkColumn owns `std::array<LightMap, SECTIONS_PER_COLUMN> m_lightMaps`. Unlike ChunkSection (lazy-allocated), LightMaps are always present (4KB × 16 = 64KB per column, acceptable).
2. **LightMap accessors on ChunkColumn** — `getLightMap(sectionY)` returns `LightMap&`/`const LightMap&`. No null checks needed since they're value-type members.
3. **BFS block light propagation** — Seed from all blocks where `BlockDefinition::lightEmission > 0`. BFS with -1 attenuation per step. Stop at 0 or when `lightFilter == 15` (opaque). Take `max(existing, new)` at each position.
4. **Cross-section propagation within column** — BFS naturally crosses Y section boundaries using world-Y coords. The propagator must handle section transitions (e.g., light at local y=15 propagates to next section's y=0).
5. **Cross-chunk propagation** — When a chunk finishes initial propagation, check border blocks. If a neighbor column exists and has lower block light at the adjacent position, enqueue the neighbor position. Mark affected neighbor sections dirty.
6. **LightMap data in mesh snapshots** — `ChunkManager::createMeshSnapshot()` copies LightMap data into `MeshJobInput`, sets `hasLightData = true` and populates `hasNeighborLight` flags.
7. **Light propagation in chunk loading pipeline** — After `WorldGenerator::generateChunkColumn()` returns, run block light propagation on the column before marking sections dirty.
8. **Unit tests** — Single torch → correct light falloff (14 at torch, 13 adjacent, ..., 0 at distance 14). Two torches → max of both values per block. Opaque blocks fully occlude. Transparent blocks (glass, lightFilter=0) pass light without attenuation beyond the standard -1. Cross-section boundary propagation works.

## Tasks / Subtasks

- [x] Task 1: Add LightMap storage to ChunkColumn (AC: 1, 2)
  - [x] 1.1 Add `#include "voxel/world/LightMap.h"` to ChunkColumn.h
  - [x] 1.2 Add `std::array<LightMap, SECTIONS_PER_COLUMN> m_lightMaps` member
  - [x] 1.3 Add `getLightMap(int sectionY)` — returns `LightMap&` and `const LightMap&`
  - [x] 1.4 Add `clearAllLight()` method to zero all 16 LightMaps

- [x] Task 2: Create BlockLightPropagator class (AC: 3, 4)
  - [x] 2.1 New header: `engine/include/voxel/world/BlockLightPropagator.h`
  - [x] 2.2 New source: `engine/src/world/BlockLightPropagator.cpp`
  - [x] 2.3 Method: `propagateColumn(ChunkColumn& column, const BlockRegistry& registry)`
  - [x] 2.4 Scan all sections for blocks with `lightEmission > 0`, seed queue
  - [x] 2.5 BFS loop: pop position, for each 6 neighbors compute new light = current - 1, if new > existing and target not opaque, set and enqueue
  - [x] 2.6 Handle Y section transitions (local y wraps 0↔15 between sections)

- [x] Task 3: Cross-chunk boundary propagation (AC: 5)
  - [x] 3.1 Method: `propagateBorders(ChunkColumn& column, ChunkManager& manager, const BlockRegistry& registry)`
  - [x] 3.2 For each border block with blockLight > 1, check if neighbor column exists
  - [x] 3.3 If neighbor's adjacent block light < this block's light - 1, seed BFS into neighbor
  - [x] 3.4 Mark affected neighbor sections dirty

- [x] Task 4: Integrate into chunk pipeline (AC: 6, 7)
  - [x] 4.1 In `ChunkManager::loadChunk()` — after `generateChunkColumn()`, call `BlockLightPropagator::propagateColumn()`
  - [x] 4.2 In `ChunkManager::loadChunk()` — after initial propagation, call `propagateBorders()` for newly loaded chunk
  - [x] 4.3 In `ChunkManager::createMeshSnapshot()` — copy LightMap data from column, set `hasLightData = true`, populate `hasNeighborLight`

- [x] Task 5: Unit tests (AC: 8)
  - [x] 5.1 New file: `tests/world/TestBlockLightPropagator.cpp`
  - [x] 5.2 Test: single torch at center of section → light = 14 at torch, 13 adjacent, falloff to 0
  - [x] 5.3 Test: two torches → max(both) at overlapping positions
  - [x] 5.4 Test: opaque block fully blocks light
  - [x] 5.5 Test: transparent block (glass, lightFilter=0) passes light with -1 attenuation only
  - [x] 5.6 Test: cross-section Y boundary propagation (torch near y=15 lights up y=0 in section above)
  - [x] 5.7 Test: ChunkColumn getLightMap() round-trip
  - [x] 5.8 Test: cross-chunk border propagation pushes light to neighbor (torch near X=15 → neighbor X=0)
  - [x] 5.9 Test: cross-chunk border propagation pulls light from neighbor (bidirectional verification)

- [x] Task 6: CMakeLists + build (AC: all)
  - [x] 6.1 Add BlockLightPropagator.cpp to `engine/CMakeLists.txt`
  - [x] 6.2 Add TestBlockLightPropagator.cpp to `tests/CMakeLists.txt`
  - [x] 6.3 Build and verify zero warnings

## Dev Notes

### LightMap Already Exists (Story 8.0)

`engine/include/voxel/world/LightMap.h` is fully implemented — header-only, 4096 bytes, packed `[sky:4 | block:4]`. API:
- `getSkyLight(x,y,z)` / `setSkyLight(x,y,z,val)` — nibble get/set
- `getBlockLight(x,y,z)` / `setBlockLight(x,y,z,val)` — nibble get/set
- `getRaw(x,y,z)` / `setRaw(x,y,z,val)` — full byte access
- `clear()` / `isClear()` / `data()`
- Indexing: Y-major `y*256 + z*16 + x` (same as ChunkSection)

DO NOT recreate or modify LightMap.h. Use it as-is.

### Meshing Pipeline Already Wired (Story 8.0)

Story 8.0 wired the complete light path: MeshJobInput has `hasLightData` flag + LightMap snapshots → MeshBuilder reads light → parallel `quadLightData` vector → GPU upload → chunk.vert unpacks per-corner light → fragment shader receives `fragSkyLight`/`fragBlockLight`.

Currently `hasLightData = false` everywhere. Your job: set it to `true` and populate the LightMaps with real BFS-propagated values.

### BFS Algorithm Detail

```
PROPAGATE_BLOCK_LIGHT(column, registry):
  queue = empty

  // Seed: scan all sections for light-emitting blocks
  for sectionY in 0..15:
    section = column.getSection(sectionY)
    if section == null: continue
    for x,y,z in 0..15:
      blockId = section.getBlock(x,y,z)
      emission = registry.getBlockType(blockId).lightEmission
      if emission > 0:
        column.getLightMap(sectionY).setBlockLight(x,y,z, emission)
        queue.push({worldX=chunkX*16+x, worldY=sectionY*16+y, worldZ=chunkZ*16+z, light=emission})

  // BFS expand
  while queue not empty:
    pos, currentLight = queue.pop()
    for each of 6 neighbors (dx,dy,dz):
      nx, ny, nz = pos + (dx,dy,dz)
      if ny < 0 or ny >= 256: continue  // World Y bounds

      // Resolve section + local coords
      nSectionY = ny / 16
      nLocalX = nx & 15  // Only valid within same chunk column
      nLocalY = ny & 15
      nLocalZ = nz & 15

      // Check if within this column (X/Z must be 0..15)
      if nLocalX != (nx - chunkX*16) or nLocalZ != (nz - chunkZ*16):
        // Cross-chunk — skip for now, handled by propagateBorders()
        continue

      block = getBlockAt(nSectionY, nLocalX, nLocalY, nLocalZ)
      def = registry.getBlockType(block)

      if def.lightFilter == 15: continue  // Fully opaque, blocks all light

      newLight = currentLight - 1
      // lightFilter > 0 means partial filtering: subtract extra attenuation
      // Actually: lightFilter semantics in this codebase: 0=transparent, 15=opaque
      // Values in between are partial filters (water=2, leaves=1)
      // Interpretation: subtract lightFilter from remaining light
      // BUT per architecture: "BFS with -1 attenuation per step, stop at 0 or opaque block"
      // Simple approach matching architecture: -1 per step, skip if lightFilter == 15

      if newLight <= 0: continue
      existing = column.getLightMap(nSectionY).getBlockLight(nLocalX, nLocalY, nLocalZ)
      if newLight > existing:
        column.getLightMap(nSectionY).setBlockLight(nLocalX, nLocalY, nLocalZ, newLight)
        queue.push({nx, ny, nz, newLight})
```

**Critical: lightFilter semantics.** The architecture says `-1 attenuation per step, stop at opaque`. For story 8.1, use this simple rule:
- `lightFilter == 15` → fully opaque, blocks ALL light (stone, dirt, etc.)
- `lightFilter < 15` → light passes through with standard -1 per step
- **Do NOT apply lightFilter as additional attenuation yet** — that complexity belongs in a future refinement. Keep it simple: opaque blocks (lightFilter == 15) block light; all others allow -1 per step.

### Cross-Chunk Border Propagation

After initial column propagation, border blocks may need to push light into neighbors:

```
PROPAGATE_BORDERS(column, chunkManager, registry):
  coord = column.getChunkCoord()

  for sectionY in 0..15:
    lightMap = column.getLightMap(sectionY)
    if lightMap.isClear(): continue  // Fast skip

    // Check X borders (x=0 → neighbor at coord-1, x=15 → neighbor at coord+1)
    // Check Z borders (z=0 → neighbor at coord-1, z=15 → neighbor at coord+1)
    for each border face:
      neighborCoord = coord + faceOffset
      neighborColumn = chunkManager.getChunk(neighborCoord)
      if neighborColumn == null: continue

      for each block on border:
        myLight = lightMap.getBlockLight(x, y, z)
        if myLight <= 1: continue

        neighborLight = neighborColumn.getLightMap(sectionY).getBlockLight(adjX, adjY, adjZ)
        if myLight - 1 > neighborLight:
          // Need to propagate into neighbor — run BFS in neighbor column
          // Seed queue with this border position, run mini-BFS
          neighborColumn.getLightMap(sectionY).setBlockLight(adjX, adjY, adjZ, myLight - 1)
          // ... BFS within neighbor from this seed
          // Mark neighbor sectionY dirty
```

**Simplification for initial implementation:** Process border propagation only for the newly loaded chunk. When a new chunk loads, check if its borders need light from already-loaded neighbors AND if its light needs to push into neighbors. Both directions matter.

### Block Properties Already Defined

From `assets/scripts/base/blocks.json` — light-emitting blocks:
| Block | lightEmission | lightFilter |
|-------|--------------|-------------|
| `base:glowstone` | 15 | 15 (opaque) |
| `base:torch` | 14 | 0 (transparent) |

Key lightFilter values:
| lightFilter | Meaning | Blocks |
|-------------|---------|--------|
| 15 | Opaque (blocks light) | stone, dirt, grass, sand, logs, ores, bedrock, etc. |
| 0 | Transparent (passes light) | glass, torch, tall_grass, flowers, dead_bush |
| 1 | Near-transparent | oak_leaves, birch_leaves, spruce_leaves, jungle_leaves |
| 2 | Slightly filtering | water |

Access via: `registry.getBlockType(stateId).lightEmission` and `registry.getBlockType(stateId).lightFilter`

### ChunkColumn Storage Design

ChunkColumn currently has:
```cpp
std::array<std::unique_ptr<ChunkSection>, SECTIONS_PER_COLUMN> m_sections;
std::array<bool, SECTIONS_PER_COLUMN> m_dirty;
```

Add LightMaps as value types (not pointers) — they're always 4KB each and start zeroed:
```cpp
std::array<LightMap, SECTIONS_PER_COLUMN> m_lightMaps;  // 64KB total
```

This is fine because ChunkColumn is already heap-allocated (`std::unique_ptr<ChunkColumn>` in ChunkManager). The 64KB is small compared to the 8KB × 16 = 128KB of section block data.

### createMeshSnapshot Integration

Current `ChunkManager::createMeshSnapshot()` copies block data. Extend to copy light data:

```cpp
// After copying block neighbors, copy light data:
input.lightData = col->getLightMap(sectionY);  // Copy center LightMap (4KB memcpy)
input.hasLightData = true;

for (int i = 0; i < 6; ++i)
{
    glm::ivec2 nCoord = coord + offsets[i].dCoord;
    int nSectionY = sectionY + offsets[i].dSection;

    const ChunkColumn* nCol = getChunk(nCoord);
    if (nCol != nullptr && nSectionY >= 0 && nSectionY < ChunkColumn::SECTIONS_PER_COLUMN)
    {
        input.neighborLightData[i] = nCol->getLightMap(nSectionY);
        input.hasNeighborLight[i] = true;
    }
    else
    {
        input.hasNeighborLight[i] = false;
    }
}
```

The `nCol` pointer is already resolved in the existing loop — reuse the same neighbor iteration. DO NOT duplicate the loop; extend the existing one.

### File Locations & Conventions

| File | Action | Namespace |
|------|--------|-----------|
| `engine/include/voxel/world/ChunkColumn.h` | MODIFY — add LightMap storage + accessors | `voxel::world` |
| `engine/src/world/ChunkColumn.cpp` | MODIFY — implement getLightMap, clearAllLight | `voxel::world` |
| `engine/include/voxel/world/BlockLightPropagator.h` | NEW — BFS propagation class | `voxel::world` |
| `engine/src/world/BlockLightPropagator.cpp` | NEW — BFS implementation | `voxel::world` |
| `engine/src/world/ChunkManager.cpp` | MODIFY — integrate light propagation + snapshot | `voxel::world` |
| `engine/include/voxel/world/ChunkManager.h` | POSSIBLY MODIFY — add `#include` if needed | `voxel::world` |
| `tests/world/TestBlockLightPropagator.cpp` | NEW — unit tests | — |
| `engine/CMakeLists.txt` | MODIFY — add new .cpp | — |
| `tests/CMakeLists.txt` | MODIFY — add test file | — |

### Naming & Style (from project-context.md)

- Classes: PascalCase (`BlockLightPropagator`)
- Methods: camelCase (`propagateColumn`, `propagateBorders`)
- Members: `m_` prefix
- Constants: SCREAMING_SNAKE
- Files: PascalCase matching class name
- `#pragma once` for all headers
- Namespace: `voxel::world`
- Error handling: `Result<T>` where needed, `VX_ASSERT` for programming errors
- No exceptions (disabled project-wide)

### Testing Pattern (from Story 8.0 + project conventions)

```cpp
#include <catch2/catch_test_macros.hpp>
#include "voxel/world/BlockLightPropagator.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkColumn.h"

using namespace voxel::world;

// Helper to register minimal block types for tests
static BlockRegistry createTestRegistry()
{
    BlockRegistry reg;
    // Air is auto-registered as ID 0
    BlockDefinition stone;
    stone.stringId = "base:stone";
    stone.isSolid = true;
    stone.lightFilter = 15;
    reg.registerBlock(std::move(stone));

    BlockDefinition torch;
    torch.stringId = "base:torch";
    torch.lightEmission = 14;
    torch.lightFilter = 0;
    torch.isSolid = false;
    reg.registerBlock(std::move(torch));

    BlockDefinition glass;
    glass.stringId = "base:glass";
    glass.lightFilter = 0;
    reg.registerBlock(std::move(glass));

    BlockDefinition glowstone;
    glowstone.stringId = "base:glowstone";
    glowstone.lightEmission = 15;
    glowstone.lightFilter = 15;
    reg.registerBlock(std::move(glowstone));

    return reg;
}

TEST_CASE("BlockLightPropagator", "[world][light]")
{
    auto registry = createTestRegistry();

    SECTION("single torch falloff") { /* ... */ }
    SECTION("two torches take max") { /* ... */ }
    SECTION("opaque block blocks light") { /* ... */ }
    SECTION("cross-section Y propagation") { /* ... */ }
}
```

### Performance Considerations

- BFS uses `std::queue<LightNode>` where `LightNode` = `{int16_t x, y, z; uint8_t light}` (8 bytes)
- Worst case: glowstone (emission=15) in open air → ~14³ = 2744 blocks visited per source
- Typical chunk: 0–5 light sources → < 15K queue operations (fast)
- `isClear()` fast path in snapshot: if LightMap is all zeros, skip copy (already optimized in LightMap)

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| LightMap | `engine/include/voxel/world/LightMap.h` | Direct — already complete |
| BlockRegistry | `engine/include/voxel/world/BlockRegistry.h` | `getBlockType(stateId).lightEmission/.lightFilter` |
| ChunkColumn | `engine/include/voxel/world/ChunkColumn.h` | Add LightMap storage |
| ChunkManager | `engine/src/world/ChunkManager.cpp` | Integration point (loadChunk, createMeshSnapshot) |
| MeshJobInput | `engine/include/voxel/renderer/MeshJobTypes.h` | Already has light fields — just populate them |
| CoordUtils | `engine/include/voxel/world/ChunkManager.h` | `floorDiv`, `euclideanMod`, `worldToChunkCoord` |

### What NOT to Do

- DO NOT modify LightMap.h — it's complete from Story 8.0
- DO NOT modify MeshBuilder or ChunkMesh — light-to-vertex pipeline already wired
- DO NOT modify shaders — already ready to consume light data
- DO NOT modify MeshJobTypes.h — already has all light snapshot fields
- DO NOT implement sky light — that's Story 8.2
- DO NOT implement dynamic light updates on block place/break — that's Story 8.3
- DO NOT implement the deferred lighting pass — that's Story 8.4
- DO NOT add lightFilter as extra attenuation beyond -1 per step — keep it simple (opaque blocks block, others pass)

### Git Intelligence (Story 8.0 Commit)

Commit `0952f4f` implemented Story 8.0. Key patterns:
- Parallel data vectors (`quadLightData` alongside `quads`) with `push_back` in lockstep
- `DEFAULT_CORNER_LIGHT = 0xF0F0F0F0` (sky=15, block=0 per corner)
- Padded 18³ workspace arrays for neighbor-aware processing
- `hasLightData` flag controls whether light averaging runs in MeshBuilder
- MeshChunkTask reconstructs pointer arrays from value-type snapshot data

### Expected Commit Message

```
feat(world): implement BFS block light propagation with ChunkColumn storage
```

### Project Structure Notes

- All paths follow `engine/include/voxel/{subsystem}/` for headers, `engine/src/{subsystem}/` for source
- Tests follow `tests/{subsystem}/Test{ClassName}.cpp` pattern
- No conflicts with existing file structure
- New files: 2 source + 1 test = 3 files total

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-08-lighting.md] Story 8.1 acceptance criteria
- [Source: _bmad-output/planning-artifacts/architecture.md] System 8 Lighting — BFS propagation rules
- [Source: _bmad-output/project-context.md] Naming conventions, error handling, testing standards
- [Source: engine/include/voxel/world/LightMap.h] Full LightMap API (Story 8.0)
- [Source: engine/include/voxel/renderer/MeshJobTypes.h] Light snapshot fields in MeshJobInput
- [Source: engine/src/world/ChunkManager.cpp] createMeshSnapshot() to extend, loadChunk() integration point
- [Source: engine/include/voxel/world/Block.h:68-69] lightEmission and lightFilter properties
- [Source: assets/scripts/base/blocks.json] Light values for all blocks (glowstone=15, torch=14)
- [Source: _bmad-output/implementation-artifacts/8-0-wire-light-data-into-meshing-pipeline.md] Story 8.0 dev notes

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- Build: zero errors, zero warnings
- Tests: 489,849 assertions in 239 test cases — all passed, zero regressions
- Light-specific tests: 175 assertions in 13 test cases (9 new + 6 existing LightMap tests, includes 2 cross-chunk border tests)

### Completion Notes List
- Added `std::array<LightMap, 16> m_lightMaps` value-type storage to ChunkColumn (64KB per column)
- Added `getLightMap(sectionY)` (const + non-const) and `clearAllLight()` accessors
- Created `BlockLightPropagator` with stateless static methods:
  - `propagateColumn()`: seeds BFS from all light-emitting blocks, expands within column with -1 attenuation per step, opaque blocks (lightFilter==15) stop propagation, cross-section Y boundaries handled naturally via world Y coords
  - `propagateBorders()`: bidirectional border propagation (push our light to neighbors, pull neighbor light into us), batched BFS per border face for efficiency, marks affected sections dirty
- Integrated into `ChunkManager::loadChunk()` — propagateColumn + propagateBorders called after world generation
- Extended `ChunkManager::createMeshSnapshot()` — copies center LightMap + 6 neighbor LightMaps, sets `hasLightData = true` and `hasNeighborLight` flags within existing neighbor iteration loop
- Added `setBlockRegistry()` to ChunkManager for BlockRegistry access during light propagation
- Wired BlockRegistry injection in `GameApp.cpp`
- 9 unit tests covering all AC8 scenarios: single torch falloff, two-torch max, opaque occlusion, transparent pass-through, cross-section Y, glowstone emission, getLightMap round-trip, cross-chunk push, cross-chunk pull
- Refactored test fixture: replaced static mutable globals with a returned TestFixture struct for safer, more idiomatic test setup

### Change Log
- 2026-03-29: Implemented Story 8.1 — BFS block light propagation with ChunkColumn storage
- 2026-03-29: Code review fixes — added cross-chunk border propagation tests (push + pull), refactored test globals to struct

### File List
- engine/include/voxel/world/ChunkColumn.h (MODIFIED — added LightMap include, m_lightMaps, getLightMap, clearAllLight)
- engine/src/world/ChunkColumn.cpp (MODIFIED — implemented getLightMap, clearAllLight)
- engine/include/voxel/world/BlockLightPropagator.h (NEW — BFS propagation class header)
- engine/src/world/BlockLightPropagator.cpp (NEW — BFS implementation with column + border propagation)
- engine/include/voxel/world/ChunkManager.h (MODIFIED — added setBlockRegistry, m_blockRegistry, BlockRegistry forward decl)
- engine/src/world/ChunkManager.cpp (MODIFIED — integrated light propagation in loadChunk, light data copy in createMeshSnapshot)
- game/src/GameApp.cpp (MODIFIED — wired setBlockRegistry call)
- tests/world/TestBlockLightPropagator.cpp (NEW — 7 unit test sections)
- engine/CMakeLists.txt (MODIFIED — added BlockLightPropagator.cpp)
- tests/CMakeLists.txt (MODIFIED — added TestBlockLightPropagator.cpp)