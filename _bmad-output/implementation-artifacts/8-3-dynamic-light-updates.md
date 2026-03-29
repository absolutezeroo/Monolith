# Story 8.3: Dynamic Light Updates

Status: ready-for-dev

## Story

As a developer,
I want lighting to update correctly when blocks are placed or broken,
so that the world stays consistently lit during gameplay.

## Acceptance Criteria

1. **Block break — light recovery**: When an opaque block is removed (solid → air), block light re-propagates from all neighboring light sources to fill the gap, and sky light re-propagates if the column's heightmap changed.
2. **Block place — light removal**: When a solid block is placed (air → solid), light at the placed position is cleared, a reverse-BFS identifies all blocks whose light depended on that position, their values are zeroed, and light is re-propagated from remaining sources.
3. **Light-emitting block placement**: If a placed block has `lightEmission > 0`, seed BFS from that position with its emission value.
4. **Light-emitting block removal**: If a broken block had `lightEmission > 0`, reverse-BFS removes its contributed light, then re-propagate from remaining sources.
5. **Section dirty flagging**: Every section whose LightMap changed during a light update is marked dirty, triggering remesh via the existing `dispatchDirtySections()` pipeline.
6. **Multi-section updates**: Light updates correctly cross section boundaries (Y axis) within the same column.
7. **Cross-chunk updates**: Light updates correctly cross chunk boundaries (X/Z axes) when neighbor chunks are loaded.
8. **Heightmap maintenance**: On block break, if the broken block was at or above the heightmap value, rebuild the heightmap for that (x,z) column. On block place of an opaque block above the current heightmap, update the heightmap value.
9. **Performance**: Single block change light update completes in < 1ms (typical case: < 0.3ms).

## Tasks / Subtasks

- [ ] Task 1: Create DynamicLightUpdater class (AC: 1–4, 6)
  - [ ] 1.1 Create `engine/include/voxel/world/DynamicLightUpdater.h`
  - [ ] 1.2 Create `engine/src/world/DynamicLightUpdater.cpp`
  - [ ] 1.3 Implement `static void onBlockBroken(ChunkColumn& column, int localX, int worldY, int localZ, const BlockDefinition& oldBlock, ChunkManager& manager, const BlockRegistry& registry)`
  - [ ] 1.4 Implement `static void onBlockPlaced(ChunkColumn& column, int localX, int worldY, int localZ, const BlockDefinition& newBlock, ChunkManager& manager, const BlockRegistry& registry)`
  - [ ] 1.5 Implement private helper `floodRemoveBlockLight(...)` — reverse-BFS that zeroes block light from an origin outward
  - [ ] 1.6 Implement private helper `floodRemoveSkyLight(...)` — reverse-BFS that zeroes sky light from an origin outward
  - [ ] 1.7 Implement private helper `reseedBlockLight(...)` — BFS re-propagation from remaining block light sources at the removal boundary
  - [ ] 1.8 Implement private helper `reseedSkyLight(...)` — BFS re-propagation from remaining sky light sources at the removal boundary

- [ ] Task 2: Implement reverse-BFS light removal algorithm (AC: 2, 4)
  - [ ] 2.1 Use two queues: `removeQueue` (positions to remove) and `reseedQueue` (boundary positions to re-propagate from)
  - [ ] 2.2 For each position popped from `removeQueue`: if its light value > 0 and came from the removed source (value < removed value), zero it and enqueue neighbors into `removeQueue`
  - [ ] 2.3 If a neighbor has light >= the current value being removed (meaning it has an independent source), add it to `reseedQueue` instead
  - [ ] 2.4 After `removeQueue` drains, BFS from `reseedQueue` to refill correct values

- [ ] Task 3: Heightmap maintenance (AC: 8)
  - [ ] 3.1 On block break: if `oldBlock.lightFilter == 15` and `worldY >= column.getHeight(localX, localZ)`, rescan that (x,z) column top-down to find new highest opaque block
  - [ ] 3.2 On block place: if `newBlock.lightFilter == 15` and `worldY > column.getHeight(localX, localZ)`, set heightmap to `worldY`
  - [ ] 3.3 When heightmap changes, trigger sky light re-propagation for affected (x,z) column

