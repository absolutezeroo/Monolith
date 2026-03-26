# Epic 6 — GPU-Driven Rendering

**Priority**: P0
**Dependencies**: Epic 2, Epic 5
**Goal**: Full indirect rendering pipeline — compute culling shader fills draw commands, single `vkCmdDrawIndexedIndirectCount` renders all visible chunks, texture arrays for block textures, deferred G-Buffer.

---

## Story 6.0: Vulkan Descriptor Infrastructure

**As a** developer,
**I want** descriptor pool, set layouts, and allocation helpers,
**so that** Stories 6.2–6.6 can bind SSBOs and texture arrays without each one reinventing descriptor management.

**Acceptance Criteria:**

**The actual problem in the current codebase:**
`Renderer::createPipeline()` creates a `VkPipelineLayout` with zero descriptor set layouts (Renderer.cpp line 300-303). Story 6.2 needs SSBO bindings (gigabuffer + per-draw data). Story 6.4 needs compute descriptors. Story 6.5 needs a sampler for the texture array. Without this story, each of 6.2/6.3/6.4/6.5 will independently hack descriptor creation.

**What to build:**
- `DescriptorAllocator` class in `engine/include/voxel/renderer/DescriptorAllocator.h`:
  - Wraps `VkDescriptorPool` with automatic growth (create new pool when current is full)
  - `allocate(VkDescriptorSetLayout) → Result<VkDescriptorSet>`
  - `resetPools()` — resets all pools (called at shutdown)
  - Owns all created pools, RAII cleanup
- `DescriptorLayoutBuilder` helper (can be a struct with chained methods):
  - `addBinding(binding, type, stageFlags) → self&`
  - `build(device) → Result<VkDescriptorSetLayout>`
- Update `PipelineConfig` (from Story 3.0 Part B) to accept `VkDescriptorSetLayout` array
- Create the descriptor set layout for chunk rendering: binding 0 = SSBO (gigabuffer), binding 1 = SSBO (per-draw data)
- Update `m_pipelineLayout` to include this layout
- Wire the Gigabuffer (already created in Story 2.4) as SSBO in descriptor set binding 0
- Unit tests: not applicable (GPU resources), but validate no Vulkan validation errors

**Files:**
```
engine/include/voxel/renderer/DescriptorAllocator.h
engine/src/renderer/DescriptorAllocator.cpp
```

---

## Story 6.1: Shared Quad Index Buffer

**As a** developer,
**I want** a single index buffer shared by all chunk draws,
**so that** I don't need per-chunk index buffers.

**Acceptance Criteria:**
- Pre-generated index buffer: pattern `{0,1,2, 2,3,0}` repeated for MAX_QUADS (e.g., 2M quads)
- Uploaded once at init to a DEVICE_LOCAL VkBuffer
- Sized for worst case: `MAX_QUADS * 6 * sizeof(uint32_t)`
- Bound once per frame, reused by all indirect draws
- `MAX_QUADS` configurable, default sufficient for 16-chunk render distance

---

## Story 6.2: Vertex Pulling Shader (chunk.vert / chunk.frag)

**As a** developer,
**I want** shaders that read quad data directly from the gigabuffer,
**so that** I can use the compact 8-byte format without traditional vertex attributes.

**Acceptance Criteria:**
- `chunk.vert`: no vertex input attributes; reads from SSBO (gigabuffer) via `gl_VertexID`
- Quad index = `gl_VertexID / 4`, corner index = `gl_VertexID % 4`
- Unpacks ALL fields from 8-byte packed data:
  - Bits 0–29: position, width, height, block type, face direction, AO (existing)
  - Bits 30–37: block type extension (existing)
  - Bits 38–48: face + AO details (existing)
  - Bits 49–56: light values → outputs `fragSkyLight`, `fragBlockLight` (float 0–1)
  - Bits 57–59: tint index → outputs `flat uint fragTintIndex`
  - Bits 60–61: waving type → used for vertex displacement
  - Bits 62–63: reserved (pass through as 0)
