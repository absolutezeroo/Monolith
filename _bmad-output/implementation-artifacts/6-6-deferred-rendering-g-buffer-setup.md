# Story 6.6: Deferred Rendering G-Buffer Setup

Status: done

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want a G-Buffer for deferred rendering,
so that lighting can be computed in screen space across all geometry types, decoupling lighting from geometry complexity.

## Acceptance Criteria

1. G-Buffer created at swapchain resolution with 3 attachments: RT0 (`VK_FORMAT_R8G8B8A8_SRGB` — albedo.rgb + AO.a), RT1 (`VK_FORMAT_R16G16_SFLOAT` — normal.xy octahedral), Depth (`VK_FORMAT_D32_SFLOAT` — shared with existing depth buffer).
2. G-Buffer images recreated on swapchain resize (alongside existing depth buffer).
3. Geometry pass writes to G-Buffer via dynamic rendering with 2 color attachments + depth.
4. Fullscreen lighting pass reads G-Buffer as sampled textures, computes basic directional sun lighting + ambient + AO, outputs composited result to swapchain.
5. ImGui renders on top of the composite to swapchain (after lighting pass).
6. External API unchanged: `beginFrame()` / `renderChunks()` / `endFrame()` contract preserved.
7. Zero Vulkan validation errors. Visual output at least as good as the current forward pass.

## Tasks / Subtasks