- [ ] Task 4: Section dirty marking + cross-boundary handling (AC: 5, 6, 7)
  - [ ] 4.1 Track which sections had light values modified during BFS (use `std::unordered_set<SectionKey>` or similar)
  - [ ] 4.2 After light update completes, mark all modified sections dirty via `column->markDirty(sectionY)` and neighbor columns' `markDirty()` for cross-chunk changes
  - [ ] 4.3 Handle Y section transitions: when BFS crosses y%16 boundary, switch to adjacent section's LightMap
  - [ ] 4.4 Handle X/Z chunk transitions: when BFS crosses x/z column boundary, look up neighbor via `ChunkManager::getChunk()`

- [ ] Task 5: Hook into EventBus (AC: 1–5)
  - [ ] 5.1 Add `updateLightAfterBlockChange(const glm::ivec3& worldPos, uint16_t oldBlockId, uint16_t newBlockId)` method to `ChunkManager`
  - [ ] 5.2 In `GameApp` initialization (or `ChunkManager` setup), subscribe to `EventType::BlockPlaced` and `EventType::BlockBroken`
  - [ ] 5.3 On `BlockBrokenEvent`: call `updateLightAfterBlockChange(pos, previousId, BLOCK_AIR)`
  - [ ] 5.4 On `BlockPlacedEvent`: call `updateLightAfterBlockChange(pos, BLOCK_AIR, blockId)`

- [ ] Task 6: Unit tests (AC: 1–9)
  - [ ] 6.1 Create `tests/world/TestDynamicLightUpdater.cpp`
  - [ ] 6.2 Test: break opaque wall next to torch — light fills gap (values match expected BFS falloff)
  - [ ] 6.3 Test: place opaque block in lit area — light removed behind block, correct shadow cast
  - [ ] 6.4 Test: place torch (lightEmission=14) — BFS produces correct falloff
  - [ ] 6.5 Test: break torch — all contributed light removed, area goes dark
  - [ ] 6.6 Test: two torches, break one — remaining torch keeps its area lit at correct values
  - [ ] 6.7 Test: break block at section Y boundary — light crosses into adjacent section
  - [ ] 6.8 Test: sky light recovery — break opaque roof block, sky light (15) floods down
  - [ ] 6.9 Test: place opaque block above surface — heightmap updated, sky light blocked below
  - [ ] 6.10 Performance benchmark: single torch place/break < 1ms (use Catch2 BENCHMARK)

- [ ] Task 7: CMakeLists + build (AC: all)
  - [ ] 7.1 Add `DynamicLightUpdater.cpp` to `engine/CMakeLists.txt`
  - [ ] 7.2 Add `TestDynamicLightUpdater.cpp` to `tests/CMakeLists.txt`
  - [ ] 7.3 Build and verify zero warnings

## Dev Notes

### Stories 8.1 and 8.2 Must Be Complete First

This story depends on:
- **Story 8.1** — `BlockLightPropagator`, LightMap storage in ChunkColumn (`m_lightMaps`), mesh snapshot light population
- **Story 8.2** — `SkyLightPropagator`, heightmap in ChunkColumn (`m_heightMap`), two-phase sky light algorithm

Both provide the foundational propagation algorithms this story reuses. DO NOT reimplement BFS propagation from scratch — call into `BlockLightPropagator` and `SkyLightPropagator` methods where possible, or extract their BFS logic into shared helpers if needed.

### Reverse-BFS Algorithm (The Core Challenge)

The trickiest part of this story is **light removal**. When a block is placed that blocks light, you cannot simply zero affected blocks — you must identify exactly which blocks' light values depended on light passing through the now-blocked position, then re-propagate from remaining independent sources.

**Two-queue algorithm (recommended approach):**

