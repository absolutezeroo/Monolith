# Epic 8 — Lighting

**Priority**: P1
**Dependencies**: Epic 3, Epic 6
**Goal**: Block light and sky light propagate correctly via BFS, update dynamically on place/break, deferred lighting pass renders the result, day/night cycle modulates sky light.

---

## Story 8.0: Wire Light Data into Meshing Pipeline

**As a** developer,
**I want** the mesher to read light values and bake them into vertex data,
**so that** Stories 8.1–8.4 can propagate light and see results rendered.

**Acceptance Criteria:**

**The actual problem:**
The mesher (Epic 5) was built to read only block IDs from ChunkSection. The quad format has 15 reserved bits (bits 49–63) that can hold light data. But the mesher's function signature, the snapshot struct, and the vertex shader all need modification before any lighting story can produce visible results.

**Mesher signature change:**
- `MeshBuilder::buildGreedy()` signature changes from:
  `(const ChunkSection&, neighbors[6]) → ChunkMesh`
  to:
  `(const ChunkSection&, neighbors[6], const LightMap*, neighborLightMaps[6]) → ChunkMesh`
- `LightMap*` is nullable — when null (before Epic 8 is implemented), mesher writes 0 into light bits (equivalent to full brightness, no lighting applied)
- When non-null, mesher averages the 4 adjacent light values per vertex and packs them into bits 49–56 of the quad: `[skyLight:4 | blockLight:4]`

**Snapshot change:**
- `MeshChunkTask` snapshot (Story 5.4) extended to include `LightMap` data per section + 6 neighbors
- Before Epic 8: snapshot copies null pointers for light maps (no overhead)
- After Epic 8: snapshot copies light data alongside block data

**Shader change:**
- `chunk.vert` (Story 6.2) unpacks bits 49–56 as two `float` outputs: `fragSkyLight`, `fragBlockLight` (each 0.0–1.0, mapped from 0–15)
- `chunk.frag` multiplies albedo by `max(skyLight * dayFactor, blockLight)` — but until Story 8.4 implements the lighting pass, this just outputs 1.0 (no change to visuals)

**Key property:** This story changes interfaces but produces ZERO visual change. Existing rendering looks identical. The light pipeline is just plumbed through and ready for Stories 8.1–8.4 to fill with real data.

- Unit tests: mesher with null LightMap produces identical output to before. Mesher with a test LightMap produces quads with non-zero light bits.

---

## Story 8.1: Light Data Storage + BFS Block Light

**As a** developer,
**I want** per-block light values stored and propagated via BFS,
**so that** torches and light-emitting blocks illuminate their surroundings.

**Acceptance Criteria:**
- `LightMap` per ChunkSection: `uint8_t lightData[4096]` — packed `[sky:4 | block:4]`
- Accessors: `getBlockLight(x,y,z)`, `setBlockLight(x,y,z,val)`, `getSkyLight(x,y,z)`, `setSkyLight(x,y,z,val)`
- BFS propagation for block light: seed from light-emitting blocks (torchLight=14), -1 attenuation per step, stop at 0 or opaque block
- Propagation crosses section boundaries (need neighbor section access)
- Queue-based BFS: `std::queue<glm::ivec3>` with light value tracking
- Light data included in chunk snapshot for meshing (light values baked into vertex data)
- Unit test: single torch in dark room → correct light falloff pattern; two torches → max of both values per block

---

## Story 8.2: Sky Light Propagation

**As a** developer,
**I want** sky light to propagate from the surface downward and horizontally,
**so that** outdoor areas and caves near the surface are naturally lit.

**Acceptance Criteria:**
- Sky light seeded at 15 for all blocks directly exposed to sky (no solid block above in the column)
- Downward propagation: sky light travels straight down without attenuation through air
- Horizontal propagation: BFS with -1 attenuation per horizontal step (like block light)
- Under overhangs: sky light attenuates horizontally from the overhang edge
- In caves: sky light doesn't penetrate unless there's an opening to the surface
- Heightmap per column: tracks highest solid block for fast sky exposure check
- Unit test: open air = 15, 3 blocks under overhang = 12 (3 horizontal steps), sealed cave = 0

---

## Story 8.3: Dynamic Light Updates

**As a** developer,
**I want** lighting to update correctly when blocks are placed or broken,
**so that** the world stays consistently lit during gameplay.

**Acceptance Criteria:**
- On block break (solid → air):
  1. If removed block was opaque: check if sky light should now reach below
  2. Re-propagate block light from all neighboring light sources
  3. Re-propagate sky light if sky column changed
- On block place (air → solid):
  1. Remove light at placed position
  2. Reverse-BFS: find all blocks whose light came through this position, reduce their values
  3. Re-propagate from remaining sources to fill correct values
- If placed block emits light: seed BFS from that position
- Light update triggers section dirty flag → remesh queued
- Multi-section updates handled (light crosses section boundaries)
- Performance: light update for single block change < 1ms

---

## Story 8.4: Deferred Lighting Pass + Day/Night Cycle

**As a** developer,
**I want** the deferred lighting pass to use light values and a day/night cycle,
**so that** the rendered world has proper illumination.

**Acceptance Criteria:**
- Light values baked into mesh vertices during meshing: average of 4 adjacent light values per vertex, stored in quad data
- `lighting.frag` shader reads G-Buffer (albedo, normal, depth) and per-vertex light values
- Block light contribution: `blockLightValue / 15.0` → warm orange tint
- Sky light contribution: `skyLightValue / 15.0 * dayNightFactor` → cool white/blue
- `dayNightFactor`: 1.0 at noon, 0.1 at midnight, smooth sinusoidal cycle
- Day/night cycle: configurable duration (default 20 minutes real-time = full day)
- Time-of-day uniform updated per frame, passed to lighting shader
- Basic sun direction changes with time of day (for directional light angle)
- Minimal ambient light so pitch-black caves are still barely visible (configurable, default 0.02)
