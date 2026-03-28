# Story 6.2: Vertex Pulling Shader (chunk.vert / chunk.frag)

Status: in-progress

## Story

As a developer,
I want shaders that read quad data directly from the gigabuffer SSBO,
so that I can render chunk meshes using the compact 8-byte packed quad format without traditional vertex attributes.

## Acceptance Criteria

1. **AC1 — Vertex pulling from SSBO**: `chunk.vert` has zero vertex input attributes. Reads packed 64-bit quad data from the gigabuffer SSBO (binding 0) via `gl_VertexIndex`. Quad index = `gl_VertexIndex / 4`, corner index = `gl_VertexIndex % 4`.
2. **AC2 — Full bit unpacking**: Unpacks ALL fields from the 8-byte packed format defined in `ChunkMesh.h`: X/Y/Z position, width/height, block state ID, face direction, 4 AO corners, flip, tint index, waving type. Bit positions match the C++ `packQuad()` exactly.
3. **AC3 — Corner reconstruction**: Reconstructs 4 corner positions from the base position, face direction, and width/height. Quad diagonal flip (bit 57) is respected for AO-correct triangle winding.
4. **AC4 — World-space transform**: Adds chunk world position (from push constants) to local section position, then multiplies by VP matrix to produce `gl_Position`.
5. **AC5 — Waving vertex animation**: Displaces vertices before clip transform when `wavingType > 0`. Leaves (type 1) sway in XZ, plants (type 2) bob anchored at base, liquid (type 3) has surface waves. Driven by `time` push constant.
6. **AC6 — AO interpolation**: Per-corner AO values (0–3) are interpolated across the quad face. The fragment shader applies AO as a darkening multiplier.
7. **AC7 — Placeholder fragment output**: `chunk.frag` outputs face-normal-based coloring with AO applied (textures come in Story 6.5). Top faces green, bottom brown, sides gray.
8. **AC8 — Renderer integration**: Renderer replaces the test triangle draw with chunk section draws. Iterates `ChunkRenderInfoMap`, issues `vkCmdDrawIndexed` per resident section. Binds descriptor set and pushes constants per draw.
9. **AC9 — Push constants updated**: `ChunkPushConstants` replaces `float padding[3]` with `glm::vec3 chunkWorldPos`. Still 80 bytes. Updated per draw call with the section's world origin.
10. **AC10 — Zero validation errors**: No Vulkan validation layer errors during startup or rendering.

## Tasks / Subtasks