```
FLOOD_REMOVE_LIGHT(column, startX, startY, startZ, startLightValue, lightType):
  removeQueue = {(startX, startY, startZ, startLightValue)}
  reseedQueue = empty

  while removeQueue not empty:
    (x, y, z, oldLight) = removeQueue.pop()

    for each of 6 neighbors (nx, ny, nz):
      if out of bounds: continue
      neighborLight = getLight(nx, ny, nz, lightType)
      if neighborLight == 0: continue

      if neighborLight < oldLight:
        // This neighbor's light depended on the removed path
        setLight(nx, ny, nz, 0, lightType)
        removeQueue.push((nx, ny, nz, neighborLight))
        markSectionDirty(ny / 16)
      else:
        // This neighbor has an independent source — reseed from here
        reseedQueue.push((nx, ny, nz, neighborLight))

  // Phase 2: Re-propagate from boundary sources
  while reseedQueue not empty:
    (x, y, z, light) = reseedQueue.pop()
    for each of 6 neighbors (nx, ny, nz):
      if out of bounds: continue
      block = getBlock(nx, ny, nz)
      if block.lightFilter == 15: continue  // Opaque
      newLight = light - 1
      if newLight > getLight(nx, ny, nz, lightType):
        setLight(nx, ny, nz, newLight, lightType)
        reseedQueue.push((nx, ny, nz, newLight))
        markSectionDirty(ny / 16)
```

**Sky light special case for downward propagation:** During reseed phase, if direction is downward, `newLight = light` (no attenuation), matching Story 8.2's rule.

### Block Break Flow

```
ON_BLOCK_BROKEN(column, localX, worldY, localZ, oldBlock, manager, registry):
  sectionY = worldY / 16
  localY = worldY % 16

  // 1. Handle sky light if opaque block was removed
  if oldBlock.lightFilter == 15:
    oldHeight = column.getHeight(localX, localZ)
    if worldY >= oldHeight:
      // Rescan heightmap for this (x,z) column
      column.rebuildHeightAt(localX, localZ, registry)
      // Seed sky light from surface downward for this (x,z)
      seedSkyLightColumn(column, localX, localZ, registry)

  // 2. Handle block light
  // The removed block may have been blocking light — check 6 neighbors
  // Re-propagate block light from neighbor sources into the gap
  for each 6 neighbors:
    neighborBlockLight = getBlockLight(neighbor)
    if neighborBlockLight > 1:
      reseedQueue.push(neighbor, neighborBlockLight)
  BFS from reseedQueue into the now-air gap position

  // 3. If removed block emitted light, reverse-BFS remove it
  if oldBlock.lightEmission > 0:
    floodRemoveBlockLight(column, localX, worldY, localZ, oldBlock.lightEmission)
    // reseedQueue collects boundary sources, re-propagates automatically

  // 4. Mark affected sections dirty
```

### Block Place Flow

```
ON_BLOCK_PLACED(column, localX, worldY, localZ, newBlock, manager, registry):
  sectionY = worldY / 16
  localY = worldY % 16

  // 1. If new block emits light, seed BFS from it FIRST
  if newBlock.lightEmission > 0:
    column.getLightMap(sectionY).setBlockLight(localX, localY, localZ, newBlock.lightEmission)
    // BFS expand from this position
    seedBlockLightFrom(column, localX, worldY, localZ, newBlock.lightEmission, manager, registry)

  // 2. If new block is opaque, remove light passing through this position
  if newBlock.lightFilter == 15:
    existingBlockLight = column.getLightMap(sectionY).getBlockLight(localX, localY, localZ)
    existingSkyLight = column.getLightMap(sectionY).getSkyLight(localX, localY, localZ)

    if existingBlockLight > 0:
      column.getLightMap(sectionY).setBlockLight(localX, localY, localZ, 0)
      floodRemoveBlockLight(column, localX, worldY, localZ, existingBlockLight)

    if existingSkyLight > 0:
      column.getLightMap(sectionY).setSkyLight(localX, localY, localZ, 0)
      floodRemoveSkyLight(column, localX, worldY, localZ, existingSkyLight)

    // 3. Update heightmap
    if worldY > column.getHeight(localX, localZ):
      column.setHeight(localX, localZ, static_cast<uint8_t>(worldY))

  // 4. Mark affected sections dirty
```

