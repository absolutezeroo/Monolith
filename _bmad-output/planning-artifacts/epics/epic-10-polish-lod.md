# Epic 10 — Polish & LOD

**Priority**: P2
**Dependencies**: Epic 6, Epic 8
**Goal**: Advanced culling eliminates most invisible geometry, LOD enables longer render distances, SSAO adds depth to the visuals. This is the optimization and visual polish epic.

---

## Story 10.1: Tommo's Cave Culling Algorithm

**As a** developer,
**I want** graph-based occlusion culling that eliminates underground geometry the player can't see,
**so that** rendering performance improves by 50–99% in cave-heavy areas.

**Acceptance Criteria:**
- **Phase 1 — Connectivity graph** (computed during meshing, per section):
  - Flood-fill through non-opaque blocks in the 16³ section
  - Record which of the 6 section faces can see which other faces
  - 15 possible pairs (C(6,2)), stored as `uint16_t` bitmask per section
  - Stored in `ChunkRenderInfo` alongside mesh data
- **Phase 2 — Visibility BFS** (computed per frame, CPU):
  - Start from the section containing the camera
  - BFS to neighbor sections: only traverse if connectivity confirms entry_face→exit_face path exists
  - Only traverse in directions moving away from camera (dot(faceNormal, toCameraDir) < 0)
  - Output: set of visible section indices, used to filter the compute culling input
- Integration: visibility BFS result filters which sections are sent to the GPU cull pass
- Debug visualization: ImGui option to color-code sections by visibility status
- Performance: BFS completes in < 0.5ms for 16-chunk render distance

---

## Story 10.2: LOD via POP Buffers

**As a** developer,
**I want** level-of-detail for distant chunks,
**so that** render distance can be extended without proportional performance cost.

**Acceptance Criteria:**
- POP buffer implementation: vertex positions snapped to coarser grids at distance
  - LOD 0: full resolution (0–4 chunks)
  - LOD 1: snap to 2-block grid (4–8 chunks)
  - LOD 2: snap to 4-block grid (8–16 chunks)
  - LOD 3: snap to 8-block grid (16–32 chunks)
- Geomorphing in vertex shader: smooth transition between LOD levels based on distance
  - `morphFactor = smoothstep(lodStartDist, lodEndDist, distance)`
  - Final position = `mix(fullResPos, snappedPos, morphFactor)`
- LOD level assigned per chunk in `ChunkRenderInfo`, passed to vertex shader via per-draw SSBO
- No visible popping at LOD transitions when moving
- Meshing: LOD meshes generated from lower-resolution versions of the chunk data (subsample)
- Memory savings: LOD 3 uses ~1/8th the quads of LOD 0
- Configurable LOD distances via ImGui debug panel

---

## Story 10.3: Hierarchical Z-Buffer Occlusion Culling

**As a** developer,
**I want** GPU-driven occlusion culling using the previous frame's depth buffer,
**so that** chunks hidden behind mountains or buildings are not rendered.

**Acceptance Criteria:**
- Two-pass rendering approach:
  1. Render chunks that were visible in the previous frame → produce Z-buffer
  2. Build HZB (hierarchical Z-buffer) via compute shader: mip chain where each texel = max depth of 4 parent texels
  3. Test all remaining chunks against HZB: project chunk bounding box to screen, compare max depth at that mip level
  4. Render newly visible chunks
- HZB compute shader: dispatches per mip level, reads previous mip, writes max of 2×2 block
- Chunk test: conservative — if any pixel of the projected AABB could be visible, include it
- Latency: 1-frame lag acceptable (chunk visible for 1 frame after becoming occluded, invisible for 1 frame after becoming unoccluded)
- Integration with existing compute cull pass: add HZB test after frustum test
- Toggle via ImGui for performance comparison

---

## Story 10.4: Screen-Space Ambient Occlusion (SSAO)

**As a** developer,
**I want** SSAO in the deferred lighting pass,
**so that** visual depth perception improves beyond the baked per-vertex AO.

**Acceptance Criteria:**
- SSAO compute/fragment shader: sample depth buffer in a hemisphere around each pixel
- Kernel: 32–64 samples in tangent-space hemisphere, scaled by distance
- Noise texture: 4×4 rotation noise, tiled across screen, eliminates banding
- Blur pass: 4×4 bilateral blur to smooth noise artifacts while preserving edges
- SSAO result: single-channel texture multiplied into the lighting pass output
- Radius and intensity configurable via ImGui
- Performance: < 1ms at 1080p on GTX 1660
- Combined with baked vertex AO: `finalAO = bakedAO * ssaoValue` (both contribute)
- Toggle via ImGui for visual comparison