- [x] **Task 1: Enable `shaderDrawParameters` Vulkan feature** (AC: #1)
  - [x] 1.1 In `VulkanContext.cpp`, add `VkPhysicalDeviceVulkan11Features` with `shaderDrawParameters = VK_TRUE` to the device selector chain
  - [x] 1.2 Chain it via `set_required_features_11()` in the vk-bootstrap physical device selector

- [x] **Task 2: Update `ChunkPushConstants`** (AC: #9)
  - [x] 2.1 In `Renderer.h`, replace `float padding[3]` with `glm::vec3 chunkWorldPos` in `ChunkPushConstants`
  - [x] 2.2 Verify `static_assert(sizeof(ChunkPushConstants) == 80)` still passes
  - [x] 2.3 Update `VkPushConstantRange.stageFlags` to include `VK_SHADER_STAGE_FRAGMENT_BIT` (the fragment shader needs `time` for future use)

- [x] **Task 3: Create `chunk.vert`** (AC: #1, #2, #3, #4, #5, #6)
  - [x] 3.1 Create `assets/shaders/chunk.vert` (GLSL 450, Vulkan 1.3 target)
  - [x] 3.2 Declare SSBO binding 0 as `readonly buffer Gigabuffer { uint data[]; }` (read packed quads as pairs of uint32)
  - [x] 3.3 Declare push constants: `mat4 viewProjection`, `float time`, 3x`float chunkWorldPos{X,Y,Z}` (3 separate floats to avoid vec3 16-byte alignment in std430)
  - [x] 3.4 Implement quad unpacking: extract all fields from `lo` (bits 0–31) and `hi` (bits 32–63) using `bitfieldExtract()`
  - [x] 3.5 Implement corner reconstruction for all 6 face directions (see Dev Notes for corner table)
  - [x] 3.6 Implement quad diagonal flip: swap corners 1 and 3 when flip bit is set
  - [x] 3.7 Implement waving displacement (leaves/plants/liquid) driven by `time`
  - [x] 3.8 Output varying attributes: `worldPos`, `normal`, `uv`, `ao` (interpolated float), `flat uint blockStateId`, `flat uint tintIndex`

- [x] **Task 4: Create `chunk.frag`** (AC: #6, #7)
  - [x] 4.1 Create `assets/shaders/chunk.frag` (GLSL 450)
  - [x] 4.2 Inputs: worldPos, normal, uv, ao, flat blockStateId, flat tintIndex
  - [x] 4.3 Placeholder coloring: top (PosY) = green `(0.3, 0.8, 0.2)`, bottom (NegY) = brown `(0.5, 0.3, 0.1)`, sides = gray `(0.6, 0.6, 0.6)`
  - [x] 4.4 Apply AO: `color *= ao` (ao is 0.0–1.0 float, where 1.0 = no occlusion)
  - [x] 4.5 Output to `outColor` (vec4, alpha = 1.0)

- [x] **Task 5: Update Renderer to load chunk shaders** (AC: #8)
  - [x] 5.1 In `Renderer::init()`, change shader paths from `triangle.vert.spv`/`triangle.frag.spv` to `chunk.vert.spv`/`chunk.frag.spv`
  - [x] 5.2 Enable backface culling: change `raster.cullMode` from `VK_CULL_MODE_NONE` to `VK_CULL_MODE_BACK_BIT` in `buildPipeline()`
  - [x] 5.3 Keep `raster.frontFace = VK_FRONT_FACE_CLOCKWISE` (match shader corner winding)

- [x] **Task 6: Add `renderChunks()` method** (AC: #8, #9)
  - [x] 6.1 Add public method `void renderChunks(const ChunkRenderInfoMap& renderInfos, const glm::mat4& viewProjection)` to Renderer
  - [x] 6.2 Bind descriptor set: `vkCmdBindDescriptorSets(cmd, GRAPHICS, m_pipelineLayout, 0, 1, &m_chunkDescriptorSet, ...)`
  - [x] 6.3 Bind quad index buffer: `m_quadIndexBuffer->bind(cmd)` (from Story 6.1)
  - [x] 6.4 Iterate `renderInfos`, skip non-Resident or zero-quadCount entries
  - [x] 6.5 Per draw: push `ChunkPushConstants` with VP, time, `worldBasePos`
  - [x] 6.6 Per draw: `vkCmdDrawIndexed(cmd, quadCount * 6, 1, 0, vertexOffset, 0)` where `vertexOffset = allocation.offset / 2`
  - [x] 6.7 Track draw count and quad count for debug overlay via `m_lastDrawCount`/`m_lastQuadCount`

- [x] **Task 7: Wire `renderChunks()` from GameApp** (AC: #8)
  - [x] 7.1 In `Renderer::beginFrame()`, removed `vkCmdDraw(cmd, 3, 1, 0, 0)` — drawing is now caller-driven
  - [x] 7.2 In `GameApp::render()`, call `m_renderer.renderChunks(m_uploadManager->getAllRenderInfos(), vp)` using `camera.getProjectionMatrix() * camera.getViewMatrix()`
  - [x] 7.3 Call happens between `beginFrame()` and `endFrame()`, after pipeline bind
  - [x] 7.4 Update debug overlay to show draw call count and total quads rendered

- [ ] **Task 8: Build and validate** (AC: #10)
  - [x] 8.1 Verify CMake auto-discovers `chunk.vert` and `chunk.frag` in `assets/shaders/` (GLOB in `CompileShaders.cmake`)
  - [x] 8.2 Build with zero errors/warnings
  - [ ] 8.3 Run with Vulkan validation layers: zero descriptor/pipeline/shader errors
  - [ ] 8.4 Visual verification: terrain chunks render as colored blocks with AO shading, waving blocks animate
  - [x] 8.5 All existing unit tests pass (no regressions) — 161 test cases, 488,988 assertions

## Dev Notes

### Quad Bit Layout (from ChunkMesh.h — THE SOURCE OF TRUTH)

The gigabuffer stores uint64_t quads. The shader reads pairs of uint32:

```
lo = data[quadIndex * 2 + 0]  (bits 0-31 of the 64-bit quad)
hi = data[quadIndex * 2 + 1]  (bits 32-63 of the 64-bit quad)

lo field extraction:
  X position     = bitfieldExtract(lo, 0,  6)    // 0-63
  Y position     = bitfieldExtract(lo, 6,  6)    // 0-63
  Z position     = bitfieldExtract(lo, 12, 6)    // 0-63
  Width - 1      = bitfieldExtract(lo, 18, 6)    // add 1 to get width
  Height - 1     = bitfieldExtract(lo, 24, 6)    // add 1 to get height
  BlockState low = bitfieldExtract(lo, 30, 2)    // low 2 bits of 16-bit ID

hi field extraction:
  BlockState high = bitfieldExtract(hi, 0,  14)  // high 14 bits
  BlockStateId    = blockStateLow | (blockStateHigh << 2)
  Face direction  = bitfieldExtract(hi, 14, 3)   // 0-5 (PosX,NegX,PosY,NegY,PosZ,NegZ)
  AO corner 0     = bitfieldExtract(hi, 17, 2)   // 0-3
  AO corner 1     = bitfieldExtract(hi, 19, 2)   // 0-3
  AO corner 2     = bitfieldExtract(hi, 21, 2)   // 0-3
  AO corner 3     = bitfieldExtract(hi, 23, 2)   // 0-3
  Flip            = bitfieldExtract(hi, 25, 1)    // quad diagonal flip
  IsNonCubic      = bitfieldExtract(hi, 26, 1)   // skip in this story (model vertices)
  Tint index      = bitfieldExtract(hi, 27, 3)   // 0-7
  Waving type     = bitfieldExtract(hi, 30, 2)   // 0-3
```

**CRITICAL**: These bit positions are derived from the C++ `packQuad()` function split at the uint32 boundary (bit 32). Do NOT use the epic's approximate bit descriptions — use these exact values derived from `ChunkMesh.h`.

### NO 64-bit Integers in GLSL

GLSL 450 has no native `uint64_t`. Read the gigabuffer as `uint data[]` (uint32 array). Each quad occupies 2 consecutive elements. Do NOT use `GL_ARB_gpu_shader_int64` — it's not universally supported.

### Corner Reconstruction Per Face

The 4 corners of a quad depend on face direction. `width` and `height` from the greedy mesher map to two axes (the two axes NOT along the face normal). Corner order must produce **clockwise winding** (matching `VK_FRONT_FACE_CLOCKWISE`).

**IMPORTANT**: The exact corner order must match the greedy mesher's output. Trace through the greedy mesher code (`GreedyMesher.cpp`) to verify which axis is "width" vs "height" for each face. The table below shows the expected mapping — verify before committing:

```
Face PosX (+X, normal right):  plane at x+1
  c0 = (x+1,  y,          z       )   uv (0, 0)
  c1 = (x+1,  y+height,   z       )   uv (0, H)
  c2 = (x+1,  y+height,   z+width )   uv (W, H)
  c3 = (x+1,  y,          z+width )   uv (W, 0)

Face NegX (-X, normal left):   plane at x
  c0 = (x,    y,          z+width )   uv (0, 0)
  c1 = (x,    y+height,   z+width )   uv (0, H)
  c2 = (x,    y+height,   z       )   uv (W, H)
  c3 = (x,    y,          z       )   uv (W, 0)

Face PosY (+Y, normal up):     plane at y+1
  c0 = (x,        y+1,  z       )   uv (0, 0)
  c1 = (x,        y+1,  z+height)   uv (0, H)
  c2 = (x+width,  y+1,  z+height)   uv (W, H)
  c3 = (x+width,  y+1,  z       )   uv (W, 0)

Face NegY (-Y, normal down):   plane at y
  c0 = (x+width,  y,    z       )   uv (0, 0)
  c1 = (x+width,  y,    z+height)   uv (0, H)
  c2 = (x,        y,    z+height)   uv (W, H)
  c3 = (x,        y,    z       )   uv (W, 0)

Face PosZ (+Z, normal front):  plane at z+1
  c0 = (x+width,  y,          z+1)   uv (0, 0)
  c1 = (x+width,  y+height,   z+1)   uv (0, H)
  c2 = (x,        y+height,   z+1)   uv (W, H)
  c3 = (x,        y,          z+1)   uv (W, 0)

Face NegZ (-Z, normal back):   plane at z
  c0 = (x,        y,          z  )   uv (0, 0)
  c1 = (x,        y+height,   z  )   uv (0, H)
  c2 = (x+width,  y+height,   z  )   uv (W, H)
  c3 = (x+width,  y,          z  )   uv (W, 0)
```

The index buffer pattern `{0,1,2, 2,3,0}` produces two triangles per quad. With the corner order above, these should be clockwise when viewed from the front of the face. **If faces render inside-out, swap c1↔c3** (or change frontFace to counter-clockwise).

### Quad Diagonal Flip

When `flip == 1`, the quad diagonal is flipped to fix AO interpolation artifacts. The index buffer's fixed pattern `{0,1,2, 2,3,0}` produces triangles (0,1,2) and (2,3,0). To flip the diagonal, swap the AO corners assigned to vertices 1 and 3 so the split line runs (0,2) instead of (1,3). Alternatively: swap the positions of corners 1 and 3. Choose whichever approach the greedy mesher assumes — check `GreedyMesher.cpp` for the flip convention.

### Waving Animation (from Epic AC)

```glsl
if (wavingType == 1u) {  // Leaves: slow XZ sway
    float phase = dot(worldPos.xz, vec2(0.7, 0.3)) + time * 1.5;
    worldPos.x += sin(phase) * 0.04;
    worldPos.z += cos(phase * 1.3) * 0.04;
}
else if (wavingType == 2u) {  // Plants: faster Y+XZ bob, anchored at base
    float localY = fract(localPos.y);  // 0 at block bottom, ~1 at top
    float phase = dot(worldPos.xz, vec2(0.5, 0.5)) + time * 2.0;
    worldPos.x += sin(phase) * 0.08 * localY;
    worldPos.y += sin(phase * 0.7) * 0.02 * localY;
    worldPos.z += cos(phase * 1.1) * 0.06 * localY;
}
else if (wavingType == 3u) {  // Liquid: surface wave
    float phase = worldPos.x * 0.5 + worldPos.z * 0.3 + time * 1.0;
    worldPos.y += sin(phase) * 0.03;
}
```

`localPos` is the vertex position within the section (0–15 range for each axis). `worldPos` = `chunkWorldPos + localPos`. Apply waving AFTER computing worldPos, BEFORE multiplying by VP.

### AO Interpolation

AO corners (0–3) map to corners 0–3. Convert to float: `float ao = float(3 - aoValue) / 3.0` (3 = no occlusion → 1.0, 0 = full occlusion → 0.0). Interpolate across the quad via the fragment shader (non-flat varying). The fragment shader multiplies `color *= ao`.

### vertexOffset Calculation

Each `vkCmdDrawIndexed` call uses `vertexOffset` to shift `gl_VertexIndex` so the shader reads from the correct gigabuffer offset:

```
gigabufferByteOffset = allocation.offset
quadsAtOffset        = gigabufferByteOffset / 8     (each quad = 8 bytes)
verticesAtOffset     = quadsAtOffset * 4             (4 vertices per quad)
vertexOffset         = static_cast<int32_t>(verticesAtOffset)
```

Simplification: `vertexOffset = allocation.offset / 2` (since offset is always 8-byte aligned).

### Shader SSBO Declaration

```glsl
layout(std430, set = 0, binding = 0) readonly buffer Gigabuffer {
    uint data[];  // packed quad data as uint32 pairs
} gigabuffer;
```

Binding 1 (ChunkRenderInfo SSBO) is declared in the layout but NOT read in Story 6.2 — world position comes from push constants. Story 6.3 will populate binding 1 and the shader will switch to SSBO-based positioning.

### Push Constants (GLSL)

```glsl
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;   // 64 bytes
    float time;            // 4 bytes
    vec3 chunkWorldPos;    // 12 bytes
} pc;
```

**std140/std430 note**: In push constants, `vec3` after `float` starts at offset 68 with no padding gap because push constants follow std430-like rules (vec3 aligns to 4 bytes, not 16). Total = 80 bytes.

### Renderer Changes Summary

**`Renderer.h` modifications:**
```cpp
// Replace padding with world position in ChunkPushConstants:
struct ChunkPushConstants {
    glm::mat4 viewProjection;  // 64 bytes
    float time;                 // 4 bytes
    glm::vec3 chunkWorldPos;    // 12 bytes
};
static_assert(sizeof(ChunkPushConstants) == 80);

// Add forward declaration:
class QuadIndexBuffer;  // from Story 6.1

// Add member:
std::unique_ptr<QuadIndexBuffer> m_quadIndexBuffer;

// Add public method:
void renderChunks(const ChunkRenderInfoMap& renderInfos, const glm::mat4& viewProjection);
```

**`Renderer::init()` changes:**
- Shader paths: `chunk.vert.spv` / `chunk.frag.spv` (replaces `triangle.vert.spv` / `triangle.frag.spv`)
- Push constant stage flags: add `VK_SHADER_STAGE_FRAGMENT_BIT` for future use

**`Renderer::beginFrame()` changes:**
- Remove `vkCmdDraw(cmd, 3, 1, 0, 0)` — no more test triangle
- Pipeline bind stays (caller draws between beginFrame/endFrame)
- Caller (GameApp) calls `renderChunks()` after `beginFrame()` returns true

**`buildPipeline()` changes:**
- `raster.cullMode = VK_CULL_MODE_BACK_BIT` (was `VK_CULL_MODE_NONE`)

**`renderChunks()` implementation:**
```
1. Get current frame's command buffer
2. Bind descriptor set 0 (m_chunkDescriptorSet)
3. Bind quad index buffer (Story 6.1)
4. For each (key, info) in renderInfos:
   a. Skip if state != Resident or quadCount == 0
   b. Fill ChunkPushConstants: VP, time, worldBasePos
   c. vkCmdPushConstants(cmd, layout, VERTEX_BIT, 0, 80, &pc)
   d. vertexOffset = info.allocation.offset / 2
   e. vkCmdDrawIndexed(cmd, info.quadCount * 6, 1, 0, vertexOffset, 0)
5. Store draw count + total quads for debug overlay
```

**`GameApp` changes:**
- In render path: after `beginFrame()` returns true, call `m_renderer.renderChunks(m_uploadManager->getAllRenderInfos(), m_camera.getViewProjection())`
- Update debug overlay: add draw call count and total rendered quads

### Existing Code to Reuse — DO NOT REINVENT

- **`ChunkMesh.h` bit layout** — ALL bit positions and pack/unpack functions. The shader must mirror these exactly.
- **`ChunkRenderInfoMap`** (ChunkRenderInfo.h:56) — already populated by `ChunkUploadManager`. Just iterate it.
- **`ChunkUploadManager::getAllRenderInfos()`** (ChunkUploadManager.h:47) — returns const ref to the map.
- **`Gigabuffer::getBuffer()`** — already bound as SSBO at descriptor set binding 0 (Story 6.0).
- **`m_chunkDescriptorSet`** — already allocated and written with gigabuffer (Story 6.0).
- **`Camera::getViewProjection()`** — returns the combined VP matrix.
- **`QuadIndexBuffer::bind(cmd)`** — from Story 6.1, binds the shared index buffer.
- **`CompileShaders.cmake`** — auto-discovers `.vert`/`.frag`/`.comp` in `assets/shaders/` via `CONFIGURE_DEPENDS` glob. No CMakeLists changes needed for new shaders.

### What NOT To Do

- Do NOT use `uint64_t` or `GL_ARB_gpu_shader_int64` in GLSL — read as `uint` pairs
- Do NOT sample textures — no texture array until Story 6.5. Use placeholder face colors.
- Do NOT implement indirect drawing — Story 6.4 adds compute culling + `vkCmdDrawIndexedIndirectCount`
- Do NOT create/populate the ChunkRenderInfo SSBO (binding 1) — Story 6.3 handles that. Use push constants for chunk world position.
- Do NOT handle model vertices (non-cubic blocks) — only packed quads. Bit 58 (isNonCubic) quads should not appear in the gigabuffer for FullCube blocks.
- Do NOT add texture array descriptor bindings or sampler declarations — Story 6.5
- Do NOT delete `triangle.vert`/`triangle.frag` — keep them as reference; they'll be unused
- Do NOT change the Gigabuffer, StagingBuffer, or ChunkUploadManager — they already work correctly
- Do NOT implement per-draw SSBO indexing via `gl_DrawID` yet — Story 6.3 will add that

### VulkanContext Feature Addition

`shaderDrawParameters` is mandatory in Vulkan 1.2+ (core feature) and guaranteed on all Vulkan 1.3 implementations. Enabling it explicitly is good practice for clarity. In `VulkanContext.cpp`:

```cpp
VkPhysicalDeviceVulkan11Features features11{};
features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
features11.shaderDrawParameters = VK_TRUE;  // enables gl_DrawID, gl_BaseVertex, gl_BaseInstance
```

Chain via `features12.pNext = &features11;` or pass to vk-bootstrap selector. This isn't strictly needed for Story 6.2 (which uses push constants), but prepares for Story 6.3/6.4 which use `gl_DrawID`.

### Backface Culling Note

The current pipeline has `VK_CULL_MODE_NONE`. Enabling `VK_CULL_MODE_BACK_BIT` is correct for voxel rendering (all faces face outward) and roughly halves fragment work. If faces are invisible, the corner winding is wrong — fix the corner table, don't disable culling.

### Dependency: Story 6.1 (Shared Quad Index Buffer)

Story 6.2 requires `QuadIndexBuffer` from Story 6.1. If 6.1 is not yet implemented when 6.2 development starts, implement 6.1 first — it is a prerequisite. The `vkCmdDrawIndexed` calls require the shared index buffer to be bound.

### Project Structure Notes

```
assets/shaders/
  chunk.vert              ← CREATE: vertex pulling shader
  chunk.frag              ← CREATE: placeholder fragment shader
  triangle.vert           (keep, unused)
  triangle.frag           (keep, unused)

engine/include/voxel/renderer/
  Renderer.h              ← MODIFY: ChunkPushConstants, renderChunks(), m_quadIndexBuffer

engine/src/renderer/
  Renderer.cpp            ← MODIFY: shader paths, renderChunks() impl, remove triangle draw
  VulkanContext.cpp        ← MODIFY: enable shaderDrawParameters

game/src/
  GameApp.cpp             ← MODIFY: call renderChunks() in render loop
```

No new C++ files. No CMakeLists changes (shader auto-discovery handles new `.vert`/`.frag` files).

### Previous Story Intelligence

**From Story 6.0 (Vulkan Descriptor Infrastructure):**
- Descriptor set layout: binding 0 = SSBO (gigabuffer), binding 1 = SSBO (ChunkRenderInfo — unwritten)
- `m_chunkDescriptorSet` allocated and gigabuffer written to binding 0
- Pipeline layout includes descriptor set layout + 80-byte push constant range (VERTEX_BIT)
- `ChunkPushConstants` struct defined in `Renderer.h` — modify it (replace padding with chunkWorldPos)
- Destruction order: pools → descriptor set layout → pipeline layout

**From Story 6.1 (Shared Quad Index Buffer):**
- `QuadIndexBuffer` class with `bind(cmd)`, `getMaxQuads()`, `getIndexCount()`
- Index pattern `{0,1,2, 2,3,0}` repeated for 2M quads
- `uint32_t` indices (handles vertex IDs > 65535)
- Owned by Renderer via `m_quadIndexBuffer`

**From Story 5.7 (Mesh Upload to Gigabuffer):**
- `ChunkUploadManager` uploads packed quad data to gigabuffer via `StagingBuffer`
- `ChunkRenderInfoMap` tracks per-section allocations: offset, quadCount, worldBasePos, state
- Only `RenderState::Resident` entries have valid gigabuffer data
- 8 uploads/frame max, distance-based priority

**From Story 5.5 (Block Tinting + Waving):**
- Tint index (3 bits) and waving type (2 bits) packed into quad format
- WavingType: 0=None, 1=Leaves, 2=Plants, 3=Liquid
- TintIndex 0 = no tint (white). Actual tint colors come in Story 6.8.

### Git Intelligence

Recent commit convention: `feat(renderer): description`. For this story:
```
feat(renderer): implement vertex pulling shader for chunk rendering
```

Recent commits:
```
c42d31d feat(renderer): implement DescriptorAllocator and layout system for Vulkan descriptors
de3f666 finalize Story 5.7: resolve code review issues for GPU mesh upload
398b602 feat(renderer): introduce shared quad index buffer for chunk rendering
4c391d2 feat(renderer): integrate tint/waving into greedy mesher and finalize Story 5.5
```

### Testing Standards

- **No unit tests for shaders** — GPU resources require a live VkDevice
- **Vulkan validation layers** — zero errors/warnings on startup and during rendering
- **Visual verification** — terrain renders as colored blocks with visible AO darkening; waving animation on leaves/plants/liquid; camera movement shows correct 3D perspective
- **Regression check** — all existing unit tests pass

### References

- [Source: engine/include/voxel/renderer/ChunkMesh.h — packQuad() bit layout, BlockFace enum, ChunkMesh struct]
- [Source: engine/include/voxel/renderer/Renderer.h — ChunkPushConstants, PipelineConfig, DescriptorAllocator members]
- [Source: engine/src/renderer/Renderer.cpp — init() descriptor setup (lines 51-148), beginFrame() draw call (line 705), buildPipeline() (lines 409-510)]
- [Source: engine/include/voxel/renderer/ChunkRenderInfo.h — ChunkRenderInfo struct, SectionKey, ChunkRenderInfoMap]
- [Source: engine/include/voxel/renderer/ChunkUploadManager.h — getAllRenderInfos(), processUploads()]
- [Source: engine/include/voxel/renderer/Gigabuffer.h — getBuffer(), SSBO already bound to descriptor binding 0]
- [Source: engine/src/renderer/VulkanContext.cpp — features12/features13 setup (lines 110-131)]
- [Source: cmake/CompileShaders.cmake — auto-discovers .vert/.frag/.comp via GLOB CONFIGURE_DEPENDS]
- [Source: game/src/GameApp.cpp — m_uploadManager usage (lines 153, 266-269), render loop, debug overlay (lines 330-336)]
- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.2 AC, waving animation code]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-009 Gigabuffer, Vertex format spec, Indirect pipeline]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling patterns]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List
- ChunkManager chunk streaming (`streamChunks()`, `setRenderDistance()`) was added out-of-scope to enable rendering; not part of story ACs.

### Change Log
| Date | Change |
|------|--------|
| 2026-03-27 | Story created by create-story workflow |
| 2026-03-28 | Code review: fixed backface culling (was VK_CULL_MODE_NONE with TODO), replaced static debug vars with member-based first-draw detection, updated outdated docstring, populated File List |

### File List
| File | Action | Description |
|------|--------|-------------|
| `assets/shaders/chunk.vert` | CREATE | Vertex pulling shader — SSBO read, bit unpack, corner reconstruction, waving, AO |
| `assets/shaders/chunk.frag` | CREATE | Placeholder fragment shader — face-normal coloring with AO darkening |
| `engine/include/voxel/renderer/Renderer.h` | MODIFY | ChunkPushConstants (chunkWorldPos replaces padding), renderChunks(), draw stat accessors |
| `engine/src/renderer/Renderer.cpp` | MODIFY | Shader paths → chunk.*, renderChunks() impl, backface culling, remove test triangle draw |
| `engine/src/renderer/VulkanContext.cpp` | MODIFY | Enable shaderDrawParameters via VkPhysicalDeviceVulkan11Features |
| `engine/include/voxel/world/ChunkManager.h` | MODIFY | (Out-of-scope) Add streamChunks(), setRenderDistance(), MAX_LOADS_PER_FRAME |
| `engine/src/world/ChunkManager.cpp` | MODIFY | (Out-of-scope) Implement streamChunks() for player-relative chunk loading/unloading |
| `game/src/GameApp.cpp` | MODIFY | Call renderChunks() in render loop, show draw stats in debug overlay |