### EventBus Integration Point

Events are published in `GameApp::handleBlockInteraction()` (in `game/src/GameApp.cpp`, lines 303 and 336), AFTER `ChunkManager::setBlock()` has already written the block. Subscribe in GameApp initialization:

```cpp
// In GameApp constructor or init():
m_eventBus.subscribe<voxel::game::EventType::BlockBroken>(
    [this](const voxel::game::BlockBrokenEvent& e) {
        glm::ivec3 pos{e.position.x, e.position.y, e.position.z};
        const auto& oldDef = m_blockRegistry.getBlockType(e.previousBlockId);
        m_chunkManager.updateLightAfterBlockChange(pos, &oldDef, nullptr);
    });

m_eventBus.subscribe<voxel::game::EventType::BlockPlaced>(
    [this](const voxel::game::BlockPlacedEvent& e) {
        glm::ivec3 pos{e.position.x, e.position.y, e.position.z};
        const auto& newDef = m_blockRegistry.getBlockType(e.blockId);
        m_chunkManager.updateLightAfterBlockChange(pos, nullptr, &newDef);
    });
```

The `updateLightAfterBlockChange` method on `ChunkManager` then delegates to `DynamicLightUpdater`:

```cpp
void ChunkManager::updateLightAfterBlockChange(
    const glm::ivec3& worldPos,
    const BlockDefinition* oldBlock,
    const BlockDefinition* newBlock)
{
    glm::ivec2 chunkCoord = worldToChunkCoord(worldPos);
    ChunkColumn* column = getChunk(chunkCoord);
    if (!column) return;

    glm::ivec3 local = worldToLocalPos(worldPos);

    if (oldBlock) {
        DynamicLightUpdater::onBlockBroken(*column, local.x, worldPos.y, local.z,
                                            *oldBlock, *this, m_blockRegistry);
    }
    if (newBlock) {
        DynamicLightUpdater::onBlockPlaced(*column, local.x, worldPos.y, local.z,
                                           *newBlock, *this, m_blockRegistry);
    }
}
```

### Existing Dirty Marking in setBlock

`ChunkManager::setBlock()` already marks the block's section and adjacent boundary sections dirty for remeshing. Light updates must mark ADDITIONAL sections dirty — any section where a LightMap value changed. The light BFS may reach sections not adjacent to the original block (up to 14 blocks away for torchlight). Track modified sections during BFS and mark them all dirty after completion.

### Cross-Chunk Light Access Pattern

For BFS that crosses chunk boundaries, use `ChunkManager::getChunk()` to resolve the neighbor column:

```cpp
// When BFS reaches x < 0 or x >= 16 (local coords):
glm::ivec2 neighborCoord = chunkCoord + glm::ivec2{dx, dz};  // dx/dz = -1 or +1
ChunkColumn* neighborCol = manager.getChunk(neighborCoord);
if (!neighborCol) return;  // Neighbor not loaded — stop BFS here
// Continue BFS in neighbor column with adjusted local coords
```

If a neighbor chunk is not loaded, stop BFS at that boundary. When the neighbor loads later, Stories 8.1/8.2 border propagation handles initial light setup.

### Performance Budget

- **Worst case**: Torch (emission=14) placed in open air → BFS visits ~14³ ≈ 2,744 blocks
- **Reverse-BFS**: Torch broken → remove phase visits same ~2,744 blocks + reseed phase visits boundary
- **Total**: ~5,500 operations for worst-case torch break = ~0.3ms on modern CPU
- **Typical**: Most block changes in partially occluded areas affect < 500 blocks = < 0.1ms
- **Budget**: < 1ms leaves margin for cross-chunk lookups and section dirty marking

