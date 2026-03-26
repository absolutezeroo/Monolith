# Story 4.5: Tree and Decoration Placement

Status: ready-for-dev

## Story

As a developer,
I want trees and surface decorations placed per-biome,
so that the world looks alive and biomes are visually distinct.

## Acceptance Criteria

1. **AC-1** — `StructureGenerator` class in `voxel::world` places multi-block structures and single-block decorations
   during terrain generation. Constructor: `explicit StructureGenerator(uint64_t seed, const BlockRegistry& registry)`.
2. **AC-2** — Five tree types with biome associations: oak (Forest, Plains), birch (Forest), spruce (Taiga), cactus (
   Desert), jungle tree (Jungle). Each tree is a hardcoded schematic (trunk + leaves/body positioned relative to root).
3. **AC-3** — Tree placement uses a per-column seeded RNG: `seed + hash(worldX, worldZ)`. Density varies per biome (
   e.g., Forest=high, Plains=low, Desert=cactus-only). Minimum spacing of 4 blocks between tree roots enforced via a
   spacing grid check.
4. **AC-4** — Cross-chunk awareness via neighbor-overlap: when generating chunk (cx,cz), tree positions are computed for
   a 3x3 grid of chunk coordinates (cx-1..cx+1, cz-1..cz+1). Only blocks falling within (cx,cz) are placed. No
   cross-chunk writes, fully deterministic.
5. **AC-5** — Surface decorations placed per-biome: tall grass (Plains, Forest, Savanna), flowers (Forest, Plains), dead
   bushes (Desert), snow layers (Tundra, IcePlains). Decoration density per biome.
6. **AC-6** — Ore veins placed in stone: coal (y=5–128, vein 4–12), iron (y=5–64, vein 3–8), gold (y=5–32, vein 3–6),
   diamond (y=5–16, vein 2–4). Each ore type uses a seeded RNG per-chunk with a unique offset.
7. **AC-7** — All new block types registered in `blocks.json`: birch_log, birch_leaves, spruce_log, spruce_leaves,
   jungle_log, jungle_leaves, cactus, tall_grass, flower_red, flower_yellow, dead_bush, snow_layer, coal_ore, iron_ore,
   gold_ore, diamond_ore.
8. **AC-8** — Same `(seed, chunkCoord)` produces byte-identical output. Unit tests for determinism, tree placement
   within biome bounds, ore depth ranges, decoration types per biome, cross-chunk tree overlap correctness.

## Tasks / Subtasks