- [x] **Task 1: Add G-Buffer constants to RendererConstants.h** (AC: #1)
  - [x] Add `GBUFFER_RT0_FORMAT = VK_FORMAT_R8G8B8A8_SRGB` (albedo + AO)
  - [x] Add `GBUFFER_RT1_FORMAT = VK_FORMAT_R16G16_SFLOAT` (octahedral normal)
  - [x] Add `GBUFFER_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT` (already used, just name it)

- [x] **Task 2: Create GBuffer class** (AC: #1, #2)
  - [x]New files: `engine/include/voxel/renderer/GBuffer.h` / `engine/src/renderer/GBuffer.cpp`
  - [x]Follow RAII factory pattern from QuadIndexBuffer / Gigabuffer: private ctor, `static create()`, deleted copy/move
  - [x]Factory signature: `static Result<unique_ptr<GBuffer>> create(VulkanContext& ctx, VkExtent2D extent)`
  - [x]Members:
    - RT0: `VkImage m_albedoImage`, `VmaAllocation m_albedoAllocation`, `VkImageView m_albedoView`
    - RT1: `VkImage m_normalImage`, `VmaAllocation m_normalAllocation`, `VkImageView m_normalView`
    - `VkSampler m_sampler` (shared nearest sampler for both G-Buffer reads)
    - `VmaAllocator m_allocator`, `VkDevice m_device` (for RAII cleanup)
    - `VkExtent2D m_extent`
  - [x]Image creation:
    - Usage: `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`
    - VMA: `VMA_MEMORY_USAGE_AUTO`, `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`
    - Tiling: `VK_IMAGE_TILING_OPTIMAL`, 1 mip, 1 layer, 1 sample
  - [x]Image views: standard 2D color views matching image format
  - [x]Sampler: `VK_FILTER_NEAREST` mag/min (we sample at exact pixel), `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`
  - [x]Accessors: `getAlbedoView()`, `getNormalView()`, `getAlbedoImage()`, `getNormalImage()`, `getSampler()`, `getExtent()`
  - [x]RAII destructor: destroy sampler, views, images (via vmaDestroyImage)
  - [x]**Does NOT own depth** — depth is shared with SwapchainResources in Renderer

- [x] **Task 3: Add SAMPLED bit to depth image** (AC: #1, #4)
  - [x]In `Renderer::createSwapchainResources()` (Renderer.cpp:308), change:
    ```cpp
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ```
    to:
    ```cpp
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ```
  - [x]This allows the lighting pass to sample depth for world position reconstruction. No other changes needed — the existing depth image view already has `VK_IMAGE_ASPECT_DEPTH_BIT`.

- [x] **Task 4: Write G-Buffer fragment shader (`gbuffer.frag`)** (AC: #3)
  - [x]Create `assets/shaders/gbuffer.frag`
  - [x]Same inputs as current `chunk.frag` (locations 0-5):
    ```glsl
    layout(location = 0) in vec3 fragWorldPos;
    layout(location = 1) in vec3 fragNormal;
    layout(location = 2) in vec2 fragUV;
    layout(location = 3) in float fragAO;
    layout(location = 4) flat in uint fragBlockStateId;
    layout(location = 5) flat in uint fragTintIndex;
    ```
  - [x]Two outputs (multiple render targets):
    ```glsl
    layout(location = 0) out vec4 outAlbedoAO;   // RT0: albedo.rgb + AO.a
    layout(location = 1) out vec2 outNormalOct;   // RT1: octahedral encoded normal.xy
    ```
  - [x]Albedo: use same face-normal-based coloring as current chunk.frag (top=green, bottom=brown, sides=gray). Story 6.5 will replace this with real texture sampling.
  - [x]AO: pack into alpha channel `outAlbedoAO.a = fragAO`
  - [x]Normal encoding — octahedral mapping (compact 3D→2D):
    ```glsl
    vec2 octahedralEncode(vec3 n) {
        n /= (abs(n.x) + abs(n.y) + abs(n.z));
        if (n.z < 0.0) {
            n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        }
        return n.xy * 0.5 + 0.5; // Remap [-1,1] → [0,1]
    }
    ```
  - [x]Output: `outNormalOct = octahedralEncode(normalize(fragNormal));`

- [x] **Task 5: Write lighting shaders (`lighting.vert` + `lighting.frag`)** (AC: #4)
  - [x]Create `assets/shaders/lighting.vert` — fullscreen triangle:
    ```glsl
    #version 450
    layout(location = 0) out vec2 fragUV;

    void main() {
        // Fullscreen triangle: 3 vertices, no VBO
        fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
        gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
    }
    ```
  - [x]Create `assets/shaders/lighting.frag`:
    ```glsl
    #version 450
    layout(location = 0) in vec2 fragUV;
    layout(location = 0) out vec4 outColor;

    layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedoAO;
    layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
    layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;

    layout(push_constant) uniform LightingPushConstants {
        vec3 sunDirection;   // 12 bytes
        float ambientStrength; // 4 bytes
    } pc;
    ```
  - [x]Octahedral decode:
    ```glsl
    vec3 octahedralDecode(vec2 e) {
        e = e * 2.0 - 1.0; // Remap [0,1] → [-1,1]
        vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
        if (n.z < 0.0) {
            n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        }
        return normalize(n);
    }
    ```
  - [x]Lighting computation:
    ```glsl
    void main() {
        vec4 albedoAO = texture(gbufferAlbedoAO, fragUV);
        vec2 normalEnc = texture(gbufferNormal, fragUV).rg;

        // Early-out for sky pixels (depth == 1.0)
        float depth = texture(gbufferDepth, fragUV).r;
        if (depth >= 1.0) {
            outColor = vec4(0.1, 0.1, 0.1, 1.0); // sky color (matches clear color)
            return;
        }

        vec3 albedo = albedoAO.rgb;
        float ao = albedoAO.a;
        vec3 normal = octahedralDecode(normalEnc);

        // Directional sun lighting
        float NdotL = max(dot(normal, normalize(pc.sunDirection)), 0.0);
        vec3 diffuse = albedo * NdotL;

        // Ambient
        vec3 ambient = albedo * pc.ambientStrength;

        // Combine: (ambient + diffuse) * AO
        vec3 color = (ambient + diffuse) * ao;

        outColor = vec4(color, 1.0);
    }
    ```
  - [x]Sun direction: hardcoded from C++ side as `normalize(vec3(0.3, 1.0, 0.5))` (high sun, slight angle). `ambientStrength = 0.3`.

- [x] **Task 6: Extend PipelineConfig for multiple color attachments** (AC: #3, #4)
  - [x]In `Renderer.h`, add to `PipelineConfig`:
    ```cpp
    std::vector<VkFormat> colorAttachmentFormats;  // replaces single swapchain format
    VkFormat depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    bool enableBlending = false;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    ```
  - [x]In `Renderer::buildPipeline()` (Renderer.cpp:409):
    - Replace hardcoded `swapFmt` with `config.colorAttachmentFormats`
    - Set `VkPipelineRenderingCreateInfo::colorAttachmentCount = config.colorAttachmentFormats.size()`
    - Set `pColorAttachmentFormats = config.colorAttachmentFormats.data()`
    - Create one `VkPipelineColorBlendAttachmentState` per color attachment (all opaque write-all for geometry, single for lighting)
    - Use `config.depthAttachmentFormat` instead of hardcoded `VK_FORMAT_D32_SFLOAT`
    - Use `config.cullMode` instead of hardcoded back-face
  - [x]Update existing pipeline creation calls to populate `colorAttachmentFormats` with `{swapchainFormat}` to maintain backward compatibility

- [x] **Task 7: Create lighting push constants struct** (AC: #4)
  - [x]In `Renderer.h`:
    ```cpp
    struct LightingPushConstants
    {
        glm::vec3 sunDirection;   // 12 bytes
        float ambientStrength;    // 4 bytes
    };
    static_assert(sizeof(LightingPushConstants) == 16);
    ```

- [x] **Task 8: Create lighting descriptor set layout + pipeline** (AC: #4)
  - [x] Add members to `Renderer`:
    - `VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE`
    - `VkDescriptorSet m_lightingDescriptorSet = VK_NULL_HANDLE`
    - `VkPipelineLayout m_lightingPipelineLayout = VK_NULL_HANDLE`
    - `VkPipeline m_lightingPipeline = VK_NULL_HANDLE`
  - [x]Build lighting descriptor set layout:
    ```cpp
    DescriptorLayoutBuilder lightBuilder;
    lightBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // RT0
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // RT1
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Depth
                .build(device);
    ```
  - [x]Create lighting pipeline layout with lighting descriptor set + `LightingPushConstants` push constant range (FRAGMENT_BIT, 16 bytes)
  - [x]Build lighting pipeline:
    - Shaders: `lighting.vert.spv` + `lighting.frag.spv`
    - Color formats: `{swapchainFormat}` (single output to swapchain)
    - Depth format: `VK_FORMAT_UNDEFINED` (no depth for fullscreen pass)
    - Depth test: disabled
    - Depth write: disabled
    - Cull mode: `VK_CULL_MODE_NONE` (fullscreen triangle)
    - Blending: disabled (overwrite)

- [x] **Task 9: Add GBuffer to Renderer + wire descriptor writes** (AC: #1, #2)
  - [x] Add member: `std::unique_ptr<GBuffer> m_gbuffer`
  - [x]In `init()`, create GBuffer after `createSwapchainResources()`:
    ```cpp
    auto gbufferResult = GBuffer::create(m_vulkanContext, m_vulkanContext.getSwapchainExtent());
    ```
  - [x]Allocate `m_lightingDescriptorSet` from `m_descriptorAllocator`
  - [x]Write descriptor set bindings for lighting pass:
    - Binding 0: `m_gbuffer->getAlbedoView()` + `m_gbuffer->getSampler()`
    - Binding 1: `m_gbuffer->getNormalView()` + `m_gbuffer->getSampler()`
    - Binding 2: `m_swapchainResources.depthImageView` + `m_gbuffer->getSampler()`
  - [x]On swapchain resize (in `beginFrame()` deferred recreation block):
    - Destroy old GBuffer → recreate at new extent
    - Re-write lighting descriptor set with new views (descriptors are invalidated on image destroy)
  - [x]`shutdown()` order: ImGuiBackend → StagingBuffer → GBuffer → QuadIndexBuffer → Gigabuffer → destroySwapchainResources → lighting pipeline → lighting layout → geometry pipelines → descriptors → frame resources

- [x] **Task 10: Restructure beginFrame() — G-Buffer geometry pass** (AC: #3, #6)
  - [x]Replace the current single-pass dynamic rendering setup with G-Buffer pass:
    - Transition RT0 and RT1: `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL`
    - Transition depth: `UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL` (same as before)
    - Begin dynamic rendering with **2 color attachments** + depth:
      ```cpp
      std::array<VkRenderingAttachmentInfo, 2> colorAttachments{};
      // [0] = RT0 (albedo+AO), clear to {0,0,0,0}
      colorAttachments[0].imageView = m_gbuffer->getAlbedoView();
      colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      colorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
      // [1] = RT1 (normals), clear to {0,0,0,0}
      colorAttachments[1].imageView = m_gbuffer->getNormalView();
      // ... same pattern
      renderingInfo.colorAttachmentCount = 2;
      renderingInfo.pColorAttachments = colorAttachments.data();
      ```
    - Bind **geometry** pipeline (which outputs to 2 MRT)
  - [x]Do NOT transition swapchain image here — that happens later in endFrame
  - [x]ImGui beginFrame() call stays here (it's just CPU-side state init)

- [x] **Task 11: Restructure endFrame() — lighting pass + composite** (AC: #4, #5)
  - [x]After ImGui render data is prepared (but not rendered yet!), do:
    1. `vkCmdEndRendering(cmd)` — end G-Buffer pass
    2. Transition G-Buffer images for lighting read:
       - RT0: `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL`
       - RT1: `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL`
       - Depth: `DEPTH_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` (different aspect mask!)
    3. Transition swapchain image: `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL`
    4. Begin new dynamic rendering pass (swapchain only, no depth):
       ```cpp
       VkRenderingAttachmentInfo swapAttachment{};
       swapAttachment.imageView = swapchainImageViews[m_currentImageIndex];
       swapAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       swapAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // lighting overwrites all
       swapAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
       renderingInfo.pDepthAttachment = nullptr; // no depth
       ```
    5. Bind lighting pipeline + lighting descriptor set
    6. Push `LightingPushConstants` (sunDirection + ambientStrength)
    7. `vkCmdDraw(cmd, 3, 1, 0, 0)` — fullscreen triangle
    8. Render ImGui on top (same swapchain rendering pass)
    9. `vkCmdEndRendering(cmd)`
    10. Transition swapchain: `COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`
  - [x]**Critical depth transition detail**: use `VK_IMAGE_ASPECT_DEPTH_BIT` in the barrier's `subresourceRange.aspectMask` (not COLOR). The existing `transitionImage()` helper uses `VK_IMAGE_ASPECT_COLOR_BIT` — either add an overload or a parameter for aspect mask, or create a dedicated transition call for depth.

- [x] **Task 12: Update transitionImage helper for aspect mask** (AC: #3, #4)
  - [x]Current `transitionImage()` does not set `subresourceRange.aspectMask` correctly for depth images (it relies on Vulkan defaults, which happen to work for transitions that don't involve SHADER_READ). For G-Buffer we need explicit aspect mask.
  - [x] Add an optional `VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT` parameter to `transitionImage()`, or create a variant:
    ```cpp
    void transitionImage(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
    ```
  - [x]Update the depth→SHADER_READ_ONLY transition call to use `VK_IMAGE_ASPECT_DEPTH_BIT`

- [x] **Task 13: Compile shaders and validate** (AC: #7)
  - [x]Compile `gbuffer.frag` → `gbuffer.frag.spv`
  - [x]Compile `lighting.vert` → `lighting.vert.spv`
  - [x]Compile `lighting.frag` → `lighting.frag.spv`
  - [x]Existing `chunk.vert.spv` unchanged (same vertex shader for geometry pass)
  - [x]Build with `/W4 /WX` — zero warnings
  - [x]Run with Vulkan validation layers — zero errors
  - [x]Visual validation: geometry should render with directional lighting, AO visible, no artifacts at G-Buffer boundaries

## Dev Notes

### Architecture Compliance

- **RAII pattern**: GBuffer follows QuadIndexBuffer/Gigabuffer/TextureArray exactly — private ctor, `static create()` factory returning `Result<unique_ptr<T>>`, deleted copy/move. [Source: architecture.md#Memory & Ownership]
- **Error handling**: `Result<T>` for factory. Image creation failure = fatal. [Source: project-context.md#Error Handling]
- **Naming**: PascalCase class `GBuffer`, camelCase methods `getAlbedoView()`, `m_` prefix members. [Source: CLAUDE.md#Naming Conventions]
- **One class per file**: GBuffer.h / GBuffer.cpp. [Source: CLAUDE.md#Critical Rules]
- **File location**: `engine/include/voxel/renderer/GBuffer.h` + `engine/src/renderer/GBuffer.cpp`. [Source: architecture.md#Project Tree]

### Why Deferred Rendering

The architecture mandates deferred rendering (architecture.md § System 5 — Deferred Rendering) for two reasons:
1. **Decouples lighting from geometry** — arbitrary light count without multiplying draw calls
2. **Screen-space effects** — SSAO (Story 10.4), screen-space reflections need G-Buffer data

The geometry pass is slightly more expensive per pixel (writing 2 color attachments instead of 1), but the lighting pass runs in constant time regardless of scene complexity.

### G-Buffer Format Rationale

| Attachment | Format | Why |
|-----------|--------|-----|
| RT0 | `R8G8B8A8_SRGB` | sRGB for albedo (matches block texture format). Alpha channel stores AO (0-1). SRGB ensures GPU auto-converts to linear for lighting math. |
| RT1 | `R16G16_SFLOAT` | Octahedral encoding maps 3D unit normal to 2D `[-1,1]`. Half-float gives ~10-bit mantissa per axis — more than sufficient for block normals. 4 bytes vs 12 for raw normal. |
| Depth | `D32_SFLOAT` | Already used by existing depth buffer. Now also sampled by lighting pass for sky detection (depth==1.0). |

Total G-Buffer bandwidth per pixel: 4 (RT0) + 4 (RT1) + 4 (depth) = 12 bytes. This is extremely lightweight.

### Octahedral Normal Encoding

Standard approach from "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle et al. 2014). Works by projecting the unit sphere onto an octahedron, then unfolding to a square:

```
Encode: vec3 → vec2 (in [0,1] for storage in RG16F)
Decode: vec2 → vec3 (in lighting.frag)
```

For block normals (only 6 possible axis-aligned values), this is exact — no precision loss.

### Fullscreen Triangle vs. Fullscreen Quad

Using a single fullscreen triangle (3 vertices, 1 draw call, no VBO) is more efficient than a quad (4 vertices or 2 triangles):
- No wasted fragment shader invocations at the quad diagonal
- Single triangle covers the entire viewport
- Built-in `gl_VertexIndex` computes UV — zero buffer binds

Pattern: `uv = vec2((vertexIndex << 1) & 2, vertexIndex & 2)`, then `position = uv * 2.0 - 1.0`. This generates a triangle from (-1,-1) to (3,-1) to (-1,3), which fully covers the [-1,1] viewport.

### Image Layout Transition Flow Per Frame

```
         G-Buffer Pass                      Lighting Pass
         ─────────────                      ─────────────
RT0:   UNDEFINED → COLOR_ATTACHMENT  →  SHADER_READ_ONLY  →  (stays until next frame)
RT1:   UNDEFINED → COLOR_ATTACHMENT  →  SHADER_READ_ONLY  →  (stays until next frame)
Depth: UNDEFINED → DEPTH_ATTACHMENT  →  SHADER_READ_ONLY  →  (stays until next frame)
Swap:  (untouched)                   →  UNDEFINED → COLOR_ATTACHMENT → PRESENT_SRC
```

Using `UNDEFINED` as old layout for G-Buffer images is correct because we clear them every frame (loadOp=CLEAR) — contents are discarded.

### Depth Image Aspect Mask — Critical

The existing `transitionImage()` helper at Renderer.cpp:531 sets `subresourceRange.aspectMask` based on layout detection — BUT for the new depth→SHADER_READ_ONLY transition, it MUST use `VK_IMAGE_ASPECT_DEPTH_BIT`. Check the existing implementation carefully. If it already detects depth layouts and sets the correct aspect, no change is needed. If it always uses `COLOR_BIT`, add an aspect parameter.

Reading the current implementation: the helper does NOT explicitly set `aspectMask` — it relies on the default value of 0, which Vulkan implicitly treats as ALL aspects. For the depth→SHADER_READ_ONLY transition, we MUST explicitly set `VK_IMAGE_ASPECT_DEPTH_BIT` because `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` for a depth image requires the depth aspect. Add the parameter to be safe.

### Pipeline Architecture After This Story

| Pipeline | Shaders | Color Formats | Depth | Push Constants |
|----------|---------|---------------|-------|----------------|
| Geometry (fill) | chunk.vert + gbuffer.frag | {RGBA8_SRGB, RG16_SFLOAT} | D32_SFLOAT | ChunkPushConstants (80B) |
| Geometry (wire) | chunk.vert + gbuffer.frag | {RGBA8_SRGB, RG16_SFLOAT} | D32_SFLOAT | ChunkPushConstants (80B) |
| Lighting | lighting.vert + lighting.frag | {swapchainFormat} | none | LightingPushConstants (16B) |

Geometry pipelines reuse the existing `m_pipelineLayout` (chunk descriptor set + chunk push constants). Lighting pipeline uses a NEW `m_lightingPipelineLayout` (lighting descriptor set + lighting push constants).

### Descriptor Set Architecture After This Story

**Set 0 (Chunk rendering — unchanged):**
| Binding | Type | Stage | Purpose |
|---------|------|-------|---------|
| 0 | STORAGE_BUFFER | VERTEX | Gigabuffer (quad data) |
| 1 | STORAGE_BUFFER | VERTEX | ChunkRenderInfo (placeholder, Story 6.3) |

**Set 0 (Lighting pass — NEW, separate layout):**
| Binding | Type | Stage | Purpose |
|---------|------|-------|---------|
| 0 | COMBINED_IMAGE_SAMPLER | FRAGMENT | G-Buffer RT0 (albedo+AO) |
| 1 | COMBINED_IMAGE_SAMPLER | FRAGMENT | G-Buffer RT1 (normal) |
| 2 | COMBINED_IMAGE_SAMPLER | FRAGMENT | Depth buffer |

These are **separate** descriptor set layouts bound to different pipeline layouts. The lighting pipeline only sees lighting descriptors; the chunk pipeline only sees chunk descriptors.

### DescriptorAllocator Pool Capacity

The existing pool already includes `COMBINED_IMAGE_SAMPLER` capacity (DescriptorAllocator.cpp:166 — `SETS_PER_POOL` samplers). Adding 3 samplers for the lighting set is well within capacity.

### Swapchain Resize Handling

On resize (triggered by `m_needsSwapchainRecreate` in `beginFrame()`):
1. Existing: recreate swapchain → recreate renderFinishedSemaphores → destroySwapchainResources → createSwapchainResources
2. **New**: after createSwapchainResources, also recreate GBuffer at new extent
3. **New**: re-write lighting descriptor set with new image views (old VkImageViews are destroyed with old GBuffer)

The GBuffer recreate path:
```cpp
m_gbuffer.reset(); // destroys old images/views
auto gbufResult = GBuffer::create(m_vulkanContext, m_vulkanContext.getSwapchainExtent());
m_gbuffer = std::move(gbufResult.value());
writeLightingDescriptors(); // re-bind new views
```

### ImGui Integration — Unchanged Externally

ImGui currently renders **inside** the dynamic rendering pass (between `vkCmdBeginRendering` and `vkCmdEndRendering`). With deferred rendering:
- `beginFrame()` starts the **G-Buffer** pass
- `m_imguiBackend->beginFrame()` still called in beginFrame() (CPU-side state only)
- In `endFrame()`, after the lighting fullscreen pass, ImGui renders **in the same lighting pass** (to swapchain) — it was always designed to render on top of whatever color attachment is active
- The ImGui pipeline's `VkPipelineRenderingCreateInfo` uses the swapchain color format, which matches the lighting pass. No ImGui changes needed.

### What This Story Does NOT Do

- Does NOT implement texture array sampling — geometry pass still uses placeholder face-normal colors (Story 6.5 replaces this)
- Does NOT implement transparent/translucent pass — that is Story 6.7
- Does NOT implement sky light or block light integration — those are Story 8.x
- Does NOT implement SSAO — that is Story 10.4
- Does NOT implement day/night cycle — future lighting story
- Does NOT change the chunk vertex shader (`chunk.vert`) — only replaces the fragment shader
- Does NOT modify the compute culling pipeline (Story 6.4) — geometry submission is orthogonal

### Project Structure Notes

```
engine/include/voxel/renderer/
  GBuffer.h                       (NEW)
  RendererConstants.h             (MODIFY — add G-Buffer format constants)
  Renderer.h                      (MODIFY — add GBuffer member, lighting pipeline/descriptors,
                                   LightingPushConstants, PipelineConfig extensions)
engine/src/renderer/
  GBuffer.cpp                     (NEW)
  Renderer.cpp                    (MODIFY — restructure begin/endFrame, create lighting pipeline,
                                   manage GBuffer lifecycle, extend buildPipeline)
assets/shaders/
  gbuffer.frag                    (NEW — G-Buffer MRT fragment shader)
  lighting.vert                   (NEW — fullscreen triangle vertex shader)
  lighting.frag                   (NEW — deferred lighting fragment shader)
```

### Previous Story Intelligence

**From Story 6.5 (Texture Array — ready-for-dev):**
- Story 6.5 will modify `chunk.frag` to sample `sampler2DArray` for real textures. With this story, chunk.frag is replaced by `gbuffer.frag` for the geometry pass. When 6.5 is implemented, it should update `gbuffer.frag` instead of `chunk.frag`.
- Descriptor binding 4 (COMBINED_IMAGE_SAMPLER) for the texture array is in the **chunk** descriptor set layout, not the lighting descriptor set. The gbuffer.frag shader will need it when 6.5 is integrated. Plan the descriptor layout binding accordingly.
- The old `chunk.frag` can be kept as a reference/fallback but is no longer used in the active render path.

**From Story 6.4 (Compute Culling — ready-for-dev):**
- Story 6.4 replaces `renderChunks()` with `renderChunksIndirect()`. The G-Buffer change is orthogonal — both methods write to whatever color attachments are bound. The key is that the pipeline bound must match the current rendering pass attachments (G-Buffer formats, not swapchain format).
- When 6.4 is implemented, `renderChunksIndirect()` must use the geometry pipeline (with G-Buffer outputs), not the old swapchain pipeline.

**From Story 6.3 (IndirectDrawBuffer — ready-for-dev):**
- Descriptor layout will be extended to bindings 0-3 (+ binding 4 from 6.5). This story's lighting descriptor set is completely separate (different layout, different set).
- ChunkRenderInfoBuffer, IndirectDrawBuffer → destroyed before GBuffer in shutdown (they're independent resources).

**From Story 6.2 (Vertex Pulling — in progress):**
- Push constants: `ChunkPushConstants` (80 bytes). Unchanged by this story.
- `chunk.vert` outputs: fragWorldPos, fragNormal, fragUV, fragAO, fragBlockStateId, fragTintIndex at locations 0-5. `gbuffer.frag` must declare matching inputs.

### Git Intelligence

Recent commits follow consistent `feat(renderer):` pattern:
```
5c50868 feat(renderer): add IndirectDrawBuffer and ChunkRenderInfoBuffer for GPU-driven rendering
140113a chore: finalize Story 6.0 and update shaders/render states
bd3e207 feat(renderer): implement GPU-driven chunk rendering via vertex pulling
```

Suggested commit: `feat(renderer): implement deferred rendering G-Buffer with lighting pass`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.6 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — § System 5: Vulkan Renderer (Deferred Rendering, G-Buffer layout, Shader Architecture)]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Memory & Ownership]
- [Source: engine/include/voxel/renderer/Renderer.h — Full class definition, SwapchainResources, PipelineConfig, current members]
- [Source: engine/src/renderer/Renderer.cpp:29-178 — init() flow (descriptor setup, pipeline creation, swapchain resources)]
- [Source: engine/src/renderer/Renderer.cpp:294-353 — createSwapchainResources() depth image creation pattern]
- [Source: engine/src/renderer/Renderer.cpp:409-529 — buildPipeline() with VkPipelineRenderingCreateInfo (single color attachment)]
- [Source: engine/src/renderer/Renderer.cpp:531-583 — transitionImage() helper (VkImageMemoryBarrier2)]
- [Source: engine/src/renderer/Renderer.cpp:585-718 — beginFrame() dynamic rendering setup (1 color + depth)]
- [Source: engine/src/renderer/Renderer.cpp:788-892 — endFrame() ImGui render, submit, present flow]
- [Source: engine/src/renderer/Renderer.cpp:894-974 — shutdown() destruction order]
- [Source: engine/include/voxel/renderer/RendererConstants.h — Existing constants (FRAMES_IN_FLIGHT, MAX_QUADS)]
- [Source: engine/include/voxel/renderer/DescriptorAllocator.h — DescriptorLayoutBuilder fluent API]
- [Source: assets/shaders/chunk.vert — Full vertex shader (219 lines, outputs at locations 0-5)]
- [Source: assets/shaders/chunk.frag — Current forward-pass fragment shader (37 lines, face-normal coloring)]
- [Source: _bmad-output/implementation-artifacts/6-5-texture-array-loading.md — Texture array descriptor at binding 4]
- [Source: _bmad-output/implementation-artifacts/6-4-compute-culling-shader.md — Compute pipeline sharing descriptor set]
- [Source: _bmad-output/implementation-artifacts/6-3-indirect-draw-buffer-chunkrenderinfo-ssbo.md — Descriptor bindings 0-3]

## File List

**New files:**
- `engine/include/voxel/renderer/GBuffer.h` — G-Buffer class declaration (RAII factory)
- `engine/src/renderer/GBuffer.cpp` — G-Buffer implementation (image creation, views, sampler, cleanup)
- `assets/shaders/gbuffer.frag` — G-Buffer MRT fragment shader (albedo+AO, octahedral normal encoding)
- `assets/shaders/lighting.vert` — Fullscreen triangle vertex shader (no VBO)
- `assets/shaders/lighting.frag` — Deferred lighting fragment shader (directional sun + ambient + AO)

**Modified files:**
- `engine/include/voxel/renderer/RendererConstants.h` — Added GBUFFER_RT0_FORMAT, GBUFFER_RT1_FORMAT, GBUFFER_DEPTH_FORMAT constants
- `engine/include/voxel/renderer/Renderer.h` — Added GBuffer forward decl, LightingPushConstants struct, lighting pipeline/descriptor members, extended PipelineConfig (colorAttachmentFormats, depthAttachmentFormat, enableBlending, cullMode, pipelineLayout), updated transitionImage signature
- `engine/src/renderer/Renderer.cpp` — Added SAMPLED bit to depth image, restructured beginFrame/endFrame for G-Buffer geometry + lighting passes, implemented createLightingPipeline/writeLightingDescriptors, extended buildPipeline for multiple color attachments, added G-Buffer lifecycle (init/resize/shutdown), added new transition cases (COLOR_ATTACHMENT→SHADER_READ, DEPTH_ATTACHMENT→SHADER_READ)
- `engine/CMakeLists.txt` — Added GBuffer.cpp to build

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Build: 0 warnings, 0 errors. All 3 new shaders compiled to SPIR-V with glslangValidator (Vulkan 1.3 target).
- Tests: 489,011 assertions in 164 test cases — all passed, zero regressions.

### Completion Notes List

- **Task 1**: Added 3 G-Buffer format constants to RendererConstants.h (RT0=R8G8B8A8_SRGB, RT1=R16G16_SFLOAT, Depth=D32_SFLOAT).
- **Task 2**: Created GBuffer class following RAII factory pattern (QuadIndexBuffer/Gigabuffer model). Two VkImages with VMA allocation, two VkImageViews, one shared VkSampler (nearest, clamp-to-edge). RAII destructor cleans up in reverse creation order.
- **Task 3**: Added VK_IMAGE_USAGE_SAMPLED_BIT to depth image creation in createSwapchainResources() to allow lighting pass to sample depth.
- **Task 4**: Created gbuffer.frag with same inputs as chunk.frag (locations 0-5), 2 MRT outputs (albedoAO + octahedral normal), texture sampling from blockTextures array.
- **Task 5**: Created lighting.vert (fullscreen triangle, no VBO) and lighting.frag (G-Buffer read, octahedral decode, directional sun + ambient + AO, sky early-out at depth>=1.0). AO remapping matches forward-pass behavior (mix 0.4-1.0).
- **Task 6**: Extended PipelineConfig with colorAttachmentFormats vector, depthAttachmentFormat, enableBlending, cullMode, pipelineLayout. Updated buildPipeline() to use these (vector of blend attachments, configurable cull/layout). Updated existing pipeline calls to use G-Buffer formats (RT0+RT1) instead of swapchain.
- **Task 7**: Added LightingPushConstants struct (sunDirection vec3 + ambientStrength float = 16 bytes) with static_assert.
- **Task 8**: Implemented createLightingPipeline() — builds lighting descriptor set layout (3 combined image samplers), lighting pipeline layout (descriptor set + push constants), and lighting graphics pipeline (no depth, cull none, swapchain format output).
- **Task 9**: Integrated GBuffer lifecycle into Renderer: creation in init() after swapchainResources, descriptor allocation + writes, resize handling (destroy + recreate + rewrite descriptors), proper shutdown ordering.
- **Task 10**: Restructured beginFrame() to transition G-Buffer images (RT0, RT1 → COLOR_ATTACHMENT, depth → DEPTH_ATTACHMENT with explicit DEPTH aspect mask) instead of swapchain. beginRenderPass() now creates G-Buffer pass (2 color attachments + depth, clear all).
- **Task 11**: Restructured endFrame() with full deferred pipeline: end G-Buffer pass → transition G-Buffer to SHADER_READ → transition swapchain to COLOR_ATTACHMENT → begin lighting pass (swapchain only, no depth) → bind lighting pipeline/descriptors → push sun direction/ambient → draw fullscreen triangle → render ImGui on top → end pass → transition to PRESENT.
- **Task 12**: Added aspectMask parameter to transitionImage() (default COLOR_BIT). Added two new transition cases: COLOR_ATTACHMENT→SHADER_READ_ONLY (for G-Buffer color targets) and DEPTH_ATTACHMENT→SHADER_READ_ONLY (for depth buffer). Depth transitions use correct DEPTH aspect.
- **Task 13**: All 3 new shaders compiled successfully. Full project builds with zero warnings/errors. 164 test cases pass with zero regressions.

### Change Log

- **2026-03-28**: Implemented deferred rendering G-Buffer with lighting pass (Story 6.6). Added GBuffer class (RT0 albedo+AO, RT1 octahedral normal), gbuffer.frag MRT shader, fullscreen triangle lighting pass (directional sun + ambient + AO), extended PipelineConfig for multi-attachment pipelines, restructured Renderer frame flow for G-Buffer geometry → lighting composite → ImGui overlay.