Optimization techniques if needed:
- `LightMap::isClear()` fast path to skip processing in empty sections
- `std::vector<LightNode>` with index tracking instead of `std::queue` (better cache locality)
- Early termination when light reaches 0
- Visited bit tracking via the LightMap values themselves (zeroed = visited for removal)

### LightNode Struct

Reuse the same struct from Stories 8.1/8.2 or define locally:

```cpp
struct LightNode
{
    int16_t x, y, z;   // World coordinates
    uint8_t level;      // Light value at this position
};
// 8 bytes, cache-friendly for queue operations
```

If 8.1/8.2 define this in a shared header, use it. Otherwise define in `DynamicLightUpdater.h`.

### What NOT to Do

- DO NOT modify `LightMap.h` — complete from Story 8.0
- DO NOT modify shaders — they already consume light data
- DO NOT modify `MeshBuilder`, `ChunkMesh`, or `MeshJobTypes.h` — light pipeline already wired
- DO NOT re-propagate the entire chunk/column — use targeted reverse-BFS + reseed for the affected region only
- DO NOT apply `lightFilter` as extra attenuation beyond -1 per step — keep simple: opaque (lightFilter==15) blocks light, others pass with -1 attenuation
- DO NOT run light updates asynchronously — they must execute synchronously on the main thread for immediate visual feedback (< 1ms budget)
- DO NOT add new event types — `BlockPlaced`/`BlockBroken` events already exist

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| NEW | `engine/include/voxel/world/DynamicLightUpdater.h` | `voxel::world` | Static methods, no state |
| NEW | `engine/src/world/DynamicLightUpdater.cpp` | `voxel::world` | BFS implementation |
| NEW | `tests/world/TestDynamicLightUpdater.cpp` | — | Unit tests + benchmark |
| MODIFY | `engine/include/voxel/world/ChunkManager.h` | `voxel::world` | Add `updateLightAfterBlockChange()` |
| MODIFY | `engine/src/world/ChunkManager.cpp` | `voxel::world` | Implement `updateLightAfterBlockChange()` |
| MODIFY | `game/src/GameApp.cpp` | — | Subscribe to block events for light updates |
| MODIFY | `engine/CMakeLists.txt` | — | Add DynamicLightUpdater.cpp |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestDynamicLightUpdater.cpp |

### Naming & Style

- Classes: PascalCase (`DynamicLightUpdater`)
- Methods: camelCase (`onBlockBroken`, `floodRemoveBlockLight`, `reseedBlockLight`)
- Members: `m_` prefix (none needed — static class)
- Constants: SCREAMING_SNAKE (`MAX_LIGHT_LEVEL = 15`)
- `#pragma once` for headers
- Namespace: `voxel::world`
- No exceptions — use assertions for programming errors
- Max ~500 lines per file — split helpers if needed

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "voxel/world/DynamicLightUpdater.h"
#include "voxel/world/BlockLightPropagator.h"
#include "voxel/world/SkyLightPropagator.h"
#include "voxel/world/ChunkColumn.h"
#include "voxel/world/BlockRegistry.h"

using namespace voxel::world;

static BlockRegistry createTestRegistry()
{
    BlockRegistry reg;
    // Air = ID 0 (auto-registered, lightFilter=0)
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

    BlockDefinition glowstone;
    glowstone.stringId = "base:glowstone";
    glowstone.lightEmission = 15;
    glowstone.lightFilter = 15;
    reg.registerBlock(std::move(glowstone));

    return reg;
}

TEST_CASE("DynamicLightUpdater", "[world][light]")
{
    auto registry = createTestRegistry();

    SECTION("break wall next to torch — light fills gap") { /* ... */ }
    SECTION("place block in lit area — shadow cast") { /* ... */ }
    SECTION("place torch — correct BFS falloff") { /* ... */ }
    SECTION("break torch — area goes dark") { /* ... */ }
    SECTION("two torches, break one — other stays lit") { /* ... */ }
    SECTION("Y boundary crossing") { /* ... */ }
    SECTION("sky light recovery on roof break") { /* ... */ }
}