- [ ] Task 1 — Register new block types (AC: #7)
    - [ ] Add 16 new entries to `assets/scripts/base/blocks.json` using `base:` namespace prefix
    - [ ] Verify all blocks load correctly via `BlockRegistry::loadFromJson()`
    - [ ] Block properties: logs are solid/opaque, leaves are cutout/transparent with waving,
      tall_grass/flowers/dead_bush are Cross model/cutout/no collision, snow_layer is slab-like, cactus is solid with
      damage, ores are solid/opaque with appropriate hardness
- [ ] Task 2 — Create `StructureGenerator` class (AC: #1, #3, #4)
    - [ ] `StructureGenerator.h` / `StructureGenerator.cpp` in `voxel::world`
    - [ ] Constructor caches block IDs from `BlockRegistry::getIdByName()` for all tree/decoration/ore block types;
      fallback to `BLOCK_AIR` with `VX_LOG_WARN` if not registered
    - [ ] Seeded RNG: use `std::mt19937` seeded per-chunk with `seed + hash(chunkCoord.x, chunkCoord.y)`. Use a
      deterministic hash (e.g., `chunkCoord.x * 341873128712 + chunkCoord.y * 132897987541 + seedOffset`)
    - [ ] Method:
      `void populate(ChunkColumn& column, glm::ivec2 chunkCoord, const BiomeSystem& biomeSystem, int surfaceHeights[16][16]) const`
- [ ] Task 3 — Implement tree schematics (AC: #2)
    - [ ] 
      `struct TreeSchematic { std::vector<glm::ivec3> trunkOffsets; std::vector<glm::ivec3> leafOffsets; uint16_t trunkBlock; uint16_t leafBlock; }` —
      positions relative to root (0,0,0)
    - [ ] Oak tree: trunk 4–6 tall, 5x5x3 leaf canopy centered on top of trunk (with corners removed)
    - [ ] Birch tree: trunk 5–7 tall, 3x3x3 leaf canopy (narrower than oak)
    - [ ] Spruce tree: trunk 6–8 tall, cone-shaped leaves — widest at bottom (5x5), narrowing to 1x1 at top
    - [ ] Jungle tree: trunk 8–12 tall, 7x7x4 large leaf canopy
    - [ ] Cactus: 1–3 blocks tall, no leaves, single column (`base:cactus`)
    - [ ] Height varies per tree using the per-column RNG (range min–max per type)
- [ ] Task 4 — Tree placement algorithm (AC: #3, #4)
    - [ ] For each chunk (cx,cz), iterate over 3x3 grid of chunk coords (nx,nz) in [cx-1..cx+1, cz-1..cz+1]
    - [ ] For each neighbor chunk, seed RNG with `seed + hash(nx, nz) + TREE_SEED_OFFSET` (use `seed + 6` for tree base
      offset, continuing the cascade)
    - [ ] Per-column chance: iterate local (x,z) in [0,15], roll RNG against biome tree density
    - [ ] Biome tree density: Desert=0.001 (cactus only), Plains=0.005, Savanna=0.003, Forest=0.04, Jungle=0.06,
      Taiga=0.03, Tundra=0.0, IcePlains=0.0
    - [ ] Spacing check: maintain `bool occupied[48][48]` grid (3 chunks wide) tracking root positions with 4-block
      exclusion radius. Skip tree if any occupied cell within radius
    - [ ] Surface validation: tree root must be on surface block at surfaceHeight, not on air or water. For cross-chunk
      positions, compute surface height via the same noise pipeline (continent+spline+biome+detail)
    - [ ] Generate tree schematic blocks. For each block, convert to local coords in target chunk (cx,cz). If
      within [0,15]x[0,255]x[0,15], place it. Leaves only overwrite air. Trunks overwrite leaves and air.
- [ ] Task 5 — Surface decoration placement (AC: #5)
    - [ ] After trees, iterate all 256 columns in the target chunk
    - [ ] For each column, get biome at that position via `BiomeSystem::getBiomeAt()`
    - [ ] Roll per-column RNG against biome decoration density
    - [ ] Biome decoration rules:
        - Plains: 30% tall_grass, 3% flower_red, 2% flower_yellow
        - Forest: 40% tall_grass, 5% flower_red, 3% flower_yellow
        - Savanna: 15% tall_grass, 1% dead_bush
        - Desert: 5% dead_bush, 2% cactus (single block, separate from tree cactus)
        - Jungle: 50% tall_grass, 2% flower_red
        - Taiga: 10% tall_grass
        - Tundra: 80% snow_layer
        - IcePlains: 90% snow_layer
    - [ ] Decoration placed at `surfaceHeight + 1`. Skip if that Y is not air.
    - [ ] Only place on appropriate surface (no tall_grass on sand, no dead_bush on snow)
- [ ] Task 6 — Ore vein generation (AC: #6)
    - [ ] Per-chunk ore placement, seeded with `seed + hash(chunkCoord) + ORE_SEED_OFFSET` (use `seed + 7` for ore base)
    - [ ] For each ore type, determine number of veins per chunk via RNG:
        - Coal: 20 veins/chunk, y=[5,128], vein size 4–12 blocks
        - Iron: 12 veins/chunk, y=[5,64], vein size 3–8 blocks
        - Gold: 4 veins/chunk, y=[5,32], vein size 3–6 blocks
        - Diamond: 1 vein/chunk, y=[5,16], vein size 2–4 blocks
    - [ ] Vein algorithm: pick random start position (x in [0,15], z in [0,15], y in range). BFS-walk from start: for
      each block in vein, pick random adjacent position, place ore if current block is `base:stone`. Continue until vein
      size reached or no more stone neighbors.
    - [ ] Ores placed BEFORE cave carving (so caves can cut through ore veins naturally)
- [ ] Task 7 — Integrate into WorldGenerator (AC: #1)
    - [ ] Add `StructureGenerator m_structureGen` member to `WorldGenerator`
    - [ ] In `generateChunkColumn()`, call order: terrain fill → ore veins → cave carving → tree placement → surface
      decorations
    - [ ] Pass `surfaceHeights[16][16]` (already collected for CaveCarver) to `StructureGenerator::populate()`
    - [ ] `StructureGenerator::populate()` internally calls ore generation, then tree placement, then decoration
      placement
- [ ] Task 8 — Unit tests (AC: #8)
    - [ ] `tests/world/TestStructureGenerator.cpp` tagged `[world][structure]`
    - [ ] Test determinism: generate same chunk twice, compare block-by-block
    - [ ] Test tree placement only in appropriate biomes (no oak in Desert, no cactus in Forest)
    - [ ] Test ore depth ranges (no diamond above y=16, no coal above y=128)
    - [ ] Test cross-chunk overlap: generate two adjacent chunks, verify tree at border has leaves in both
    - [ ] Test decoration types per biome (snow_layer only in Tundra/IcePlains)
    - [ ] Test spacing enforcement: no two tree roots within 4 blocks
    - [ ] Update `TestWorldGenerator.cpp` with integration test: full pipeline produces deterministic output
- [ ] Task 9 — CMake wiring
    - [ ] Add `src/world/StructureGenerator.cpp` to `engine/CMakeLists.txt`
    - [ ] Add `tests/world/TestStructureGenerator.cpp` to `tests/CMakeLists.txt`

## Dev Notes

### Seed Cascade (continuing from prior stories)

| Offset       | Noise/RNG                   | Story   |
|--------------|-----------------------------|---------|
| seed + 0     | Continent noise             | 4.1/4.2 |
| seed + 1     | Detail noise                | 4.2     |
| seed + 2     | Temperature noise           | 4.3     |
| seed + 3     | Humidity noise              | 4.3     |
| seed + 4     | Cheese cave noise           | 4.4     |
| seed + 5     | Spaghetti cave noise        | 4.4     |
| **seed + 6** | **Tree placement RNG base** | **4.5** |
| **seed + 7** | **Ore vein RNG base**       | **4.5** |

Per-chunk RNG seeding:
`std::mt19937 rng(static_cast<uint32_t>(seed + offset + chunkCoord.x * 341873128712LL + chunkCoord.y * 132897987541LL))`.
Use deterministic integer hash — NOT `std::hash` (implementation-defined).

### Block Namespace Convention

The project uses `base:` namespace prefix (NOT `base:`). See existing blocks in `assets/scripts/base/blocks.json`. All
new blocks must follow: `base:birch_log`, `base:coal_ore`, etc.

### Generation Order in WorldGenerator::generateChunkColumn()

```
1. Terrain fill (bedrock, stone, biome surface) — Stories 4.1–4.3
2. Ore vein placement — Story 4.5 (ores inside stone, before carving)
3. Cave carving — Story 4.4 (carves through ore veins naturally)
4. Tree placement (3x3 neighbor overlap scan) — Story 4.5
5. Surface decorations — Story 4.5 (placed last, on final surface)
```

Ore veins go BEFORE cave carving so caves cut through them. Trees and decorations go AFTER caves so they don't get
carved away.

### Cross-Chunk Tree Algorithm

```
populate(targetChunk, targetCoord):
  occupied[48][48] = false  // 3-chunk-wide spacing grid

  for nx in [targetCoord.x - 1 .. targetCoord.x + 1]:
    for nz in [targetCoord.y - 1 .. targetCoord.y + 1]:
      rng = seeded(seed + 6 + hash(nx, nz))
      for lx in [0..15], lz in [0..15]:
        worldX = nx * 16 + lx
        worldZ = nz * 16 + lz
        biome = biomeSystem.getBiomeAt(worldX, worldZ)
        if rng.nextFloat() > biome.treeDensity: continue
        gridX = (nx - targetCoord.x + 1) * 16 + lx
        gridZ = (nz - targetCoord.y + 1) * 16 + lz
        if any occupied[gridX±4][gridZ±4]: continue
        occupied[gridX][gridZ] = true
        surfaceH = computeSurfaceHeight(worldX, worldZ)  // noise pipeline
        tree = selectTree(biome, rng)
        for each block in tree.schematic(surfaceH):
          localX = block.worldX - targetCoord.x * 16
          localZ = block.worldZ - targetCoord.y * 16
          if localX in [0,15] and localZ in [0,15] and block.y in [0,255]:
            if isLeaf and targetChunk.getBlock(localX, block.y, localZ) != BLOCK_AIR: continue
            targetChunk.setBlock(localX, block.y, localZ, block.id)
```

**Critical**: For trees in neighboring chunks, you need the surface height at those positions. Recompute via the noise
pipeline (continent noise + spline + biome height modifier + detail noise) — NOT by reading neighbor ChunkColumn data (
neighbors may not be loaded yet). This is the same math WorldGenerator already uses, so expose a
`int computeSurfaceHeight(float worldX, float worldZ)` helper in WorldGenerator.

### Surface Height Computation for Cross-Chunk Trees

Add a helper to WorldGenerator:

```cpp
int WorldGenerator::computeSurfaceHeight(float worldX, float worldZ) const
{
    float continent = m_continentNoise.GetNoise(worldX, worldZ);
    float baseHeight = m_spline.evaluate(continent);
    BlendedBiome blended = m_biomeSystem.getBlendedBiomeAt(worldX, worldZ);
    float detail = m_detailNoise.GetNoise(worldX, worldZ);
    float finalHeight = baseHeight + blended.blendedHeightModifier
                      + detail * DETAIL_AMPLITUDE * blended.blendedHeightScale;
    return static_cast<int>(std::clamp(finalHeight, 1.0f, 254.0f));
}
```

### Tree Schematic Definitions

**Oak** (Plains, Forest):

- Trunk: `base:oak_log`, height 4–6 (random)
- Leaves: `base:oak_leaves`, 5x5x3 canopy starting 1 below trunk top, corners removed
- Total bounds: 5 wide x 5 deep x (height+3) tall

**Birch** (Forest):

- Trunk: `base:birch_log`, height 5–7
- Leaves: `base:birch_leaves`, 3x3x3 canopy at trunk top
- Narrower silhouette than oak

**Spruce** (Taiga):

- Trunk: `base:spruce_log`, height 6–8
- Leaves: `base:spruce_leaves`, cone shape — 1x1 at top, 3x3 middle layers, 5x5 bottom layer
- Characteristic pointed shape

**Jungle** (Jungle):

- Trunk: `base:jungle_log`, height 8–12
- Leaves: `base:jungle_leaves`, 7x7x4 large canopy, corners removed
- Tallest tree type

**Cactus** (Desert):

- Body: `base:cactus`, height 1–3 (random), single column
- No leaves, no branching
- `damagePerSecond` set in block definition (contact damage)
- Only place on `base:sand` surface

### New Block Definitions (add to blocks.json)

```json
{"stringId": "base:birch_log", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 2.0, "textureIndices": [14, 14, 13, 13, 14, 14], "dropItem": "base:birch_log", "groups": {"choppy": 2, "wood": 1}},
{"stringId": "base:birch_leaves", "isSolid": true, "isTransparent": true, "hasCollision": true, "hardness": 0.2, "textureIndices": [15, 15, 15, 15, 15, 15], "dropItem": "", "renderType": "cutout", "waving": 1, "isFloodable": true, "groups": {"choppy": 3, "leafdecay": 3}},
{"stringId": "base:spruce_log", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 2.0, "textureIndices": [17, 17, 16, 16, 17, 17], "dropItem": "base:spruce_log", "groups": {"choppy": 2, "wood": 1}},
{"stringId": "base:spruce_leaves", "isSolid": true, "isTransparent": true, "hasCollision": true, "hardness": 0.2, "textureIndices": [18, 18, 18, 18, 18, 18], "dropItem": "", "renderType": "cutout", "waving": 1, "isFloodable": true, "groups": {"choppy": 3, "leafdecay": 3}},
{"stringId": "base:jungle_log", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 2.0, "textureIndices": [20, 20, 19, 19, 20, 20], "dropItem": "base:jungle_log", "groups": {"choppy": 2, "wood": 1}},
{"stringId": "base:jungle_leaves", "isSolid": true, "isTransparent": true, "hasCollision": true, "hardness": 0.2, "textureIndices": [21, 21, 21, 21, 21, 21], "dropItem": "", "renderType": "cutout", "waving": 1, "isFloodable": true, "groups": {"choppy": 3, "leafdecay": 3}},
{"stringId": "base:cactus", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 0.4, "textureIndices": [23, 23, 22, 22, 23, 23], "dropItem": "base:cactus", "damagePerSecond": 1, "groups": {"choppy": 3}},
{"stringId": "base:tall_grass", "isSolid": false, "isTransparent": true, "hasCollision": false, "hardness": 0.0, "textureIndices": [24, 24, 24, 24, 24, 24], "dropItem": "", "renderType": "cutout", "modelType": "cross", "waving": 1, "isFloodable": true, "isBuildableTo": true, "isReplaceable": true, "groups": {"dig_immediate": 3, "flora": 1}},
{"stringId": "base:flower_red", "isSolid": false, "isTransparent": true, "hasCollision": false, "hardness": 0.0, "textureIndices": [25, 25, 25, 25, 25, 25], "dropItem": "base:flower_red", "renderType": "cutout", "modelType": "cross", "waving": 1, "isFloodable": true, "groups": {"dig_immediate": 3, "flora": 1}},
{"stringId": "base:flower_yellow", "isSolid": false, "isTransparent": true, "hasCollision": false, "hardness": 0.0, "textureIndices": [26, 26, 26, 26, 26, 26], "dropItem": "base:flower_yellow", "renderType": "cutout", "modelType": "cross", "waving": 1, "isFloodable": true, "groups": {"dig_immediate": 3, "flora": 1}},
{"stringId": "base:dead_bush", "isSolid": false, "isTransparent": true, "hasCollision": false, "hardness": 0.0, "textureIndices": [27, 27, 27, 27, 27, 27], "dropItem": "", "renderType": "cutout", "modelType": "cross", "isFloodable": true, "isBuildableTo": true, "isReplaceable": true, "groups": {"dig_immediate": 3}},
{"stringId": "base:snow_layer", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 0.1, "textureIndices": [28, 28, 28, 28, 28, 28], "dropItem": "", "modelType": "slab", "isFloodable": true, "isBuildableTo": true, "isReplaceable": true, "groups": {"crumbly": 3}},
{"stringId": "base:coal_ore", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 3.0, "textureIndices": [29, 29, 29, 29, 29, 29], "dropItem": "base:coal_ore", "groups": {"cracky": 2, "stone": 1}},
{"stringId": "base:iron_ore", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 3.0, "textureIndices": [30, 30, 30, 30, 30, 30], "dropItem": "base:iron_ore", "groups": {"cracky": 2, "stone": 1}},
{"stringId": "base:gold_ore", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 3.0, "textureIndices": [31, 31, 31, 31, 31, 31], "dropItem": "base:gold_ore", "groups": {"cracky": 2, "stone": 1}},
{"stringId": "base:diamond_ore", "isSolid": true, "isTransparent": false, "hasCollision": true, "hardness": 3.0, "textureIndices": [32, 32, 32, 32, 32, 32], "dropItem": "base:diamond_ore", "groups": {"cracky": 1, "stone": 1}}
```

Note: `textureIndices` values are placeholder sequential indices starting from 13 (after existing block textures 0–12).
Actual texture atlas slots depend on what textures exist. Since there is no rendering pipeline yet (Epic 6), these are
placeholders.

### Existing Files Modified

| File                                          | Change                                                                                          |
|-----------------------------------------------|-------------------------------------------------------------------------------------------------|
| `engine/include/voxel/world/WorldGenerator.h` | Add `StructureGenerator m_structureGen` member, `computeSurfaceHeight()` helper                 |
| `engine/src/world/WorldGenerator.cpp`         | Call `m_structureGen.populate()` in `generateChunkColumn()`, implement `computeSurfaceHeight()` |
| `engine/CMakeLists.txt`                       | Add `src/world/StructureGenerator.cpp`                                                          |
| `tests/CMakeLists.txt`                        | Add `tests/world/TestStructureGenerator.cpp`                                                    |
| `tests/world/TestWorldGenerator.cpp`          | Add integration tests for full pipeline with structures                                         |
| `assets/scripts/base/blocks.json`             | Add 16 new block entries                                                                        |

### New Files Created

| File                                              | Description                                                        |
|---------------------------------------------------|--------------------------------------------------------------------|
| `engine/include/voxel/world/StructureGenerator.h` | Class declaration, TreeSchematic struct, BiomeDecorationRules      |
| `engine/src/world/StructureGenerator.cpp`         | Implementation: tree schematics, placement, decorations, ore veins |
| `tests/world/TestStructureGenerator.cpp`          | Unit tests tagged `[world][structure]`                             |

### Architecture Compliance

- `StructureGenerator` lives in `voxel::world` namespace, outside ECS (ADR-004)
- No exceptions — use `VX_ASSERT` for programmer errors, `VX_LOG_WARN` for missing blocks (ADR-008)
- Synchronous on main thread — async deferred to Story 5.6 (ADR-006)
- One class per file, max ~500 lines. If `StructureGenerator.cpp` exceeds 500 lines, split tree schematics into a
  separate `TreeSchematics.h` (header-only constexpr data)
- Naming: PascalCase classes, camelCase methods, m_ members, SCREAMING_SNAKE constants
- `#pragma once`, explicit include order, no `using namespace` in headers
- Use `std::mt19937` (not `rand()`) for deterministic RNG
- All block data is data-driven via JSON — tree shapes are code-defined schematics (not JSON, since they're structural
  patterns not block properties)

### What NOT To Do

- Do NOT create a `PopulateManager` or `DecorationManager` — `StructureGenerator` is sufficient
- Do NOT read neighbor ChunkColumn data for cross-chunk trees — recompute surface height via noise
- Do NOT add biome-specific tree variation via Lua (Epic 9)
- Do NOT add async generation (Story 5.6)
- Do NOT add water fill for lakes/rivers (future story)
- Do NOT add structure generation from JSON templates (future — schematics are hardcoded for now)
- Do NOT modify ChunkManager, ChunkSection, ChunkColumn, BiomeSystem, BiomeTypes, CaveCarver, or SplineCurve
- Do NOT add cave decorations (stalactites, mushrooms — future)
- Do NOT add floating block fixes for trees carved by caves — acceptable for V1

### Previous Story Intelligence

Stories 4.1–4.4 established:

- `WorldGenerator` owns all generation components (`SplineCurve`, `BiomeSystem`, `CaveCarver`) as member objects
- Block IDs are cached at construction via `BlockRegistry::getIdByName()` with `VX_LOG_WARN` fallback
- `surfaceHeights[16][16]` is already collected during the terrain fill loop and passed to `CaveCarver::carveColumn()`
- `FastNoiseLite` is wrapped with `#pragma warning(push, 0)` / `#pragma warning(pop)` in .cpp files
- Chunk coordinate convention: `glm::ivec2` uses `.y` for Z axis
- World coordinates: `worldX = chunkCoord.x * 16 + localX`, `worldZ = chunkCoord.y * 16 + localZ`
- All noise is deterministic with `static_cast<int>(seed + N)` seeds
- `ChunkColumn::setBlock(x, y, z, id)` and `getBlock(x, y, z)` are the APIs for block manipulation

### Testing Strategy

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("StructureGenerator determinism", "[world][structure]") {
    SECTION("same seed same coord produces identical output") { ... }
    SECTION("different seeds produce different structures") { ... }
}

TEST_CASE("Tree placement respects biome", "[world][structure]") {
    SECTION("oak trees appear in Forest biome") { ... }
    SECTION("cactus appears in Desert biome") { ... }
    SECTION("no trees in Tundra or IcePlains") { ... }
}

TEST_CASE("Ore depth ranges", "[world][structure]") {
    SECTION("diamond only below y=16") { ... }
    SECTION("coal found up to y=128") { ... }
    SECTION("ores only replace stone") { ... }
}

TEST_CASE("Cross-chunk tree overlap", "[world][structure]") {
    SECTION("tree at chunk border has blocks in adjacent chunk") { ... }
}

TEST_CASE("Surface decorations", "[world][structure]") {
    SECTION("snow_layer only in Tundra/IcePlains") { ... }
    SECTION("decorations placed at surfaceHeight + 1") { ... }
}
```

Tests need `BlockRegistry` with all blocks loaded, `BiomeSystem`, and `WorldGenerator` instances. Create a test fixture
helper that sets up the full generation pipeline.

### Project Structure Reference

```
engine/include/voxel/world/
  StructureGenerator.h    ← NEW (Story 4.5)
engine/src/world/
  StructureGenerator.cpp  ← NEW (Story 4.5)
tests/world/
  TestStructureGenerator.cpp  ← NEW (Story 4.5)
```

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-04-terrain-generation.md#Story 4.5]
- [Source: _bmad-output/planning-artifacts/architecture.md#System 6 - World Generation]
- [Source: _bmad-output/planning-artifacts/architecture.md#Async Chunk Pipeline - Populate stage]
- [Source: _bmad-output/project-context.md#Critical Implementation Rules]
- [Source: assets/scripts/base/blocks.json — existing block format reference]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition struct, ModelType enum]
- [Source: engine/include/voxel/world/BlockRegistry.h — getIdByName(), loadFromJson() API]

## Dev Agent Record

### Agent Model Used

{{agent_model_name_version}}

### Debug Log References

### Completion Notes List

### File List