- Per-draw data (chunk world position) from SSBO indexed by `gl_DrawID`
- Reconstructs 4 corner positions from face direction + width/height
- **Waving vertex animation:** if wavingType > 0, apply displacement before world→clip transform:
  ```glsl
  if (wavingType == 1) { // Leaves: slow XZ sway
      float phase = dot(worldPos.xz, vec2(0.7, 0.3)) + time * 1.5;
      worldPos.x += sin(phase) * 0.04;
      worldPos.z += cos(phase * 1.3) * 0.04;
  } else if (wavingType == 2) { // Plants: faster Y+XZ bob
      float phase = dot(worldPos.xz, vec2(0.5, 0.5)) + time * 2.0;
      worldPos.x += sin(phase) * 0.08 * localY; // Anchored at base
      worldPos.y += sin(phase * 0.7) * 0.02 * localY;
      worldPos.z += cos(phase * 1.1) * 0.06 * localY;
  } else if (wavingType == 3) { // Liquid surface: wave pattern
      float phase = worldPos.x * 0.5 + worldPos.z * 0.3 + time * 1.0;
      worldPos.y += sin(phase) * 0.03;
  }
  ```
  - `time` uniform: elapsed seconds, updated per-frame from GameLoop
  - `localY`: normalized Y position within the block (0 at bottom, 1 at top) — plants are anchored at their base
- Outputs: `gl_Position` (world → clip), UV + texture layer index, AO value, world normal, light values, tint index
- `chunk.frag`: samples `sampler2DArray` with `vec3(u, v, textureLayer)`, applies AO as multiplier
- Renders correctly: textured blocks visible at correct world positions, leaves sway, plants bob

---

## Story 6.3: Indirect Draw Buffer + ChunkRenderInfo SSBO

**As a** developer,
**I want** GPU buffers for indirect draw commands and per-chunk render metadata,
**so that** the compute culling shader can fill them.

**Acceptance Criteria:**
- `IndirectBuffer` class: VkBuffer holding `VkDrawIndexedIndirectCommand[]` (STORAGE + INDIRECT usage)
- `DrawCountBuffer`: VkBuffer holding a single `uint32_t` draw count (STORAGE + INDIRECT usage)
- `ChunkRenderInfoBuffer`: SSBO holding per-chunk data (world position vec3, gigabuffer offset, quad count, bounding sphere)
- `ChunkRenderInfo` struct uploaded/updated when chunks are meshed or unloaded
- All buffers sized for max expected chunks (render distance² × height sections)
- Draw count reset to 0 at start of each frame (via `vkCmdFillBuffer` or mapped write)

---

## Story 6.4: Compute Culling Shader (cull.comp)

**As a** developer,
**I want** a compute shader that tests chunk visibility and fills the indirect draw buffer,
**so that** only visible chunks are rendered with zero CPU overhead.

**Acceptance Criteria:**
- `cull.comp`: workgroup size 64, dispatched with `ceil(chunkCount / 64)` groups
- Reads: `ChunkRenderInfo[]` SSBO, camera frustum planes (6 × vec4 uniform), camera position
- Per chunk: skip if quadCount == 0; test bounding sphere against 6 frustum planes
- If visible: `atomicAdd(drawCount, 1)` → fill `VkDrawIndexedIndirectCommand` at that index
- Command fields: `indexCount = quadCount * 6`, `instanceCount = 1`, `firstIndex = 0`, `vertexOffset = gigabufferOffset * 4`, `firstInstance = chunkIndex` (for per-draw data)
- Pipeline barrier: compute write → indirect draw read
- Validation: render matches frustum (no popping when turning camera)

---

## Story 6.5: Texture Array Loading

**As a** developer,
**I want** block textures loaded as a Vulkan texture array,
**so that** all block textures are available in a single bind.

**Acceptance Criteria:**
- Load PNG textures from `assets/textures/blocks/` (16×16 pixels each)
- Create `VkImage` with `VK_IMAGE_VIEW_TYPE_2D_ARRAY`, one layer per texture
- Generate mipmaps (5 levels for 16×16: 16, 8, 4, 2, 1) via `vkCmdBlitImage`
- Nearest-neighbor filtering for that pixel-art look (VK_FILTER_NEAREST for mag, linear for min+mip)
- `TextureArray` class: `loadTextures(directory) → Result<void>`, `getLayerIndex(textureName) → uint16_t`
- Block registry texture indices point into this array
- Sampler with `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`
- Descriptor set binding: single `sampler2DArray` accessible from fragment shader

---

## Story 6.6: Deferred Rendering G-Buffer Setup

**As a** developer,
**I want** a G-Buffer for deferred rendering,
**so that** lighting can be computed in screen space across all geometry types.

**Acceptance Criteria:**
- G-Buffer attachments: RT0 (RGBA8_SRGB: albedo.rgb + AO.a), RT1 (RG16_SFLOAT: normal.xy octahedral), Depth (D32_SFLOAT)
- Created at swapchain resolution, recreated on resize
- Geometry pass (chunk rendering) writes to G-Buffer via dynamic rendering with multiple color attachments
- Fullscreen lighting pass reads G-Buffer as input attachments / sampled textures
- Basic lighting: directional sun (hardcoded direction for now), ambient term, AO multiplied in
- Final composite to swapchain image
- ImGui renders on top after composite

