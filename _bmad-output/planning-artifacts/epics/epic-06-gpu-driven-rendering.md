# Epic 6 — GPU-Driven Rendering

**Priority**: P0
**Dependencies**: Epic 2, Epic 5
**Goal**: Full indirect rendering pipeline — compute culling shader fills draw commands, single `vkCmdDrawIndexedIndirectCount` renders all visible chunks, texture arrays for block textures, deferred G-Buffer.

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
- Unpacks position, width, height, block type, face direction, AO from 8-byte packed data
- Per-draw data (chunk world position) from SSBO indexed by `gl_DrawID`
- Reconstructs 4 corner positions from face direction + width/height
- Outputs: `gl_Position` (world → clip), UV + texture layer index, AO value, world normal
- `chunk.frag`: samples `sampler2DArray` with `vec3(u, v, textureLayer)`, applies AO as multiplier
- Renders correctly: textured blocks visible at correct world positions

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
