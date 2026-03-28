# Story 6.4: Compute Culling Shader (cull.comp)

Status: review

## Story

As a developer,
I want a compute shader that tests chunk visibility and fills the indirect draw buffer,
so that only visible chunks are rendered with zero CPU overhead via `vkCmdDrawIndexedIndirectCount`.

## Acceptance Criteria

1. `cull.comp` compute shader: workgroup size 64, dispatched with `ceil(totalSections / 64)` groups.
2. Compute shader reads `ChunkRenderInfo[]` SSBO (binding 1), camera frustum planes (6 × vec4 push constant), and `totalSections` count.
3. Per chunk: skip if `quadCount == 0`; test bounding sphere against all 6 frustum planes.
4. If visible: `atomicAdd(drawCount, 1)` → fill `VkDrawIndexedIndirectCommand` at the atomically-acquired index.
5. Command fields: `indexCount = quadCount * 6`, `instanceCount = 1`, `firstIndex = 0`, `vertexOffset = int(gigabufferOffset / 2)`, `firstInstance = chunkSlotIndex`.
6. Pipeline barrier after compute: COMPUTE_SHADER write → DRAW_INDIRECT read + VERTEX_SHADER read.
7. `Renderer` creates a compute pipeline layout and compute pipeline for `cull.comp`.
8. `Renderer::renderChunks()` replaced with `Renderer::renderChunksIndirect()`: resets draw count, dispatches compute, barriers, calls `vkCmdDrawIndexedIndirectCount`.
9. `chunk.vert` updated: reads `chunkWorldPos` from ChunkRenderInfo SSBO (binding 1) via `gl_InstanceIndex` instead of push constants. Push constants reduced to `mat4 viewProjection` + `float time`.
10. `VulkanContext` enables `VkPhysicalDeviceVulkan12Features::drawIndirectCount = VK_TRUE`.
11. Validation: frustum-culled render matches CPU-rendered output (no popping when turning camera). Zero Vulkan validation errors.

## Tasks / Subtasks