TEST_CASE("DynamicLightUpdater performance", "[world][light][!benchmark]")
{
    auto registry = createTestRegistry();
    // Setup column with torch
    BENCHMARK("single torch place") { /* ... */ };
    BENCHMARK("single torch break") { /* ... */ };
}
```

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `LightMap` | `engine/include/voxel/world/LightMap.h` | Direct — getBlockLight/setBlockLight/getSkyLight/setSkyLight |
| `BlockLightPropagator` | From Story 8.1 | BFS algorithm reference; possibly call `propagateColumn()` for full-column re-propagation fallback |
| `SkyLightPropagator` | From Story 8.2 | Sky light algorithm reference; heightmap rebuild via `buildHeightMap()` or targeted `rebuildHeightAt()` |
| `ChunkColumn` | `engine/include/voxel/world/ChunkColumn.h` | LightMap access (`getLightMap()`), heightmap access, dirty marking |
| `ChunkManager` | `engine/src/world/ChunkManager.cpp` | `getChunk()` for cross-chunk access, `worldToChunkCoord()`, `worldToLocalPos()` |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | `getBlockType(id).lightEmission/.lightFilter` |
| `EventBus` | `engine/include/voxel/game/EventBus.h` | Subscribe to `BlockPlaced`/`BlockBroken` events |
| Dirty tracking | `ChunkColumn::markDirty(sectionY)` | Mark sections for remeshing after light change |

### Git Intelligence

Recent commits show the lighting pipeline evolution:
- `23dd811` — Story 8.0: light averaging tests, GPU upload handling for parallel light buffer
- `0952f4f` — Story 8.0: LightMap creation, meshing pipeline integration
- `dff6d19` — Story 7.5: block placement and breaking systems with mining mechanics

The block place/break system (7.5) publishes `BlockPlacedEvent`/`BlockBrokenEvent` via `EventBus`. These events carry `position` and `blockId`/`previousBlockId` — exactly what `DynamicLightUpdater` needs.

### Project Structure Notes

- All files follow `engine/include/voxel/{subsystem}/` for headers, `engine/src/{subsystem}/` for source
- Tests follow `tests/{subsystem}/Test{ClassName}.cpp` pattern
- `DynamicLightUpdater` belongs in the `world` subsystem alongside `BlockLightPropagator` and `SkyLightPropagator`
- No conflicts with existing file structure
- New files: 2 source (header + cpp) + 1 test = 3 new files
- Modified files: 4 (ChunkManager.h, ChunkManager.cpp, GameApp.cpp, both CMakeLists.txt)

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-08-lighting.md — Story 8.3 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 8: Lighting, BFS propagation rules, reverse-BFS on place/break]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards, threading rules]
- [Source: engine/include/voxel/world/LightMap.h — Light data storage API]
- [Source: engine/include/voxel/game/EventBus.h — BlockPlacedEvent, BlockBrokenEvent, subscribe/publish API]
- [Source: engine/include/voxel/game/GameCommand.h — PlaceBlockPayload, BreakBlockPayload]
- [Source: game/src/GameApp.cpp:250-343 — handleBlockInteraction, event publication after setBlock]
- [Source: engine/src/world/ChunkManager.cpp:43-107 — setBlock with boundary dirty marking]
- [Source: engine/src/world/ChunkManager.cpp:292-391 — dispatchDirtySections, mesh dispatch pipeline]
- [Source: _bmad-output/implementation-artifacts/8-0-wire-light-data-into-meshing-pipeline.md — Parallel light buffer, shader plumbing]
- [Source: _bmad-output/implementation-artifacts/8-1-light-data-storage-bfs-block-light.md — BlockLightPropagator BFS, LightMap in ChunkColumn]
- [Source: _bmad-output/implementation-artifacts/8-2-sky-light-propagation.md — SkyLightPropagator, heightmap, two-phase sky algorithm]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
