# Story 6.0: Vulkan Descriptor Infrastructure

Status: ready-for-dev

## Story

As a developer,
I want descriptor pool, set layouts, and allocation helpers,
so that Stories 6.2–6.6 can bind SSBOs and texture arrays without each one reinventing descriptor management.

## Why Now

`Renderer::createPipeline()` (Renderer.cpp line 35-38) creates a `VkPipelineLayout` with zero descriptor set layouts. Story 6.2 needs SSBO bindings (gigabuffer + per-draw data). Story 6.4 needs compute descriptors. Story 6.5 needs a sampler for the texture array. Without this story, each of 6.2/6.3/6.4/6.5 will independently hack descriptor creation. This story provides a clean, reusable descriptor management layer.

## Acceptance Criteria

1. **AC1 — DescriptorAllocator**: A `DescriptorAllocator` class wraps `VkDescriptorPool` with automatic growth (create new pool when current is full). Provides `allocate(VkDescriptorSetLayout) → Result<VkDescriptorSet>` and `resetPools()`. RAII cleanup of all pools.
2. **AC2 — DescriptorLayoutBuilder**: A builder struct with chained methods: `addBinding(binding, type, stageFlags) → self&`, `build(device) → Result<VkDescriptorSetLayout>`.
3. **AC3 — PipelineConfig updated**: `PipelineConfig` accepts a `std::vector<VkDescriptorSetLayout>` and optional push constant ranges. `buildPipeline()` uses them to create `m_pipelineLayout`.
4. **AC4 — Chunk descriptor set layout created**: Binding 0 = SSBO (gigabuffer, vertex stage), Binding 1 = SSBO (per-draw ChunkRenderInfo, vertex stage). Layout stored on the Renderer for reuse by Stories 6.2–6.4.
5. **AC5 — Pipeline layout includes descriptor set layout**: `m_pipelineLayout` is rebuilt with the chunk descriptor set layout and push constant range for MVP matrix + time.
6. **AC6 — Gigabuffer wired as SSBO**: A descriptor set is allocated and written with the Gigabuffer VkBuffer bound to binding 0. Binding 1 is left unwritten until Story 6.3 provides the ChunkRenderInfo SSBO.
7. **AC7 — No Vulkan validation errors**: The Vulkan validation layer reports zero errors/warnings related to descriptors, pipeline layout, or descriptor set binding during startup and frame rendering.

## Tasks / Subtasks