- [x] **Task 1: Enable `drawIndirectCount` Vulkan feature** (AC: #10)
  - [x] In `VulkanContext.cpp`, add `features12.drawIndirectCount = VK_TRUE` to the `VkPhysicalDeviceVulkan12Features` struct (line ~118, after `descriptorIndexing`)
  - [x] Verify device creation still succeeds (all GTX 1660+ GPUs support this)

- [x] **Task 2: Add `ChunkRenderInfoBuffer::getHighWaterMark()` accessor** (AC: #1, #2)
  - [x] Add `uint32_t m_highWaterMark = 0` member to `ChunkRenderInfoBuffer`
  - [x] In `allocateSlot()`: update `m_highWaterMark = std::max(m_highWaterMark, slotIndex + 1)`
  - [x] Add `[[nodiscard]] uint32_t getHighWaterMark() const` accessor
  - [x] This tells the compute shader dispatch how many slots to iterate (avoids dispatching for all 32768 when only 200 are used)

- [x] **Task 3: Write `cull.comp` compute shader** (AC: #1, #2, #3, #4, #5)
  - [x] Create `assets/shaders/cull.comp` (GLSL 450, `local_size_x = 64`)
  - [x] Define push constants (128 bytes max):
    ```glsl
    layout(push_constant) uniform CullPushConstants {
        vec4 frustumPlanes[6]; // 96 bytes (6 × 16)
        uint totalSections;    // 4 bytes
        uint _pad[3];          // 12 bytes padding
    } pc;                      // 112 bytes total
    ```
  - [x] Bind SSBOs from shared descriptor set:
    - Binding 1 (read): `ChunkRenderInfo[]` — bounding sphere, world pos, gigabuffer offset, quad count
    - Binding 2 (write): `VkDrawIndexedIndirectCommand[]` — indirect command buffer
    - Binding 3 (read/write): `uint drawCount` — atomic counter
  - [x] Frustum culling: sphere-vs-6-planes test. Sphere is outside frustum if `dot(plane.xyz, center) + plane.w < -radius` for ANY plane
  - [x] If visible: `uint drawIdx = atomicAdd(drawCount, 1)` then write command at `drawIdx`:
    ```glsl
    commands[drawIdx].indexCount    = chunk.quadCount * 6;
    commands[drawIdx].instanceCount = 1;
    commands[drawIdx].firstIndex    = 0;
    commands[drawIdx].vertexOffset  = int(chunk.gigabufferOffset) / 2;
    commands[drawIdx].firstInstance = gl_GlobalInvocationID.x; // = slot index
    ```
  - [x] Compile to SPIR-V alongside existing shaders

- [x] **Task 4: Update `chunk.vert` to read chunkWorldPos from SSBO** (AC: #9)
  - [x] Add `ChunkRenderInfo` SSBO at binding 1:
    ```glsl
    struct ChunkRenderInfo {
        vec4 boundingSphere;
        vec4 worldBasePos;
        uint gigabufferOffset;
        uint quadCount;
        uint _pad0;
        uint _pad1;
    };
    layout(std430, set = 0, binding = 1) readonly buffer ChunkInfoSSBO {
        ChunkRenderInfo infos[];
    } chunkInfo;
    ```
  - [x] Replace `vec3 chunkWorldPos = vec3(pc.chunkWorldPosX, pc.chunkWorldPosY, pc.chunkWorldPosZ);` with `vec3 chunkWorldPos = chunkInfo.infos[gl_InstanceIndex].worldBasePos.xyz;`
  - [x] Update push constants to remove chunkWorldPos fields:
    ```glsl
    layout(push_constant) uniform PushConstants {
        mat4 viewProjection;  // 64 bytes
        float time;           // 4 bytes
    } pc;
    ```
  - [x] All other vertex shader logic (quad unpacking, corner reconstruction, AO, waving) remains identical

- [x] **Task 5: Update `ChunkPushConstants` C++ struct** (AC: #9)
  - [x] In `Renderer.h`, change `ChunkPushConstants` to:
    ```cpp
    struct ChunkPushConstants
    {
        glm::mat4 viewProjection; // 64 bytes
        float time;               // 4 bytes
        float _pad[3];            // 12 bytes padding to 80 bytes
    };
    static_assert(sizeof(ChunkPushConstants) == 80);
    ```
  - [x] Alternatively, reduce to 68 bytes if padding isn't needed — but keeping 80 avoids changing pipeline layout size. Preference: keep at 80 bytes for simplicity (backward-safe).
  - [x] Remove `chunkWorldPos` field — it's now read from SSBO

- [x] **Task 6: Create compute pipeline in Renderer** (AC: #7)
  - [x] Add `CullPushConstants` struct (matches GLSL):
    ```cpp
    struct CullPushConstants
    {
        glm::vec4 frustumPlanes[6]; // 96 bytes
        uint32_t totalSections;     // 4 bytes
        uint32_t pad[3];            // 12 bytes
    };
    static_assert(sizeof(CullPushConstants) == 112);
    ```
  - [x] Add members to `Renderer`:
    - `VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE`
    - `VkPipeline m_cullPipeline = VK_NULL_HANDLE`
  - [x] In `init()`, after graphics pipeline creation:
    - Create compute pipeline layout using **same** `m_chunkDescriptorSetLayout` but with COMPUTE_BIT push constant range (112 bytes)
    - Load `cull.comp.spv`, create compute pipeline via `VkComputePipelineCreateInfo`
  - [x] In `shutdown()`, destroy `m_cullPipeline` and `m_computePipelineLayout` alongside existing pipeline resources

- [x] **Task 7: Add `IndirectDrawBuffer` and `ChunkRenderInfoBuffer` to Renderer** (AC: #8)
  - [x] Add `std::unique_ptr<IndirectDrawBuffer> m_indirectDrawBuffer` and `std::unique_ptr<ChunkRenderInfoBuffer> m_chunkRenderInfoBuffer` members (if not already added by Story 6.3)
  - [x] Add accessors: `getIndirectDrawBuffer()`, `getMutableChunkRenderInfoBuffer()`
  - [x] These may already exist from Story 6.3 — if so, just verify they're available

- [x] **Task 8: Implement `renderChunksIndirect()`** (AC: #6, #8, #11)
  - [x] New method signature: `void renderChunksIndirect(const glm::mat4& viewProjection, const std::array<glm::vec4, 6>& frustumPlanes)`
  - [x] Implementation sequence:
    1. **Reset draw count** — `m_indirectDrawBuffer->recordCountReset(cmd)`
    2. **Barrier**: TRANSFER_DST → COMPUTE_SHADER (fill → compute read/write)
    3. **Bind compute pipeline** — `vkCmdBindPipeline(cmd, COMPUTE, m_cullPipeline)`
    4. **Bind descriptor set** for compute — same `m_chunkDescriptorSet`
    5. **Push compute constants** — frustum planes + totalSections (from `m_chunkRenderInfoBuffer->getHighWaterMark()`)
    6. **Dispatch** — `vkCmdDispatch(cmd, ceil(totalSections / 64), 1, 1)`
    7. **Barrier**: COMPUTE write → DRAW_INDIRECT read + VERTEX_SHADER read (memory barrier for indirect commands + ChunkRenderInfo SSBO)
    8. **Bind graphics pipeline** — `vkCmdBindPipeline(cmd, GRAPHICS, m_pipeline)` (or wireframe)
    9. **Bind descriptor set** for graphics — same set
    10. **Bind quad index buffer**
    11. **Push graphics constants** — VP matrix + time (no chunkWorldPos)
    12. **Indirect draw** — `vkCmdDrawIndexedIndirectCount(cmd, commandBuffer, 0, countBuffer, 0, MAX_RENDERABLE_SECTIONS, sizeof(VkDrawIndexedIndirectCommand))`
  - [x] Update `m_lastDrawCount` and `m_lastQuadCount` — read back from GPU is expensive; for debug overlay use `getHighWaterMark()` as upper bound or defer readback. Simplest V1: display "indirect" in overlay, skip exact count.

- [x] **Task 9: Wire `renderChunksIndirect()` into GameApp** (AC: #8, #11)
  - [x] In `GameApp::render()`, replace:
    ```cpp
    m_renderer.renderChunks(m_uploadManager->getAllRenderInfos(), vp);
    ```
    with:
    ```cpp
    auto frustumPlanes = m_camera.extractFrustumPlanes();
    m_renderer.renderChunksIndirect(vp, frustumPlanes);
    ```
  - [x] Remove the `renderChunks()` call entirely (keep method for potential fallback, mark deprecated)
  - [x] Update debug overlay: change "Draw calls" stat to show indirect mode info

- [x] **Task 10: Compile shaders and validate** (AC: #11)
  - [x] Compile `cull.comp` to `cull.comp.spv` (add to shader compilation script/CMake if exists)
  - [x] Compile updated `chunk.vert` to `chunk.vert.spv`
  - [x] Build with `/W4 /WX` — zero warnings
  - [x] Run with Vulkan validation layers — zero errors
  - [ ] Visual validation: chunks render correctly, no popping when turning camera, culled chunks disappear at frustum edges

## Dev Notes

### Architecture Compliance

- **RAII pattern**: Compute pipeline and layout destroyed in `shutdown()` alongside existing graphics pipeline. [Source: architecture.md#Memory & Ownership]
- **Error handling**: `Result<void>` for init methods. Compute pipeline creation errors are fatal. [Source: project-context.md#Error Handling]
- **Naming**: PascalCase structs (`CullPushConstants`), camelCase methods (`renderChunksIndirect`), `m_` prefix members. [Source: CLAUDE.md#Naming Conventions]
- **Shader files**: `assets/shaders/cull.comp` follows existing pattern alongside `chunk.vert` and `chunk.frag`. [Source: architecture.md#Project Tree]

### Frustum Culling Algorithm (Sphere vs. 6 Planes)

```glsl
bool isVisible(vec4 sphere, vec4 planes[6])
{
    vec3 center = sphere.xyz;
    float radius = sphere.w;
    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius)
            return false; // entirely outside this plane
    }
    return true;
}
```

`Camera::extractFrustumPlanes()` already returns normalized planes using Gribb-Hartmann extraction on the VP matrix. The planes are `[nx, ny, nz, d]` where `dot(n, point) + d < 0` means the point is outside. Sphere test: if `dot(n, center) + d < -radius`, the entire sphere is outside.

### vertexOffset Calculation — Corrected Formula

The epic states `vertexOffset = gigabufferOffset * 4` but the **correct formula** based on the existing `renderChunks()` at `Renderer.cpp:764` is:

```
vertexOffset = int(gigabufferOffset / 2)
```

**Derivation**: Each quad = 8 bytes (2 × uint32) = 4 vertices. `gl_VertexIndex = indexValue + vertexOffset`. The shader computes `quadIndex = gl_VertexIndex / 4`, then reads `gigabuffer.data[quadIndex * 2]`. For byte offset `B`:
- Quad offset = `B / 8`
- Need `vertexOffset / 4 = B / 8` → `vertexOffset = B / 2`

Gigabuffer allocates with 16-byte alignment, so `gigabufferOffset` is always even. Integer division is exact.

### `firstInstance` = Chunk Slot Index

The compute shader writes `firstInstance = gl_GlobalInvocationID.x` (the slot index into `ChunkRenderInfo[]`). In the vertex shader:
- `gl_InstanceIndex = firstInstance + instanceID`
- With `instanceCount = 1`, `instanceID = 0`, so `gl_InstanceIndex = firstInstance = slotIndex`
- Vertex shader reads `chunkInfo.infos[gl_InstanceIndex].worldBasePos.xyz`

`shaderDrawParameters` feature (Vulkan 1.1) is already enabled in `VulkanContext.cpp:123`.

### Pipeline Barrier Sequence Per Frame

```
1. vkCmdFillBuffer(countBuffer, 0, 4, 0)        — reset draw count to 0
2. Memory barrier: TRANSFER → COMPUTE             — fill completes before compute reads
3. vkCmdDispatch(ceil(totalSections/64), 1, 1)   — compute culling
4. Memory barrier: COMPUTE → DRAW_INDIRECT + VERTEX
   srcStage:  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
   srcAccess: VK_ACCESS_2_SHADER_WRITE_BIT
   dstStage:  VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
   dstAccess: VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT
5. vkCmdDrawIndexedIndirectCount(...)             — GPU-driven rendering
```

Use `VkMemoryBarrier2` with synchronization2 (Vulkan 1.3 core, already used in codebase).

### Compute Push Constants vs. UBO

Push constants chosen for frustum planes because:
- Only 112 bytes needed (well within 128-byte guaranteed minimum)
- Updated every frame (not worth a UBO allocation)
- Zero allocation overhead vs buffer upload
- Standard pattern for per-frame compute dispatch data

### `vkCmdDrawIndexedIndirectCount` Parameters

```cpp
vkCmdDrawIndexedIndirectCount(
    cmd,
    m_indirectDrawBuffer->getCommandBuffer(),  // buffer with VkDrawIndexedIndirectCommand[]
    0,                                          // offset into command buffer
    m_indirectDrawBuffer->getCountBuffer(),     // buffer with uint32_t draw count
    0,                                          // offset into count buffer
    MAX_RENDERABLE_SECTIONS,                    // maxDrawCount (GPU clamps to this)
    sizeof(VkDrawIndexedIndirectCommand)         // stride (20 bytes)
);
```

### Draw Count Stats After Indirect

With indirect draw, the CPU no longer knows the exact draw count (the GPU decides). Options:
1. **V1 (this story)**: Display `highWaterMark` as "sections" and "indirect" as draw mode in overlay. No GPU readback.
2. **Future**: Use a GPU timestamp query or pipeline statistics query for draw count.

### Vulkan Feature Enablement — Critical

`VulkanContext.cpp` must add to `features12`:
```cpp
features12.drawIndirectCount = VK_TRUE;
```
Without this, `vkCmdDrawIndexedIndirectCount` will crash or produce validation errors. This feature is supported on all GPUs that support Vulkan 1.2+ (GTX 1060+, RX 480+).

### Descriptor Set Sharing

Both the compute pipeline and graphics pipeline bind the **same** `m_chunkDescriptorSet`. The descriptor set layout already includes all 4 bindings with appropriate stage flags (Story 6.3 sets binding 1 to VERTEX | COMPUTE, bindings 2-3 to COMPUTE). This means:
- One `vkCmdBindDescriptorSets` per pipeline bind point (COMPUTE + GRAPHICS)
- No descriptor set switching or duplication
- Bindings 0 (Gigabuffer) and 1 (ChunkRenderInfo) are read by both stages

### What This Story Does NOT Do

- Does NOT implement distance-based culling or LOD — frustum only
- Does NOT change the fragment shader (`chunk.frag`)
- Does NOT add texture array sampling — that is Story 6.5
- Does NOT implement transparent/translucent pass separation — that is Story 6.7
- Does NOT add GPU readback for exact draw count stats — V1 uses highWaterMark estimate

### Project Structure Notes

New and modified files follow the established mirror pattern:

```
assets/shaders/
  cull.comp                       (NEW — compute culling shader)
  chunk.vert                      (MODIFY — read chunkWorldPos from SSBO, reduce push constants)
engine/include/voxel/renderer/
  Renderer.h                      (MODIFY — add compute pipeline members, CullPushConstants, renderChunksIndirect)
  ChunkRenderInfoBuffer.h         (MODIFY — add getHighWaterMark())
engine/src/renderer/
  Renderer.cpp                    (MODIFY — create compute pipeline, implement renderChunksIndirect)
  VulkanContext.cpp               (MODIFY — enable drawIndirectCount feature)
game/src/
  GameApp.cpp                     (MODIFY — call renderChunksIndirect with frustum planes)
```

### Previous Story Intelligence

**From Story 6.3 (IndirectDrawBuffer + ChunkRenderInfoBuffer — ready-for-dev):**
- `IndirectDrawBuffer` owns command array + count buffer with INDIRECT + STORAGE + TRANSFER_DST usage
- `IndirectDrawBuffer::recordCountReset(cmd)` calls `vkCmdFillBuffer(cmd, countBuffer, 0, 4, 0)`
- `ChunkRenderInfoBuffer` is HOST_VISIBLE, persistently mapped, with free-list slot allocation
- `GpuChunkRenderInfo` is 48 bytes std430-aligned: `vec4 boundingSphere | vec4 worldBasePos | uint gigabufferOffset | uint quadCount | uint[2] pad`
- Descriptor bindings: 0=Gigabuffer, 1=ChunkRenderInfo (VERTEX|COMPUTE), 2=IndirectCommands (COMPUTE), 3=DrawCount (COMPUTE)
- Slot allocation is sparse — freed slots have `quadCount == 0`, compute shader skips them
- `ChunkUploadManager` manages slot map: `SectionKey → uint32_t slotIndex`

**From Story 6.2 (Vertex Pulling — in progress):**
- Push constants are `ChunkPushConstants` (80 bytes): `mat4 VP + float time + vec3 chunkWorldPos`
- `chunkWorldPos` is set per-draw via push constants — this story transitions it to SSBO read
- `shaderDrawParameters` already enabled (needed for `gl_DrawID` / `gl_InstanceIndex`)
- Current draw call: `vkCmdDrawIndexed(cmd, quadCount*6, 1, 0, offset/2, 0)` per chunk

**From Story 6.0 (Descriptor Infrastructure):**
- `DescriptorLayoutBuilder` uses chained `.addBinding()` → `.build(device)`
- `DescriptorAllocator::allocate(layout)` returns `Result<VkDescriptorSet>`
- Single descriptor set allocated once in `Renderer::init()`

**From Story 6.1 (QuadIndexBuffer):**
- Shared index buffer: `{0,1,2, 2,3,0}` pattern for MAX_QUADS
- Bound once before all draws — unchanged by this story

### Git Intelligence

Recent commits follow consistent `feat(renderer):` pattern:
```
140113a chore: finalize Story 6.0 and update shaders/render states
3ee030d fix(renderer): update winding order and apply Vulkan Y-flip adjustment
bd3e207 feat(renderer): implement GPU-driven chunk rendering via vertex pulling
4ff70e3 feat(renderer): add QuadIndexBuffer for shared GPU index data
c42d31d feat(renderer): implement DescriptorAllocator and layout system
```

Commit for this story: `feat(renderer): implement compute frustum culling and indirect draw`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.4 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-009 Gigabuffer, Indirect Rendering Pipeline, Culling Strategies]
- [Source: _bmad-output/project-context.md — Buffer Usage Flags, Memory & Ownership, Error Handling]
- [Source: engine/include/voxel/renderer/Camera.h — extractFrustumPlanes() method (Gribb-Hartmann)]
- [Source: engine/include/voxel/renderer/Renderer.h — ChunkPushConstants struct, current pipeline layout]
- [Source: engine/src/renderer/Renderer.cpp:720-786 — Current per-draw renderChunks() implementation]
- [Source: engine/src/renderer/VulkanContext.cpp:110-136 — Current Vulkan feature enablement (missing drawIndirectCount)]
- [Source: assets/shaders/chunk.vert — Current push constants layout, chunkWorldPos usage at line 188]
- [Source: _bmad-output/implementation-artifacts/6-3-indirect-draw-buffer-chunkrenderinfo-ssbo.md — Full Story 6.3 specification]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
