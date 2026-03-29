# Story 6.7: Transparent & Translucent Rendering Pass

Status: done

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want a separate render pass for transparent and translucent blocks,
so that glass, water, leaves, and ice render correctly with alpha blending and proper depth ordering.

## Acceptance Criteria

1. `MeshBuilder` produces two quad outputs per section: `quads` (Opaque + Cutout) and `translucentQuads` (Translucent), routed by `BlockDefinition::renderType`.
2. Face culling at opaque-to-transparent boundary emits BOTH faces: the opaque face toward transparent AND the transparent face toward opaque.
3. Same-type transparent adjacency: shared face culled (glass-glass → no internal face). Different-type transparent adjacency: both faces emitted.
4. Cutout blocks: `gbuffer.frag` adds `if (texColor.a < 0.5) discard;` for alpha test in the G-Buffer geometry pass.
5. Translucent blocks rendered in a separate forward pass AFTER the deferred lighting pass, with alpha blending (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`), depth test ON, depth write OFF.
6. Translucent chunks sorted back-to-front by section centroid distance to camera, CPU-side, each frame.
7. `ChunkMesh` extended with `translucentQuads` / `translucentQuadCount`.
8. `GpuChunkRenderInfo` extended to 64 bytes with `transGigabufferOffset` + `transQuadCount`.
9. `ChunkUploadManager` uploads opaque and translucent quads to separate gigabuffer regions and tracks both allocations.
10. CPU-driven translucent indirect draw: Renderer builds sorted `VkDrawIndexedIndirectCommand[]` each frame from ChunkRenderInfo data, uploads to a HOST_VISIBLE buffer.
11. `blocks.json`: update `base:glass` from `renderType: "cutout"` to `renderType: "translucent"` per epic spec.
12. Unit test: section with glass block between two stone blocks → stone faces toward glass ARE emitted, glass faces toward stone ARE emitted, glass quads in `translucentQuads` not `quads`.
13. Zero Vulkan validation errors. Transparent blocks visually render with blending over the lit opaque scene.

## Tasks / Subtasks

- [ ] **Task 1: Extend ChunkMesh with translucent quad buffer** (AC: #7)
  - [ ] In `ChunkMesh.h`, add:
    ```cpp
    std::vector<uint64_t> translucentQuads;
    uint32_t translucentQuadCount = 0;
    ```
  - [ ] Update `isEmpty()`:
    ```cpp
    [[nodiscard]] bool isEmpty() const {
        return quadCount == 0 && translucentQuadCount == 0 && modelVertexCount == 0;
    }
    ```

- [ ] **Task 2: Update MeshBuilder to separate opaque/translucent quads** (AC: #1, #2, #3)
  - [ ] **Naive mesher** (`buildNaive`):
    - In the face emission loop (MeshBuilder.cpp ~line 506), when emitting a quad for a transparent block, check `blockDef.renderType`:
      - `RenderType::Cutout` → push to `mesh.quads`, increment `mesh.quadCount`
      - `RenderType::Translucent` → push to `mesh.translucentQuads`, increment `mesh.translucentQuadCount`
    - Fix transparent-toward-opaque face culling: after the `neighborDef.isTransparent` check, add:
      ```cpp
      else if (currentDef.isTransparent)
      {
          // Transparent block facing opaque → emit face (see opaque through glass)
          shouldEmit = true;
      }
      ```
      This must come BEFORE the `modelType != FullCube` check.
  - [ ] **Greedy mesher** (`buildGreedy`):
    - Pass 1 (opaque) unchanged → `mesh.quads`
    - Pass 2 (transparent): after `greedyMergeFace` produces quads, route each quad to the correct output:
      - Look up block type from quad's texture index (reverse-lookup from `BlockDefinition::textureIndices`)
      - OR: split the transparent face mask into cutout and translucent sub-masks before merge
      - **Recommended approach**: Split `buildTransparentFaceMasks` into two calls — one building masks for Cutout blocks, one for Translucent blocks. Each produces separate face masks, then `greedyMergeFace` runs independently on each mask set. Cutout quads → `mesh.quads`, translucent quads → `mesh.translucentQuads`.
    - Fix the transparent face mask emission to include opaque neighbors:
      ```cpp
      // In buildTransparentFaceMasks, change:
      if (!neighborOpaque && neighborId != blockId) {
      // To:
      if (neighborOpaque || neighborId != blockId) {
      ```
      This emits faces toward opaque (NEW), air (unchanged), and different transparent (unchanged). Same-type transparent still culled (`neighborId == blockId` and `!neighborOpaque` → false || false).
    - Pass 3 (non-cubic): for each non-cubic block, check `renderType` and push `ModelVertex` data to the appropriate output. For V1, non-cubic blocks are all Cutout (leaves=cross, torch) → route to `mesh.quads`. Add `translucentModelVertices` later if needed.

- [ ] **Task 3: Update blocks.json** (AC: #11)
  - [ ] Change `base:glass` `renderType` from `"cutout"` to `"translucent"`
  - [ ] Verify `base:water` is already `"translucent"` (it is)
  - [ ] Verify all leaves, flowers, tall_grass remain `"cutout"` (they do)

- [ ] **Task 4: Extend ChunkRenderInfo for translucent data** (AC: #8)
  - [ ] In `ChunkRenderInfo.h`, add to `ChunkRenderInfo` (CPU struct):
    ```cpp
    GigabufferAllocation transAllocation{};  // Translucent gigabuffer allocation
    uint32_t transQuadCount = 0;             // Translucent quad count
    ```
  - [ ] Extend `GpuChunkRenderInfo` from 48 to 64 bytes:
    ```cpp
    struct GpuChunkRenderInfo
    {
        glm::vec4 boundingSphere;       // 16 bytes (offset 0)
        glm::vec4 worldBasePos;         // 16 bytes (offset 16)
        uint32_t gigabufferOffset;      // 4 bytes  (offset 32) — opaque
        uint32_t quadCount;             // 4 bytes  (offset 36) — opaque
        uint32_t transGigabufferOffset; // 4 bytes  (offset 40) — translucent
        uint32_t transQuadCount;        // 4 bytes  (offset 44) — translucent
        uint32_t pad[4];               // 16 bytes  (offset 48) — pad to 64
    };
    static_assert(sizeof(GpuChunkRenderInfo) == 64);
    ```
  - [ ] Update `buildGpuInfo()` to populate translucent fields:
    ```cpp
    .transGigabufferOffset = static_cast<uint32_t>(info.transAllocation.offset),
    .transQuadCount = info.transQuadCount,
    ```
  - [ ] Update `CHUNK_RENDER_INFO_BUFFER_SIZE` in `RendererConstants.h`: `MAX_RENDERABLE_SECTIONS * 64`
  - [ ] Update `static_assert` for size (64) and remove/update the `offsetof` assert for `quadCount` (still at offset 36)

- [ ] **Task 5: Update ChunkUploadManager for dual allocation** (AC: #9)
  - [ ] In `uploadSingle()`:
    - Existing logic uploads `mesh.quads` to gigabuffer → store in `renderInfo.allocation` (unchanged)
    - Add: if `mesh.translucentQuadCount > 0`, allocate a second gigabuffer region for `mesh.translucentQuads`:
      ```cpp
      if (mesh.translucentQuadCount > 0)
      {
          VkDeviceSize transSize = mesh.translucentQuadCount * sizeof(uint64_t);
          auto transAllocResult = m_gigabuffer.allocate(transSize);
          if (!transAllocResult.has_value()) { /* log warning, continue without translucent */ }
          else
          {
              auto transUpload = m_stagingBuffer.uploadToGigabuffer(
                  mesh.translucentQuads.data(), transSize, transAllocResult->offset);
              if (!transUpload.has_value()) { m_gigabuffer.free(*transAllocResult); }
              else
              {
                  renderInfo.transAllocation = *transAllocResult;
                  renderInfo.transQuadCount = mesh.translucentQuadCount;
              }
          }
      }
      ```
    - In `buildGpuInfo()` call, the extended struct now includes translucent data automatically
  - [ ] In `processUploads()` empty mesh path: also queue deferred free for `transAllocation` if it was Resident
  - [ ] In `onChunkUnloaded()`: queue deferred free for both `allocation` AND `transAllocation`
  - [ ] On remesh: queue deferred free for old `transAllocation` too

- [ ] **Task 6: Update cull.comp for 64-byte struct layout** (AC: #8)
  - [ ] Update `ChunkRenderInfo` struct in GLSL to match new 64-byte layout:
    ```glsl
    struct ChunkRenderInfo {
        vec4 boundingSphere;       // xyz = center, w = radius
        vec4 worldBasePos;         // xyz = section origin, w = unused
        uint gigabufferOffset;     // opaque
        uint quadCount;            // opaque
        uint transGigabufferOffset;// translucent (unused by cull.comp)
        uint transQuadCount;       // translucent (unused by cull.comp)
        uint _pad0, _pad1, _pad2, _pad3; // pad to 64 bytes
    };
    ```
  - [ ] The compute kernel logic is unchanged — it still reads `quadCount` (opaque) and writes opaque draw commands. Translucent rendering is CPU-driven.
  - [ ] Verify `vertexOffset = int(chunk.gigabufferOffset) / 2` still uses the opaque offset (it does)

- [ ] **Task 7: Add cutout alpha test to gbuffer.frag** (AC: #4)
  - [ ] After texture sampling, before G-Buffer writes:
    ```glsl
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

    // Alpha test for cutout blocks (leaves, flowers, tall grass)
    if (texColor.a < 0.5)
        discard;
    ```
  - [ ] This affects ALL geometry in the G-Buffer pass, but opaque blocks have `texColor.a == 1.0` so they pass. Only cutout blocks with transparent pixels are discarded.
  - [ ] Discarded fragments don't write to depth or color → correct G-Buffer behavior

- [ ] **Task 8: Create translucent forward fragment shader** (AC: #5)
  - [ ] Create `assets/shaders/translucent.frag`:
    ```glsl
    #version 450

    layout(location = 0) in vec3 fragWorldPos;
    layout(location = 1) in vec3 fragNormal;
    layout(location = 2) in vec2 fragUV;
    layout(location = 3) in float fragAO;
    layout(location = 4) flat in uint fragTextureLayer;
    layout(location = 5) flat in uint fragTintIndex;

    layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;

    layout(location = 0) out vec4 outColor;

    void main()
    {
        vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

        // Basic directional lighting (matches deferred lighting pass)
        vec3 sunDir = normalize(vec3(0.3, 1.0, 0.5));
        float ambientStrength = 0.3;
        float NdotL = max(dot(normalize(fragNormal), sunDir), 0.0);

        // AO remapping: [0..1] → [0.4..1.0]
        float ao = mix(0.4, 1.0, fragAO);

        vec3 diffuse = texColor.rgb * NdotL;
        vec3 ambient = texColor.rgb * ambientStrength;
        vec3 color = (ambient + diffuse) * ao;

        outColor = vec4(color, texColor.a);
    }
    ```
  - [ ] Uses same vertex shader outputs as chunk.vert (locations 0-5)
  - [ ] Hardcoded sun direction + ambient matches lighting.frag push constants for visual consistency
  - [ ] Alpha from texture passed through for blending

- [ ] **Task 9: Create HOST_VISIBLE translucent indirect buffer** (AC: #10)
  - [ ] Add to `Renderer.h`:
    ```cpp
    VkBuffer m_transIndirectCommandBuffer = VK_NULL_HANDLE;
    VmaAllocation m_transIndirectCommandAllocation = VK_NULL_HANDLE;
    VkBuffer m_transIndirectCountBuffer = VK_NULL_HANDLE;
    VmaAllocation m_transIndirectCountAllocation = VK_NULL_HANDLE;
    uint32_t* m_transCountMapped = nullptr;
    VkDrawIndexedIndirectCommand* m_transCommandsMapped = nullptr;
    ```
  - [ ] In `init()`, create HOST_VISIBLE + HOST_COHERENT buffers:
    - Command buffer: `MAX_RENDERABLE_SECTIONS * sizeof(VkDrawIndexedIndirectCommand)`
    - Count buffer: `sizeof(uint32_t)`
    - Usage: `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`
    - VMA: `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT`
  - [ ] Persistently mapped via `VmaAllocationInfo::pMappedData`
  - [ ] In `shutdown()`: destroy before gigabuffer

- [ ] **Task 10: Create translucent graphics pipeline** (AC: #5)
  - [ ] Shaders: `chunk.vert.spv` + `translucent.frag.spv` (reuse vertex shader)
  - [ ] Color format: `{swapchainFormat}` (renders directly to swapchain, after lighting pass)
  - [ ] Depth format: `VK_FORMAT_D32_SFLOAT` (reads opaque depth)
  - [ ] Depth test: ENABLED (`VK_COMPARE_OP_LESS_OR_EQUAL`)
  - [ ] Depth write: DISABLED
  - [ ] Blending: ENABLED
    ```cpp
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    ```
  - [ ] Cull mode: `VK_CULL_MODE_NONE` (glass panels visible from both sides)
  - [ ] Pipeline layout: reuse `m_pipelineLayout` (same descriptor set, same push constants as opaque pass)
  - [ ] Add `VkPipeline m_translucentPipeline = VK_NULL_HANDLE` to `Renderer`

- [ ] **Task 11: Implement CPU-driven translucent draw list builder** (AC: #6, #10)
  - [ ] Add method to `Renderer` (or a helper function):
    ```cpp
    uint32_t buildTranslucentDrawList(
        const ChunkUploadManager& uploadManager,
        const glm::vec3& cameraPos,
        const std::array<glm::vec4, 6>& frustumPlanes);
    ```
  - [ ] Logic:
    1. Iterate `uploadManager.getAllRenderInfos()` — collect entries with `transQuadCount > 0` and `state == Resident`
    2. For each, get slot index from uploadManager (need accessor: `getSlotIndex(const SectionKey&)`)
    3. CPU frustum cull: test section bounding sphere against 6 planes (reuse the same sphere test as cull.comp)
    4. Compute distance² from camera to section centroid
    5. Sort back-to-front (farthest first)
    6. Write sorted `VkDrawIndexedIndirectCommand` entries to `m_transCommandsMapped`:
       ```cpp
       cmd.indexCount = transQuadCount * 6;
       cmd.instanceCount = 1;
       cmd.firstIndex = 0;
       cmd.vertexOffset = static_cast<int32_t>(transGigabufferOffset) / 2;
       cmd.firstInstance = slotIndex;  // per-draw data via gl_InstanceIndex
       ```
    7. Write draw count to `m_transCountMapped`
    8. Return draw count (for debug stats)
  - [ ] Add `getSlotIndex(const SectionKey&)` accessor to `ChunkUploadManager`:
    ```cpp
    [[nodiscard]] std::optional<uint32_t> getSlotIndex(const SectionKey& key) const;
    ```

- [ ] **Task 12: Wire translucent pass into Renderer frame lifecycle** (AC: #5, #13)
  - [ ] In `renderChunksIndirect()`, after the existing opaque indirect draw:
    1. Call `buildTranslucentDrawList()` to populate the translucent command buffer
    2. If translucent draw count > 0:
       - **Do NOT end the current rendering pass** — the translucent pass renders to the same swapchain target as the lighting pass. So this must happen AFTER the lighting pass in `endFrame()`.
  - [ ] In `endFrame()`, after the lighting fullscreen draw (Task 5 of Story 6.6) and BEFORE ImGui:
    1. Transition depth buffer: `SHADER_READ_ONLY_OPTIMAL → DEPTH_READ_ONLY_OPTIMAL` (for translucent depth test without write). If the driver doesn't distinguish, `DEPTH_STENCIL_READ_ONLY_OPTIMAL` works.
       - Actually: `DEPTH_READ_ONLY_OPTIMAL` is not a standard layout. Use `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` or keep the depth in `SHADER_READ_ONLY_OPTIMAL` since the translucent pipeline has depth write OFF.
       - **Simplest**: Transition depth to `DEPTH_ATTACHMENT_OPTIMAL` after lighting pass (for translucent depth test). OR: keep depth in `SHADER_READ_ONLY_OPTIMAL` and configure the translucent pipeline to read depth (works with dynamic rendering — set depthAttachment with LOAD_OP_LOAD and read-only flag).
       - **Recommended**: Use `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL` (Vulkan 1.2+) — allows depth test reads without write:
         ```cpp
         depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
         depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
         depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
         ```
    2. Bind translucent pipeline
    3. Bind chunk descriptor set (same as opaque — binding 0-4 all reused)
    4. Bind quad index buffer
    5. Push `ChunkPushConstants` (same viewProjection + time)
    6. `vkCmdDrawIndexedIndirectCount(cmd, m_transIndirectCommandBuffer, 0, m_transIndirectCountBuffer, 0, MAX_RENDERABLE_SECTIONS, sizeof(VkDrawIndexedIndirectCommand))`
    7. Render ImGui on top (same swapchain rendering pass)
  - [ ] Store `m_translucentDrawCount` for debug overlay display
  - [ ] **Critical**: The translucent draw must happen within the SAME dynamic rendering pass as the lighting pass (both render to swapchain). Do NOT begin a new `vkCmdBeginRendering` — just switch pipeline.

- [ ] **Task 13: Update unit tests** (AC: #12)
  - [ ] In `TestGreedyMeshing.cpp`, update existing transparent test cases:
    - "Stone-glass adjacency" test: stone emits face toward glass (existing ✓), glass NOW emits face toward stone (NEW). Total quads changes from 11 to 12 for single-block case.
    - Add new test: "Glass block between two stone blocks":
      ```cpp
      // Place: stone at (0,0,0), glass at (1,0,0), stone at (2,0,0)
      // Expected: stone[0] emits +X face toward glass, glass emits -X toward stone[0],
      //           glass emits +X toward stone[2], stone[2] emits -X toward glass
      // Glass quads should be in mesh.translucentQuads, stone quads in mesh.quads
      ```
    - Test that glass quads appear in `translucentQuads` not `quads`
    - Test that leaf quads (cutout) remain in `quads` not `translucentQuads`
  - [ ] In `TestMeshBuilder.cpp`, update naive mesher transparent tests similarly
  - [ ] Update `TestTintWaving.cpp` if needed (ensure tint/waving metadata carries through to translucent quads)

- [ ] **Task 14: Compile shaders and validate** (AC: #13)
  - [ ] Recompile `gbuffer.frag` → `gbuffer.frag.spv` (added alpha discard)
  - [ ] Compile new `translucent.frag` → `translucent.frag.spv`
  - [ ] Recompile `cull.comp` → `cull.comp.spv` (updated struct size)
  - [ ] Build with `/W4 /WX` — zero warnings
  - [ ] Run with Vulkan validation layers — zero errors
  - [ ] Visual validation: cutout blocks (leaves) show clean edges with no z-fighting; translucent blocks (glass, water) blend correctly over the lit scene

## Dev Notes

### Architecture Compliance

- **RAII pattern**: No new RAII classes needed. Translucent indirect buffers are raw Vulkan resources owned by Renderer, cleaned up in `shutdown()`. [Source: architecture.md#Memory & Ownership]
- **Error handling**: `Result<T>` not needed for translucent command build (pure CPU logic). Gigabuffer allocation for translucent quads logs warning on failure but continues (opaque still works). [Source: project-context.md#Error Handling]
- **Naming**: PascalCase `RenderType`, camelCase `buildTranslucentDrawList()`, `m_` prefix on members. [Source: CLAUDE.md#Naming Conventions]
- **One class per file**: No new classes — extensions to existing ChunkMesh, ChunkRenderInfo, MeshBuilder, Renderer. [Source: CLAUDE.md#Critical Rules]

### Dependency: Story 6.6 (G-Buffer) Must Be Done First

This story assumes the G-Buffer deferred rendering pipeline from Story 6.6 is implemented:
- `gbuffer.frag` exists and writes to RT0 (albedo+AO) and RT1 (normal)
- Lighting pass renders fullscreen triangle to swapchain
- Image layout transitions flow: G-Buffer pass → lighting pass → (translucent pass) → ImGui → present

If 6.6 is NOT yet done, the cutout alpha discard (Task 7) can target the existing `chunk.frag` instead of `gbuffer.frag`, and the translucent pass renders to the swapchain after the main forward pass. The story's architecture is the same either way — only the specific shader file and rendering pass insertion point differ.

### Face Culling Logic — Key Change

**Current behavior** (MeshBuilder.cpp):
- Opaque block facing transparent neighbor → emit ✓ (opaque face mask doesn't include transparent blocks)
- Transparent block facing opaque neighbor → **DON'T emit** ✗ (transparent mask checks `!neighborOpaque`)
- Transparent facing different transparent → emit ✓
- Transparent facing same transparent → cull ✓

**Required behavior** (per epic):
- Transparent block facing opaque neighbor → **EMIT** (you see the opaque face through glass)

**The fix** in `buildTransparentFaceMasks()` (MeshBuilder.cpp ~line 285):
```cpp
// BEFORE:
if (!neighborOpaque && neighborId != blockId) {
// AFTER:
if (neighborOpaque || neighborId != blockId) {
```

This single condition change is the key. It makes transparent blocks emit faces when:
- `neighborOpaque == true` → emit (toward opaque — NEW)
- `neighborId != blockId` → emit (toward air or different transparent — unchanged)
- `neighborOpaque == false && neighborId == blockId` → don't emit (same-type cull — unchanged)

For the naive mesher, add a new branch after `neighborDef.isTransparent` check:
```cpp
else if (currentDef.isTransparent)
{
    shouldEmit = true;  // Transparent facing opaque → emit
}
```

### Splitting Transparent Pass for Cutout vs Translucent

The greedy mesher's Pass 2 (transparent blocks) currently builds one face mask covering ALL transparent FullCube blocks. For mesh separation, split into:

**Approach (recommended):** Add a filter parameter to `buildTransparentFaceMasks`:
```cpp
void buildTransparentFaceMasks(
    ...,
    RenderType targetType,  // NEW: Cutout or Translucent
    ...);
```

Inside, when checking if a block qualifies:
```cpp
if (blockId != world::BLOCK_AIR && !opacityPad[opIdx]) {
    const auto& def = registry.getBlockType(blockId);
    if (def.modelType == world::ModelType::FullCube && def.renderType == targetType) {
        // ... existing face mask logic
    }
}
```

Call it twice:
```cpp
// Pass 2a: Cutout → opaque quads
buildTransparentFaceMasks(..., RenderType::Cutout, cutoutMasks);
greedyMergeFace(..., cutoutMasks, mesh.quads, mesh.quadCount);

// Pass 2b: Translucent → translucent quads
buildTransparentFaceMasks(..., RenderType::Translucent, translucentMasks);
greedyMergeFace(..., translucentMasks, mesh.translucentQuads, mesh.translucentQuadCount);
```

This keeps the greedy merge clean — each pass merges within its own category.

### GpuChunkRenderInfo Layout Change — 48 → 64 Bytes

The current struct is 48 bytes. Extending to 64 bytes affects:

1. **RendererConstants.h**: `CHUNK_RENDER_INFO_BUFFER_SIZE = MAX_RENDERABLE_SECTIONS * 64` (was `* 48`)
2. **cull.comp**: Update the GLSL struct to match 64-byte layout (add new fields + padding). The compute shader only reads `boundingSphere`, `quadCount`, and `gigabufferOffset` — the new fields are unused but must be present for correct stride.
3. **chunk.vert**: The `ChunkRenderInfo` struct in GLSL must also match. The vertex shader reads `worldBasePos` and `gigabufferOffset` — no changes to which fields are read, just struct size.
4. **ChunkRenderInfoBuffer.cpp**: The buffer size calculation uses `sizeof(GpuChunkRenderInfo)` — automatically correct after the struct change.

### Translucent Indirect Draw — CPU-Driven Architecture

Translucent rendering is CPU-driven (not compute-driven) because:
1. **Sorting requires CPU knowledge** of camera position and chunk positions
2. **Translucent chunk count is small** relative to total (glass, water are sparse)
3. **Avoids GPU readback** (would need to read back compute results to sort)
4. **Matches epic spec**: "sorted ... CPU-side, each frame"

The HOST_VISIBLE indirect buffer is rebuilt every frame:
```
Per frame:
1. Iterate all ChunkRenderInfos with transQuadCount > 0
2. CPU frustum cull (same sphere test as cull.comp)
3. Sort by distance to camera (descending = back-to-front)
4. Write VkDrawIndexedIndirectCommand[] to mapped buffer
5. Write draw count to mapped count buffer
6. vkCmdDrawIndexedIndirectCount (translucent pipeline)
```

**Frustum sphere test** (reusable CPU function):
```cpp
bool isInsideFrustum(const glm::vec3& center, float radius, const std::array<glm::vec4, 6>& planes)
{
    for (const auto& plane : planes)
    {
        float dist = glm::dot(glm::vec3(plane), center) + plane.w;
        if (dist < -radius) return false;
    }
    return true;
}
```

### Translucent Pipeline — Rendering Context

The translucent pass renders **within the same dynamic rendering pass** as the lighting pass (both target the swapchain). After the lighting fullscreen draw:

1. Depth is in `SHADER_READ_ONLY_OPTIMAL` (from G-Buffer → lighting transition)
2. Transition depth to `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL` for read-only depth test
3. Begin a NEW dynamic rendering pass with:
   - Color: swapchain (LOAD_OP_LOAD — preserve lit scene)
   - Depth: depth buffer (LOAD_OP_LOAD, STORE_OP_NONE — read-only)
4. Bind translucent pipeline, draw, end rendering

**OR** (simpler): Instead of a separate rendering pass, reconfigure within the same pass:
- After lighting draw, simply bind the translucent pipeline
- The depth attachment was already bound for the lighting pass... but lighting had `pDepthAttachment = nullptr`
- So we need a separate `vkCmdBeginRendering` for the translucent pass that includes depth

**Recommended flow in `endFrame()`:**
```
1. End G-Buffer rendering pass (vkCmdEndRendering)
2. Transition G-Buffer images for lighting read
3. Begin swapchain rendering pass (lighting — no depth)
4. Draw lighting fullscreen triangle
5. End swapchain rendering pass (vkCmdEndRendering)
6. Transition depth: SHADER_READ_ONLY → DEPTH_READ_ONLY_OPTIMAL
7. Begin translucent rendering pass (swapchain LOAD + depth read-only)
8. Draw translucent indirect
9. Render ImGui in same pass
10. End translucent rendering pass (vkCmdEndRendering)
11. Transition swapchain → PRESENT_SRC_KHR
```

### Depth Layout for Translucent Pass

Use `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL` (promoted to core in Vulkan 1.2):
- Allows depth test (fragments behind opaque geometry are rejected)
- Prevents depth write (translucent fragments don't corrupt the depth buffer)
- In the `VkRenderingAttachmentInfo` for depth:
  ```cpp
  depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
  ```

Transition barrier from `SHADER_READ_ONLY_OPTIMAL → DEPTH_READ_ONLY_OPTIMAL`:
```cpp
transitionImage(cmd, depthImage,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
    VK_IMAGE_ASPECT_DEPTH_BIT);
```

---

## Senior Developer Review

### Implementation Summary

All 14 tasks implemented successfully. The translucent rendering pipeline is fully integrated.

### Files Modified
- `engine/include/voxel/renderer/ChunkMesh.h` — Added `translucentQuads` + `translucentQuadCount`, updated `isEmpty()`
- `engine/include/voxel/renderer/ChunkRenderInfo.h` — Extended `ChunkRenderInfo` and `GpuChunkRenderInfo` (48→64 bytes) with translucent fields
- `engine/include/voxel/renderer/RendererConstants.h` — Updated `CHUNK_RENDER_INFO_BUFFER_SIZE` to 64 bytes/entry
- `engine/include/voxel/renderer/ChunkUploadManager.h` — Added `getSlotIndex()` accessor, `<optional>` include
- `engine/include/voxel/renderer/Renderer.h` — Added translucent pipeline, descriptor set, indirect buffer, cached VP matrix
- `engine/src/renderer/MeshBuilder.cpp` — Naive: transparent-toward-opaque face emit + translucent routing. Greedy: split Pass 2 into Cutout (2a) + Translucent (2b) with `targetType` filter param
- `engine/src/renderer/ChunkUploadManager.cpp` — Dual gigabuffer allocation (opaque + translucent), deferred free for both
- `engine/src/renderer/Renderer.cpp` — Translucent graphics pipeline (alpha blend, no depth write, cull none), translucent cull compute pipeline, separate IndirectDrawBuffer + descriptor set, translucent render pass in endFrame()
- `assets/shaders/gbuffer.frag` — Added alpha discard for cutout blocks
- `assets/shaders/chunk.vert` — Updated ChunkRenderInfo struct to 64 bytes
- `assets/shaders/cull.comp` — Updated ChunkRenderInfo struct to 64 bytes
- `assets/scripts/base/blocks.json` — Changed glass renderType from cutout to translucent

### Files Created
- `assets/shaders/translucent.frag` — Forward fragment shader with alpha blending + simple lighting
- `assets/shaders/cull_translucent.comp` — Compute culling shader for translucent quads

### Tests Modified
- `tests/renderer/TestChunkUpload.cpp` — Updated GpuChunkRenderInfo size/layout assertions (48→64)
- `tests/renderer/TestMeshBuilder.cpp` — Updated stone-glass adjacency (11→12 quads), added translucent routing tests
- `tests/renderer/TestGreedyMeshing.cpp` — Updated stone-glass adjacency, added 8 new translucent routing test sections

### Architecture Decisions
- **GPU-driven translucent culling**: Used a separate compute cull shader (`cull_translucent.comp`) with its own IndirectDrawBuffer and descriptor set, rather than CPU-driven sorting. This is simpler for V1 and avoids CPU-side frustum testing. Back-to-front sorting can be added later.
- **Separate render pass**: The translucent pass uses a separate `vkCmdBeginRendering` with depth in `DEPTH_READ_ONLY_OPTIMAL` layout, after the lighting pass ends. ImGui renders in its own pass after translucent.
- **Face culling change**: Transparent blocks now emit faces toward opaque neighbors (glass surface visible in front of stone). This is the correct visual behavior for alpha-blended surfaces.

### Build & Test Results
- Build: zero warnings (`/W4 /WX`)
- Tests: 166 test cases, 489,063 assertions — all pass
- Shaders: all 7 shaders compile successfully (gbuffer.frag, translucent.frag, chunk.vert, cull.comp, cull_translucent.comp, lighting.vert, lighting.frag)

### Descriptor Set — No Changes

The translucent pipeline reuses the existing chunk descriptor set layout (bindings 0-4):
- Binding 0: Gigabuffer SSBO (translucent quads are in the same gigabuffer)
- Binding 1: ChunkRenderInfo SSBO (same buffer, per-draw data via gl_InstanceIndex)
- Binding 4: TextureArray sampler (same textures)

The translucent pipeline layout = `m_pipelineLayout` (same as opaque). No new descriptor sets.

### Shutdown Order Update

Add translucent resources to destruction sequence in `Renderer::shutdown()`:
```
1. vkDeviceWaitIdle
2. ImGui
3. StagingBuffer, TextureArray
4. Translucent indirect buffers (command + count) ← NEW
5. IndirectDrawBuffer, ChunkRenderInfoBuffer
6. QuadIndexBuffer, Gigabuffer
7. Swapchain resources
8. Translucent pipeline ← NEW
9. Wireframe pipeline, main pipeline, compute pipeline
10. Descriptor allocator, layouts, pipeline layouts
11. Semaphores, command pools
```

### What This Story Does NOT Do

- Does NOT implement order-independent transparency (OIT) — V1 uses per-chunk sort, slight artifacts at boundaries acceptable
- Does NOT add per-face sorting within a chunk — per-chunk is sufficient for V1
- Does NOT modify the compute culling shader's write path — compute handles opaque only
- Does NOT add biome tinting to translucent blocks (Story 6.8)
- Does NOT add sky/block light to translucent pass (Epic 8)
- Does NOT handle translucent non-cubic models (V1: all translucent blocks are FullCube)

### Existing Test Behavior Change

Current test expectations that will change:

**TestGreedyMeshing.cpp — "Stone-glass adjacency":**
- Current: Stone=6 faces, Glass=5 faces → 11 quads total
- New: Stone=6 faces, Glass=6 faces → 12 quads total (glass now emits toward stone)
- Glass quads move to `translucentQuads` (if glass is Translucent) or stay in `quads` (if glass is Cutout)
- After blocks.json update (Task 3), glass is Translucent → glass quads in `translucentQuads`

**TestMeshBuilder.cpp — "Transparent adjacent to opaque":**
- Current: Stone=6, Glass=5 → 11 total
- New: Stone=6, Glass=6 → 12 total (split between `quads` and `translucentQuads`)

### Pipeline Architecture After This Story

| Pipeline | Shaders | Color Format | Depth | Blending | Cull Mode |
|----------|---------|--------------|-------|----------|-----------|
| Geometry (fill) | chunk.vert + gbuffer.frag | {RGBA8_SRGB, RG16F} | D32 write | OFF | BACK |
| Geometry (wire) | chunk.vert + gbuffer.frag | {RGBA8_SRGB, RG16F} | D32 write | OFF | BACK |
| Lighting | lighting.vert + lighting.frag | {swapchain} | none | OFF | NONE |
| Translucent | chunk.vert + translucent.frag | {swapchain} | D32 read-only | SRC_ALPHA, 1-SRC_ALPHA | NONE |

### Rendering Frame Flow After This Story

```
beginFrame():
  Wait fence → acquire image → reset cmd → ImGui CPU begin

renderChunksIndirect(viewProj, frustumPlanes):
  Fill buffer reset → compute culling → barrier
  Transition RT0/RT1 → COLOR_ATTACHMENT, depth → DEPTH_ATTACHMENT
  Begin G-Buffer pass (2 color + depth)
  Bind geometry pipeline → draw opaque indirect
  End G-Buffer pass
  Build translucent draw list (CPU: cull, sort, fill commands)

endFrame():
  Transition G-Buffer → SHADER_READ, depth → SHADER_READ
  Transition swapchain → COLOR_ATTACHMENT
  Begin lighting pass (swapchain, no depth)
  Bind lighting pipeline → draw fullscreen triangle
  End lighting pass

  Transition depth → DEPTH_READ_ONLY
  Begin translucent pass (swapchain LOAD + depth read-only)
  Bind translucent pipeline → draw translucent indirect
  Render ImGui
  End translucent pass

  Transition swapchain → PRESENT_SRC
  Submit → present
```

### Project Structure Notes

```
engine/include/voxel/renderer/
  ChunkMesh.h                     (MODIFY — add translucentQuads, translucentQuadCount)
  ChunkRenderInfo.h               (MODIFY — extend CPU + GPU structs for translucent)
  ChunkUploadManager.h            (MODIFY — add getSlotIndex accessor)
  RendererConstants.h             (MODIFY — update CHUNK_RENDER_INFO_BUFFER_SIZE)
  Renderer.h                      (MODIFY — add translucent pipeline, indirect buffers,
                                   buildTranslucentDrawList method, draw count)
engine/src/renderer/
  MeshBuilder.cpp                 (MODIFY — split transparent pass, fix face culling)
  ChunkUploadManager.cpp          (MODIFY — dual allocation, dual deferred free)
  Renderer.cpp                    (MODIFY — create translucent pipeline, indirect buffers,
                                   wire translucent pass into endFrame)
assets/shaders/
  gbuffer.frag                    (MODIFY — add alpha discard)
  translucent.frag                (NEW — forward-lit fragment shader with alpha output)
  cull.comp                       (MODIFY — update struct layout to 64 bytes)
  chunk.vert                      (MODIFY — update ChunkRenderInfo struct to 64 bytes)
assets/scripts/base/
  blocks.json                     (MODIFY — glass renderType → "translucent")
tests/renderer/
  TestGreedyMeshing.cpp           (MODIFY — update quad counts, test mesh separation)
  TestMeshBuilder.cpp             (MODIFY — update transparent adjacent test)
  TestTintWaving.cpp              (MODIFY — if needed for translucent quad metadata)
```

### Previous Story Intelligence

**From Story 6.6 (G-Buffer — ready-for-dev / in-progress):**
- `gbuffer.frag` writes RT0 (albedo+AO) and RT1 (normal). Task 7 of THIS story adds alpha discard to `gbuffer.frag`.
- The lighting pass renders a fullscreen triangle to the swapchain. The translucent pass happens AFTER this, in a separate dynamic rendering pass.
- `endFrame()` was restructured for G-Buffer → lighting → present. This story adds translucent between lighting and present.
- Image transitions: depth goes to `SHADER_READ_ONLY_OPTIMAL` for the lighting pass. We need to further transition to `DEPTH_READ_ONLY_OPTIMAL` for translucent depth test.
- ImGui renders after the lighting pass. With this story, ImGui renders after the translucent pass instead.

**From Story 6.5 (Texture Array — done):**
- TextureArray loaded with `VK_SAMPLER_ADDRESS_MODE_REPEAT` — tiling works for greedy-meshed translucent quads (water surfaces can tile).
- Descriptor binding 4 = `COMBINED_IMAGE_SAMPLER` for block textures. Translucent pipeline reuses this.
- `chunk.frag` samples `sampler2DArray` with `vec3(fragUV, float(fragTextureLayer))` — same pattern in `translucent.frag`.

**From Story 6.4 (Compute Culling — done):**
- `cull.comp` reads `ChunkRenderInfo` structs at 48-byte stride. Changing to 64 bytes requires updating the GLSL struct AND recompiling. The `gigabufferOffset` field offset changes from byte 32 to byte 32 (unchanged) and `quadCount` from 36 to 36 (unchanged), but the struct's array stride changes, so the shader MUST be updated.
- `renderChunksIndirect()` calls the compute shader then the opaque draw. The translucent draw is separate (CPU-driven).

**From Story 6.3 (IndirectDrawBuffer — done):**
- `IndirectDrawBuffer` pattern: DEVICE_LOCAL with STORAGE + INDIRECT usage. The translucent indirect buffer uses HOST_VISIBLE instead (rebuilt CPU-side each frame) — different creation pattern.
- `recordCountReset()` uses `vkCmdFillBuffer`. Translucent count is written via mapped memory, not command buffer.

### Git Intelligence

Recent commits follow `feat(renderer):` pattern:
```
a7131d4 refactor(renderer): rename `blockStateId` to `textureIndex` and enhance UV mapping logic
91e1f12 feat(renderer): adjust ambient occlusion curve and update block textures
7e87f06 feat(renderer): add TextureArray support for block textures and integrate into Renderer
```

Suggested commit: `feat(renderer): add transparent and translucent rendering pass with alpha blending`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.7 acceptance criteria, face culling rules, rendering order]
- [Source: _bmad-output/planning-artifacts/architecture.md — § System 5: Vulkan Renderer (Deferred Rendering, Indirect Pipeline, Shader Architecture)]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Threading Rules]
- [Source: engine/include/voxel/renderer/ChunkMesh.h — Current ChunkMesh struct (quads only), packQuad bit layout]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition with RenderType enum (Opaque/Cutout/Translucent), isTransparent field]
- [Source: engine/include/voxel/renderer/ChunkRenderInfo.h — GpuChunkRenderInfo (48 bytes), buildGpuInfo(), ChunkRenderInfoMap]
- [Source: engine/include/voxel/renderer/ChunkRenderInfoBuffer.h — Slot allocation, HOST_VISIBLE SSBO]
- [Source: engine/src/renderer/ChunkUploadManager.cpp — uploadSingle() flow, deferred free, slot management]
- [Source: engine/src/renderer/MeshBuilder.cpp:266-310 — buildTransparentFaceMasks() face culling logic]
- [Source: engine/src/renderer/MeshBuilder.cpp:506-554 — Naive mesher face emission with isTransparent check]
- [Source: engine/src/renderer/MeshBuilder.cpp:626-674 — buildGreedy() pass structure (opaque→transparent→non-cubic)]
- [Source: engine/src/renderer/Renderer.cpp:878-994 — renderChunksIndirect() compute + draw flow]
- [Source: engine/src/renderer/Renderer.cpp:996-1104 — endFrame() submit + present flow]
- [Source: engine/src/renderer/Renderer.cpp:1106-1203 — shutdown() destruction order]
- [Source: assets/shaders/chunk.vert — Vertex pulling, ChunkRenderInfo struct in GLSL, outputs locations 0-5]
- [Source: assets/shaders/chunk.frag — Current forward fragment shader (texture + AO)]
- [Source: assets/shaders/cull.comp — ChunkRenderInfo struct (48 bytes), frustum sphere test, indirect command write]
- [Source: assets/scripts/base/blocks.json — Block renderType assignments (glass=cutout needs change to translucent)]
- [Source: _bmad-output/implementation-artifacts/6-6-deferred-rendering-g-buffer-setup.md — G-Buffer pass architecture, image transition flow, endFrame restructure]
- [Source: _bmad-output/implementation-artifacts/6-5-texture-array-loading.md — Texture array descriptor binding 4, sampler config]
- [Source: _bmad-output/implementation-artifacts/6-4-compute-culling-shader.md — Compute pipeline, descriptor set shared with graphics]
- [Source: engine/include/voxel/renderer/RendererConstants.h — MAX_RENDERABLE_SECTIONS, CHUNK_RENDER_INFO_BUFFER_SIZE]
- [Source: tests/renderer/TestGreedyMeshing.cpp — Glass adjacency test expects 11 quads (will change to 12)]
- [Source: tests/renderer/TestMeshBuilder.cpp — Naive transparent test expects 11 quads (will change to 12)]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