- [ ] **Task 1: Create DescriptorAllocator** (AC: #1)
  - [ ] 1.1 Create `engine/include/voxel/renderer/DescriptorAllocator.h`
  - [ ] 1.2 Create `engine/src/renderer/DescriptorAllocator.cpp`
  - [ ] 1.3 Constructor takes `VkDevice`
  - [ ] 1.4 Implement `allocate(VkDescriptorSetLayout) → Result<VkDescriptorSet>` — tries current pool, creates new pool on `VK_ERROR_OUT_OF_POOL_MEMORY` or `VK_ERROR_FRAGMENTED_POOL`
  - [ ] 1.5 Implement `resetPools()` — resets all pools via `vkResetDescriptorPool`, moves all to free list
  - [ ] 1.6 Implement destructor — `vkDestroyDescriptorPool` for all pools
  - [ ] 1.7 Pool creation uses sensible defaults: 1000 max sets, balanced type counts (see Design section)

- [ ] **Task 2: Create DescriptorLayoutBuilder** (AC: #2)
  - [ ] 2.1 Define `DescriptorLayoutBuilder` struct in `DescriptorAllocator.h` (small helper, same header is fine)
  - [ ] 2.2 `addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t count = 1) → DescriptorLayoutBuilder&`
  - [ ] 2.3 `build(VkDevice device) → Result<VkDescriptorSetLayout>`
  - [ ] 2.4 `clear()` — resets bindings for reuse

- [ ] **Task 3: Update PipelineConfig and buildPipeline()** (AC: #3, #5)
  - [ ] 3.1 Add `std::vector<VkDescriptorSetLayout> descriptorSetLayouts` to `PipelineConfig`
  - [ ] 3.2 Add `std::vector<VkPushConstantRange> pushConstantRanges` to `PipelineConfig`
  - [ ] 3.3 In `Renderer::init()`, move pipeline layout creation into `buildPipeline()` or create a separate `createPipelineLayout()` method that uses PipelineConfig fields
  - [ ] 3.4 Update `buildPipeline()` to pass `descriptorSetLayouts` and `pushConstantRanges` to `VkPipelineLayoutCreateInfo`
  - [ ] 3.5 Both `m_pipeline` and `m_wireframePipeline` share the same layout (already the case)

- [ ] **Task 4: Create chunk rendering descriptor set layout** (AC: #4)
  - [ ] 4.1 In `Renderer::init()`, use `DescriptorLayoutBuilder` to create the chunk layout:
    - Binding 0: `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `VK_SHADER_STAGE_VERTEX_BIT` (gigabuffer)
    - Binding 1: `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `VK_SHADER_STAGE_VERTEX_BIT` (ChunkRenderInfo per-draw data)
  - [ ] 4.2 Store as `m_chunkDescriptorSetLayout` on Renderer
  - [ ] 4.3 Destroy in `Renderer::shutdown()` via `vkDestroyDescriptorSetLayout`

- [ ] **Task 5: Define push constant range** (AC: #5)
  - [ ] 5.1 Define `ChunkPushConstants` struct: `glm::mat4 viewProjection` (64 bytes) + `float time` (4 bytes) + `float padding[3]` (12 bytes) = 80 bytes total
  - [ ] 5.2 Create `VkPushConstantRange` for vertex stage, offset 0, size 80 bytes
  - [ ] 5.3 Pass to PipelineConfig alongside the descriptor set layout

- [ ] **Task 6: Allocate and write chunk descriptor set** (AC: #6)
  - [ ] 6.1 Create `DescriptorAllocator` instance (owned by Renderer)
  - [ ] 6.2 Allocate a descriptor set from the chunk layout
  - [ ] 6.3 Write binding 0: `VkDescriptorBufferInfo` with `Gigabuffer::getBuffer()`, offset=0, range=`VK_WHOLE_SIZE`
  - [ ] 6.4 Write binding 1 with a dummy/placeholder (null buffer or skip) — Story 6.3 will provide the real ChunkRenderInfo SSBO
  - [ ] 6.5 Store as `m_chunkDescriptorSet` on Renderer

- [ ] **Task 7: Expose descriptor infrastructure for downstream stories** (AC: #4, #6)
  - [ ] 7.1 Add public accessor: `VkDescriptorSetLayout getChunkDescriptorSetLayout() const`
  - [ ] 7.2 Add public accessor: `VkDescriptorSet getChunkDescriptorSet() const`
  - [ ] 7.3 Add public accessor: `DescriptorAllocator& getDescriptorAllocator()`
  - [ ] 7.4 Add public accessor: `VkPipelineLayout getPipelineLayout() const`

- [ ] **Task 8: Build system** (AC: all)
  - [ ] 8.1 Add `src/renderer/DescriptorAllocator.cpp` to `engine/CMakeLists.txt`
  - [ ] 8.2 Verify build succeeds with zero Vulkan validation errors on startup
  - [ ] 8.3 Verify all existing tests still pass (no regressions)

## Dev Notes

### Architecture Compliance

- **One class per file**: `DescriptorAllocator` + `DescriptorLayoutBuilder` in same header (builder is a tiny helper, not a full class)
- **Namespace**: `voxel::renderer` for all new types
- **Error handling**: No exceptions. `allocate()` returns `Result<VkDescriptorSet>`. `build()` returns `Result<VkDescriptorSetLayout>`. Use `EngineError::VulkanError` for Vulkan failures.
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, SCREAMING_SNAKE constants
- **Max 500 lines per file** — DescriptorAllocator should be ~150-200 lines total
- **`#pragma once`** for all headers
- **RAII**: DescriptorAllocator owns all pools, destructor cleans up

### Existing Code to Reuse — DO NOT REINVENT

- **`Gigabuffer::getBuffer()`** (Gigabuffer.h:67): Returns `VkBuffer` for descriptor write. Already exists. Use for binding 0 SSBO.
- **`Gigabuffer::getCapacity()`** (Gigabuffer.h:69): Returns buffer size for `VkDescriptorBufferInfo.range`. Or use `VK_WHOLE_SIZE`.
- **`VulkanContext::getDevice()`** (VulkanContext.h:30): Returns `VkDevice` for all Vulkan calls. Already the pattern.
- **`Renderer::m_vulkanContext`** (Renderer.h:129): Non-owning reference to VulkanContext. Use `m_vulkanContext.getDevice()` everywhere.
- **`Renderer::m_pipelineLayout`** (Renderer.h:135): Already exists. Modify its creation, don't create a second layout.
- **`PipelineConfig` struct** (Renderer.h:109-116): Extend this with new fields, do NOT create a separate config type.
- **`buildPipeline()` method** (Renderer.h:119): Already takes `PipelineConfig`. Extend it.
- **Pipeline layout creation** (Renderer.cpp:34-38): Currently creates empty layout. Replace with layout that includes descriptors + push constants.
- **`ImGuiBackend` descriptor pool** (ImGuiBackend.cpp:19-28): ImGui has its own pool. Do NOT share it with chunk rendering. DescriptorAllocator is for engine rendering only.
- **`core::Result<T>`** (Result.h): Use for all failable operations. Pattern: `return std::unexpected(EngineError{...})`.
- **`EngineError` enum** (includes `VulkanError`, `OutOfMemory`): Use for descriptor allocation failures.

### What NOT To Do

- Do NOT create descriptor sets for compute (Story 6.4) or texture arrays (Story 6.5) — this story only builds the infrastructure + chunk rendering layout
- Do NOT write binding 1 with real data — ChunkRenderInfo SSBO doesn't exist until Story 6.3. Leave binding 1 unwritten or write a dummy buffer.
- Do NOT bind the descriptor set during rendering yet — the current shaders (triangle.vert/frag) don't use descriptors. Story 6.2 will write chunk.vert that reads from the SSBO.
- Do NOT modify the existing triangle shaders — they'll be replaced in Story 6.2
- Do NOT create a compute pipeline layout — that's Story 6.4
- Do NOT share ImGui's descriptor pool — ImGui manages its own descriptors
- Do NOT implement dynamic descriptor indexing features (bindless) yet — Story 6.5 will use a simple sampler2DArray binding, not bindless
- Do NOT destroy and recreate descriptors on swapchain resize — descriptor sets are resolution-independent
- Do NOT use `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC` — static SSBO binding is sufficient (the whole gigabuffer is one buffer)
- Do NOT add the chunk descriptor set layout to ImGui-only pipeline paths

### DescriptorAllocator Design

```cpp
// DescriptorAllocator.h
#pragma once
#include "voxel/core/Result.h"

#include <volk.h>

#include <cstdint>
#include <vector>

namespace voxel::renderer
{

/// Builder for VkDescriptorSetLayout with chained API.
class DescriptorLayoutBuilder
{
public:
    DescriptorLayoutBuilder& addBinding(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t count = 1);

    [[nodiscard]] core::Result<VkDescriptorSetLayout> build(VkDevice device);

    void clear();

private:
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
};

/// Manages VkDescriptorPool allocation with automatic pool growth.
class DescriptorAllocator
{
public:
    explicit DescriptorAllocator(VkDevice device);
    ~DescriptorAllocator();

    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
    DescriptorAllocator(DescriptorAllocator&&) = delete;
    DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;

    /// Allocate a descriptor set from the managed pools.
    [[nodiscard]] core::Result<VkDescriptorSet> allocate(VkDescriptorSetLayout layout);

    /// Reset all pools. Invalidates all previously allocated descriptor sets.
    void resetPools();

private:
    core::Result<VkDescriptorPool> createPool();

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_usedPools;
    std::vector<VkDescriptorPool> m_freePools;
    VkDescriptorPool m_currentPool = VK_NULL_HANDLE;

    static constexpr uint32_t SETS_PER_POOL = 1000;
};

} // namespace voxel::renderer
```

### Pool Creation Details

Each pool allocates a balanced set of descriptor types. This avoids needing to know exact counts upfront:

```cpp
core::Result<VkDescriptorPool> DescriptorAllocator::createPool()
{
    std::array<VkDescriptorPoolSize, 4> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SETS_PER_POOL * 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SETS_PER_POOL},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_PER_POOL},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SETS_PER_POOL / 2},
    }};

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = SETS_PER_POOL;
    poolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCI.pPoolSizes = poolSizes.data();
    // Note: NO VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    // We reset entire pools, never free individual sets.

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorPool(m_device, &poolCI, nullptr, &pool);
    if (result != VK_SUCCESS)
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to create descriptor pool"});

    return pool;
}
```

**Storage buffer gets 2x multiplier** because chunk rendering needs 2 SSBOs per set, and future compute passes need SSBOs too.

### allocate() Flow

```
allocate(layout):
  1. If m_currentPool is null → grab from m_freePools or createPool()
  2. Try vkAllocateDescriptorSets(m_currentPool, ...)
  3. If VK_SUCCESS → return set
  4. If VK_ERROR_OUT_OF_POOL_MEMORY or VK_ERROR_FRAGMENTED_POOL:
     a. Move m_currentPool to m_usedPools
     b. Grab from m_freePools or createPool()
     c. Retry vkAllocateDescriptorSets
     d. If still fails → return error
  5. Other VkResult → return error
```

### resetPools() Flow

```
resetPools():
  1. For each pool in m_usedPools:
     - vkResetDescriptorPool(device, pool, 0)
     - Move to m_freePools
  2. If m_currentPool is not null:
     - vkResetDescriptorPool(device, m_currentPool, 0)
     - Move to m_freePools
  3. m_currentPool = null
```

### ChunkPushConstants Struct

```cpp
// In a shared header or directly in Renderer.h
struct ChunkPushConstants
{
    glm::mat4 viewProjection;  // 64 bytes
    float time;                // 4 bytes
    float padding[3];          // 12 bytes (align to 16)
};
static_assert(sizeof(ChunkPushConstants) == 80);
```

Push constants are updated per-frame via `vkCmdPushConstants()` in `beginFrame()`. This avoids needing a per-frame UBO for the VP matrix. 80 bytes is well within the minimum guaranteed 128-byte push constant limit.

**Why push constants instead of UBO for VP matrix**: Push constants are faster (no memory access), simpler (no buffer management), and the VP matrix + time fit easily within limits. UBOs are reserved for larger data (tint palette = 128 bytes, future per-frame data).

### Renderer Modifications Summary

**New members to add:**
```cpp
// Renderer.h private:
std::unique_ptr<DescriptorAllocator> m_descriptorAllocator;
VkDescriptorSetLayout m_chunkDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSet m_chunkDescriptorSet = VK_NULL_HANDLE;  // allocated from pool, not individually freed
```

**New public accessors:**
```cpp
// Renderer.h public:
[[nodiscard]] VkDescriptorSetLayout getChunkDescriptorSetLayout() const { return m_chunkDescriptorSetLayout; }
[[nodiscard]] VkDescriptorSet getChunkDescriptorSet() const { return m_chunkDescriptorSet; }
[[nodiscard]] DescriptorAllocator& getDescriptorAllocator() { return *m_descriptorAllocator; }
[[nodiscard]] VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
```

**init() changes — descriptor setup order:**
```
1. Create Gigabuffer (already done)
2. Create StagingBuffer (already done)
3. Create DescriptorAllocator (NEW)
4. Build chunk descriptor set layout via DescriptorLayoutBuilder (NEW)
5. Define push constant range (NEW)
6. Set PipelineConfig.descriptorSetLayouts and pushConstantRanges (NEW)
7. Create pipeline layout using PipelineConfig (MODIFIED — was empty)
8. Build m_pipeline and m_wireframePipeline (already done, now with layout)
9. Allocate chunk descriptor set (NEW)
10. Write gigabuffer to binding 0 (NEW)
11. Init ImGui (already done)
```

**shutdown() changes — add cleanup before pipeline layout destroy:**
```cpp
// After destroying pipelines, before destroying pipeline layout:
if (m_chunkDescriptorSetLayout != VK_NULL_HANDLE)
{
    vkDestroyDescriptorSetLayout(device, m_chunkDescriptorSetLayout, nullptr);
    m_chunkDescriptorSetLayout = VK_NULL_HANDLE;
}
m_descriptorAllocator.reset();  // Destroys all pools (and their descriptor sets)
```

**Destruction order matters**: Destroy descriptor set layout AFTER the descriptor allocator is reset (pools destroyed). Actually, the descriptor set layout must outlive the pipeline and descriptor sets that reference it. Safe order:
1. Destroy pipelines (already done)
2. Reset/destroy DescriptorAllocator (destroys pools + sets)
3. Destroy descriptor set layout
4. Destroy pipeline layout

### Descriptor Write for Gigabuffer (Binding 0)

```cpp
VkDescriptorBufferInfo gigabufferInfo{};
gigabufferInfo.buffer = m_gigabuffer->getBuffer();
gigabufferInfo.offset = 0;
gigabufferInfo.range = VK_WHOLE_SIZE;

VkWriteDescriptorSet write{};
write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
write.dstSet = m_chunkDescriptorSet;
write.dstBinding = 0;
write.descriptorCount = 1;
write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
write.pBufferInfo = &gigabufferInfo;

vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
```

**Binding 1 (ChunkRenderInfo)**: Not written in this story. The descriptor set layout declares it, but the actual SSBO doesn't exist until Story 6.3 creates the `ChunkRenderInfoBuffer`. When Story 6.3 is implemented, it will call `vkUpdateDescriptorSets` to write binding 1. Until then, binding 1 is simply not accessed by any shader — this is valid in Vulkan as long as the shader doesn't read from it.

### PipelineConfig Extension

```cpp
struct PipelineConfig
{
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    std::string vertShaderPath;
    std::string fragShaderPath;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;  // NEW
    std::vector<VkPushConstantRange> pushConstantRanges;      // NEW
};
```

### Pipeline Layout Creation (MODIFIED)

Replace the current empty layout creation in `Renderer::init()` (lines 34-38) with one that uses PipelineConfig:

```cpp
// In init(), after building descriptor set layout:
VkPushConstantRange pushRange{};
pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
pushRange.offset = 0;
pushRange.size = sizeof(ChunkPushConstants);

PipelineConfig fillConfig;
fillConfig.polygonMode = VK_POLYGON_MODE_FILL;
fillConfig.vertShaderPath = shaderDir + "/triangle.vert.spv";  // Will be chunk.vert in 6.2
fillConfig.fragShaderPath = shaderDir + "/triangle.frag.spv";  // Will be chunk.frag in 6.2
fillConfig.descriptorSetLayouts = {m_chunkDescriptorSetLayout};
fillConfig.pushConstantRanges = {pushRange};
```

The pipeline layout is still created ONCE and shared by both fill and wireframe pipelines. Move layout creation into `buildPipeline()` or keep it separate — just ensure it uses the config's layouts and push constants.

**Important**: The pipeline layout must be created BEFORE `buildPipeline()` is called (pipeline references the layout). Currently init() creates layout first, then calls buildPipeline(). Keep that order. Just make the layout creation use the descriptor set layouts.

### buildPipeline() Changes

The method (Renderer.cpp lines 333-453) already receives a `PipelineConfig` and references `m_pipelineLayout` at line 432. No changes needed inside `buildPipeline()` itself — the layout is already created before it's called. Just ensure the layout creation uses the new config fields.

If refactoring layout creation into `buildPipeline()`:
```cpp
VkPipelineLayoutCreateInfo layoutInfo{};
layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
layoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
layoutInfo.pSetLayouts = config.descriptorSetLayouts.empty() ? nullptr : config.descriptorSetLayouts.data();
layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
layoutInfo.pPushConstantRanges = config.pushConstantRanges.empty() ? nullptr : config.pushConstantRanges.data();
```

### File Structure

```
engine/include/voxel/renderer/
  DescriptorAllocator.h      ← CREATE: DescriptorAllocator + DescriptorLayoutBuilder

engine/src/renderer/
  DescriptorAllocator.cpp    ← CREATE: Implementation

engine/include/voxel/renderer/
  Renderer.h                 ← MODIFY: Add descriptor members, push constants struct,
                                        public accessors, extend PipelineConfig

engine/src/renderer/
  Renderer.cpp               ← MODIFY: Create descriptor layout, allocator, set in init();
                                        update pipeline layout creation; cleanup in shutdown()

engine/CMakeLists.txt        ← MODIFY: Add DescriptorAllocator.cpp to sources
```

### Project Structure Notes

- All new files in existing directories — no new directories needed
- `DescriptorAllocator.h` needs `<volk.h>` for Vulkan types and `"voxel/core/Result.h"` for error handling
- GLM needed in Renderer.h for `ChunkPushConstants` — GLM is already included transitively (Camera.h)
- Follow existing CMakeLists.txt pattern: explicit file listing, no GLOB
- `DescriptorLayoutBuilder` is in the same header as `DescriptorAllocator` since it's a small helper (similar to how `PipelineConfig` is inside `Renderer.h`)

### Previous Story Intelligence

**From Story 5.5 (review) — quad format and tint:**
- Quad format is finalized: 64 bits with tintIndex (3 bits) + wavingType (2 bits)
- The vertex shader (Story 6.2) will need to unpack these fields from the SSBO
- TintPalette exists CPU-side; Story 6.8 will upload it as UBO via this descriptor infrastructure

**From Story 5.7 (ready-for-dev) — mesh upload to gigabuffer:**
- ChunkRenderInfo struct defined: `{GigabufferAllocation, quadCount, worldBasePos, state}`
- ChunkUploadManager will populate gigabuffer and ChunkRenderInfo map
- Story 6.3 will create a GPU SSBO from ChunkRenderInfo and write it to binding 1 of the descriptor set created here

**From Story 2.4 (done) — Gigabuffer:**
- Buffer created with `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` — ready for SSBO descriptor
- `getBuffer()` returns VkBuffer handle, `getCapacity()` returns size
- Buffer Device Address also available via `getBufferAddress()` but SSBO binding is preferred over BDA for vertex pulling (simpler shader code, no pointer arithmetic)

**From Story 3.0b (done) — PipelineConfig:**
- PipelineConfig currently has: polygonMode, depthTestEnable, depthWriteEnable, vertShaderPath, fragShaderPath
- buildPipeline() takes PipelineConfig by const ref, returns Result<VkPipeline>
- Pipeline layout created separately in init(), stored as m_pipelineLayout
- Both fill and wireframe pipelines share m_pipelineLayout

### Git Intelligence

Recent commits:
```
fa9cff6 feat(renderer): add block tinting and waving fields in quad format
c2b6438 finalize Story 5.4: apply code review fixes for non-cubic block meshing
3e21579 feat(renderer): add face culling for non-cubic blocks and optimize vertex emission
5c60d5d feat(renderer): add AO-aware greedy meshing for transparent and opaque FullCube blocks
```

Convention: `feat(renderer): description`. For this story:
- `feat(renderer): add Vulkan descriptor infrastructure for GPU-driven rendering`

### Testing Standards

- **No unit tests for GPU resources** — Vulkan descriptor pools, sets, and layouts require a live VkDevice
- **Validation via Vulkan validation layers** — Run the application, confirm zero validation errors
- **Visual verification** — Existing triangle rendering should still work (shaders don't use descriptors yet, layout change is backward-compatible)
- **Regression check** — All existing unit tests must pass (`bash build.sh VoxelTests && ./build/msvc-debug/tests/VoxelTests`)

### Vulkan Features Already Enabled

The following features are enabled in VulkanContext.cpp and are relevant to this story:
- `descriptorIndexing` (Vulkan 1.2) — Future use for bindless texture arrays (Story 6.5). Not needed for basic SSBO binding.
- `bufferDeviceAddress` (Vulkan 1.2) — Gigabuffer already has BDA. SSBO binding is simpler for vertex pulling though.

### Potential Pitfalls

1. **Pipeline layout must match shader expectations**: The current triangle.vert/frag don't declare any descriptors or push constants. Adding them to the pipeline layout is fine — Vulkan allows the pipeline layout to declare more resources than the shader uses. The shader just won't access them.
2. **Descriptor set not bound during draw**: Don't bind `m_chunkDescriptorSet` in `beginFrame()` yet — the current shaders don't use it. Story 6.2 will add the `vkCmdBindDescriptorSets` call when chunk.vert is ready.
3. **Binding 1 unwritten**: A descriptor set with an unwritten binding is valid as long as no shader reads from it. The Vulkan spec allows this. Validation layers may warn if `VK_EXT_robustness2` isn't enabled — if so, write a dummy 16-byte buffer to binding 1 to silence the warning.
4. **Pool size overallocation**: The pool sizes are generous (1000 sets × multiple types). This uses minimal GPU memory (~few KB per pool). Over-allocating is fine for a game engine with a bounded number of descriptor sets.
5. **Destruction order**: DescriptorAllocator must be destroyed BEFORE VmaAllocator (owned by VulkanContext). Since Renderer is destroyed before VulkanContext (established destruction order in GameApp), this is safe.

### References

- [Source: engine/include/voxel/renderer/Renderer.h — PipelineConfig (line 109-116), m_pipelineLayout (line 135), getGigabuffer()]
- [Source: engine/src/renderer/Renderer.cpp — Pipeline layout creation (line 34-38), buildPipeline() (line 333-453), shutdown() (line 782-789)]
- [Source: engine/include/voxel/renderer/Gigabuffer.h — getBuffer() (line 67), getCapacity() (line 69), STORAGE_BUFFER usage]
- [Source: engine/include/voxel/renderer/VulkanContext.h — getDevice() (line 30), all accessors]
- [Source: engine/src/renderer/ImGuiBackend.cpp — ImGui's own descriptor pool pattern (line 19-28), do NOT share]
- [Source: engine/include/voxel/renderer/StagingBuffer.h — Already wired for transfers, no changes needed]
- [Source: engine/include/voxel/renderer/RendererConstants.h — FRAMES_IN_FLIGHT = 2]
- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.0 AC, Stories 6.2-6.6 descriptor needs]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-002 Vulkan 1.3, ADR-009 Gigabuffer, System 5 Renderer]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, code organization]
- [Source: _bmad-output/implementation-artifacts/5-5-block-tinting-waving-animation.md — Quad format, TintPalette for future Story 6.8]
- [Source: _bmad-output/implementation-artifacts/5-7-mesh-upload-to-gigabuffer.md — ChunkRenderInfo struct for binding 1]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List