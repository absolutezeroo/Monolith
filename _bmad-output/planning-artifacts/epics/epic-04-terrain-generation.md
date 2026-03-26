# Epic 4 — Terrain Generation

**Priority**: P0
**Dependencies**: Epic 3
**Goal**: Procedurally generated terrain with biomes, caves, trees, producing a convincing Minecraft-like world from a seed.

---

## Story 4.1: FastNoiseLite Integration + Basic Heightmap

**As a** developer,
**I want** noise-based terrain height generation,
**so that** the world has varied elevation instead of flat ground.

**Acceptance Criteria:**
- FastNoiseLite integrated (single header, included in `voxel/world/`)
- `WorldGenerator` class: takes a `uint64_t seed`, produces terrain for a chunk column
- `generateChunkColumn(glm::ivec2 chunkCoord) → ChunkColumn` — fills sections with blocks based on noise height
- Basic heightmap: 2D Simplex noise (freq 0.01, 6 octaves FBm) → height in range [40, 120]
- Surface: grass on top, 3 layers of dirt below, stone underneath, bedrock at y=0
- Deterministic: same seed + same coord = same output, always
- Unit test: determinism (generate twice, compare block-by-block)

**Seed management:**
- Seed source priority: command line arg (`--seed 12345`) > `config.json` `"seed"` field > random (generated from system clock + PID)
- On first run: generate random seed, save to `config.json` AND `worlds/MyWorld/world.json`
- On subsequent runs: load seed from `world.json` (world-specific, not global config)
- Seed displayed in F3 debug overlay

**Spawn point calculation:**
- Initial spawn: find terrain height at world position (0, 0) using the heightmap
- Walk outward in spiral if (0,0) is underwater, in a cave, or otherwise unsuitable
- Spawn Y = highest solid block at (spawnX, spawnZ) + 2 (feet above ground)
- Player spawns in fly mode initially (before Story 7.3 adds gravity) — just set Camera position to spawn
- Save spawn position in `world.json` for respawn (future: beds change spawn)
- `WorldGenerator::findSpawnPoint(seed) → glm::dvec3` — called once at world creation

**Wiring into ChunkManager (resolves gap between Epic 3 and Epic 4):**
Story 3.5 created `ChunkManager::loadChunk()` which creates empty ChunkColumns. This story replaces that:
- `ChunkManager` receives a `WorldGenerator&` at construction (injected dependency, not owned)
- `ChunkManager::loadChunk(glm::ivec2 coord)` now calls `m_worldGen.generateChunkColumn(coord)` instead of creating empty columns
- GameApp owns the `WorldGenerator` instance and passes it to ChunkManager
- For now generation is synchronous on the main thread — Story 5.6 will make it async via enkiTS later
- If a saved chunk exists on disk (Story 3.7): load from disk instead of generating. Disk has priority over generation.

---

## Story 4.2: Spline Remapping + Elevation Distribution

**As a** developer,
**I want** spline curves to control how noise values map to terrain height,
**so that** I get distinct plains, hills, and mountains instead of uniform noise.

**Acceptance Criteria:**
- `SplineCurve` class: cubic Hermite spline with configurable control points
- `evaluate(float noiseValue) → float heightValue`
- Default spline: flat at low noise (plains), gentle rise (hills), steep rise (mountains), plateau at extreme
- Continent noise (freq 0.001, 4 octaves) feeds the spline → base elevation
- Detail noise (freq 0.02, 4 octaves) adds local variation on top
- Result: world has distinct flat areas, rolling hills, and mountain ranges
- Visually verifiable: walking around shows varied terrain

---

## Story 4.3: Biome System (Whittaker Diagram)

**As a** developer,
**I want** biome selection based on temperature and humidity noise maps,
**so that** different areas of the world have distinct surface blocks and features.

**Acceptance Criteria:**
- Two independent 2D noise maps: temperature (freq 0.005) and humidity (freq 0.005)
- `BiomeType` enum: Desert, Savanna, Plains, Forest, Jungle, Taiga, Tundra, IcePlains (minimum 8)
- Whittaker diagram lookup: `(temperature, humidity) → BiomeType`
- Each biome defines: surface block, sub-surface block, filler block, surface depth, decoration rules
- Biome blending at boundaries: sample 5×5 columns, weighted average of height functions by distance
- `WorldGenerator` uses biome to select surface blocks and modify height distribution
- Unit test: biome selection determinism, boundary blending produces smooth transitions

---

## Story 4.4: 3D Caves and Overhangs

**As a** developer,
**I want** 3D noise carving to create caves and overhangs,
**so that** the underground is interesting to explore.

**Acceptance Criteria:**
- 3D Simplex noise (freq 0.02, 3 octaves) sampled per block in solid regions
- If `noise3D(x,y,z) > threshold` → carve block to air (create cave)
- Threshold varies with depth: more caves at mid-depth, fewer near surface and bedrock
- Cave openings to surface possible (no artificial capping)
- Spaghetti caves: elongated cavities via stretched 3D noise (different scale per axis)
- Overhangs: where 3D noise diverges from 2D heightmap near surface
- No floating blocks left after carving (or acceptable as-is for V1)

---

## Story 4.5: Tree and Decoration Placement

**As a** developer,
**I want** trees and surface decorations placed per-biome,
**so that** the world looks alive and biomes are visually distinct.

**Acceptance Criteria:**
- `StructureGenerator` class: places multi-block structures during the Populate pipeline stage
- Tree types: oak (Forest/Plains), birch (Forest), spruce (Taiga), cactus (Desert), jungle tree (Jungle)
- Tree placement: per-column seeded RNG, density per biome, minimum spacing between trees
- Cross-chunk awareness: trees near chunk border generate trunk/leaves in neighbor chunk too
- Surface decorations: tall grass (Plains), flowers (Forest/Plains), dead bushes (Desert), snow layer (Tundra/IcePlains)
- Ore generation: stone replaced by ore veins (coal, iron, gold, diamond) at depth-appropriate ranges
- All structure/decoration types registered in BlockRegistry