---

## Story 6.7: Transparent & Translucent Rendering Pass

**As a** developer,
**I want** a separate render pass for transparent and translucent blocks,
**so that** glass, water, leaves, and ice render correctly with alpha blending.

**Why this must be done now:**
The opaque indirect draw pipeline (Stories 6.1–6.4) draws everything in one pass with no blending. Transparent blocks (glass = cutout alpha) and translucent blocks (water = partial alpha, stained glass = colored alpha) need different treatment. If we build the full rendering pipeline without this, adding it later requires restructuring the draw command generation, the compute culling, and the G-Buffer.

**Acceptance Criteria:**

**Block transparency types:**
- `BlockDefinition` gains `RenderType` enum: `Opaque`, `Cutout`, `Translucent`
- `Opaque`: standard pipeline, no blending (stone, dirt, wood)
- `Cutout`: alpha test, no blending — fragment discarded if alpha < 0.5 (leaves, flowers, tall grass)
- `Translucent`: alpha blending, sorted back-to-front (glass, water, ice, stained glass)

**Rendering order (3 passes per frame):**
1. **Opaque pass** (existing): indirect draw of all opaque + cutout chunks into G-Buffer
   - Cutout blocks: `chunk.frag` adds `if (albedo.a < 0.5) discard;`
   - G-Buffer still works (discarded fragments don't write depth or color)
2. **Deferred lighting pass** (Story 6.6): processes G-Buffer, outputs lit opaque scene
3. **Translucent forward pass** (NEW): renders translucent geometry on top of the lit scene
   - Separate pipeline: blending enabled (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA`)
   - Reads the depth buffer from the opaque pass (depth test ON, depth write OFF)
   - Sorted back-to-front per-chunk (not per-quad — approximate sorting is sufficient for V1)
   - No deferred lighting: translucent pass applies lighting directly in the fragment shader using per-vertex light values

**Meshing integration:**
- `MeshBuilder` produces two outputs per section: `opaqueQuads` and `translucentQuads`
- Transparent blocks in the section are meshed separately and stored in the translucent buffer
- Both uploaded to gigabuffer (separate allocations tracked in ChunkRenderInfo)
- The compute culling shader outputs TWO draw lists: opaque draws and translucent draws

**Face culling changes for transparent blocks:**
- Opaque-to-transparent boundary: both faces emitted (you see the stone face through the glass)
- Transparent-to-transparent same type: face culled (glass next to glass looks cleaner)
- Transparent-to-transparent different type: both faces emitted

**Sorting:**
- Translucent chunks sorted by distance to camera (centroid) each frame, CPU-side
- Sort the draw commands array before uploading to the indirect buffer
- V1: per-chunk sort, not per-face. Slight artifacts at chunk boundaries acceptable
- Future: OIT (order-independent transparency) can replace this

**Unit tests:** Section with glass block between two stone blocks → stone faces toward glass ARE emitted, glass faces toward stone ARE emitted, glass mesh in translucent buffer not opaque buffer.

---

## Story 6.8: Block Tinting Shader Support

**As a** developer,
**I want** the fragment shader to apply per-vertex biome tinting,
**so that** grass, leaves, and water change color based on location.

**Acceptance Criteria:**

**Shader changes (extending Story 6.2 chunk.vert/chunk.frag):**
- `chunk.vert` unpacks tint index from bits 57–59 of quad data, passes to fragment shader as `flat uint tintIndex`
- `chunk.frag` reads `TintPalette` from UBO/SSBO (8 entries × RGB)
- `albedo.rgb *= tintPalette[tintIndex].rgb;` — applied before G-Buffer write
- Index 0 = white (1,1,1) = no tint = no visual change

**Tint palette management:**
- `TintPaletteManager` class: builds the palette per-chunk or globally
- V1 (simple): global palette with 8 fixed colors, updated when player changes biome region
- Future: per-chunk palette uploaded alongside ChunkRenderInfo
- Palette uploaded as a small UBO (8 × vec4 = 128 bytes)

**Integration with biome system (Epic 4):**
- `WorldGenerator` or `BiomeManager` provides `getGrassTint(biomeType) → RGB`, `getFoliageTint(biomeType) → RGB`, `getWaterTint(biomeType) → RGB`
- On chunk mesh, the tint palette is populated from the chunk's dominant biome
- At biome boundaries: use nearest biome (slight color discontinuity at chunk borders acceptable for V1)

**Visual result:** Plains grass = bright green (#7CFC00), desert dead grass = brown (#BDB76B), taiga grass = dark green (#3B6324), jungle leaves = deep green (#228B22).
