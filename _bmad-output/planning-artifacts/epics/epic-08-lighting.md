# Epic 8 — Lighting

**Priority**: P1
**Dependencies**: Epic 3, Epic 6
**Goal**: Block light and sky light propagate correctly via BFS, update dynamically on place/break, deferred lighting pass renders the result, day/night cycle modulates sky light.

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
