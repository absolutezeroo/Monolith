# Story 6.3: Indirect Draw Buffer + ChunkRenderInfo SSBO

Status: ready-for-dev

## Story

As a developer,
I want GPU buffers for indirect draw commands and per-chunk render metadata,
so that the compute culling shader (Story 6.4) can fill them and drive `vkCmdDrawIndexedIndirectCount` with zero CPU overhead.

## Acceptance Criteria

1. `IndirectDrawBuffer` class owns two VkBuffers: an indirect command array (`VkDrawIndexedIndirectCommand[]`) and a draw count (`uint32_t`) — both STORAGE + INDIRECT + TRANSFER_DST usage.
2. `ChunkRenderInfoBuffer` class owns a HOST_VISIBLE SSBO holding `GpuChunkRenderInfo[]` (world position, gigabuffer offset, quad count, bounding sphere) — STORAGE usage.
3. `GpuChunkRenderInfo` struct has an explicit `std430`-compatible layout with a `static_assert(sizeof(...) == 48)`.
4. All buffers sized for `MAX_RENDERABLE_SECTIONS` (32768) chunks — sufficient for 20-chunk render distance.
5. Draw count reset to 0 per frame via `vkCmdFillBuffer`.
6. `ChunkRenderInfoBuffer` entries updated by `ChunkUploadManager` whenever chunks mesh or unmesh (via a stable slot index with free-list).
7. Descriptor set binding 1 written with `ChunkRenderInfoBuffer`, bindings 2–3 added to layout for `IndirectDrawBuffer` and draw count (preparation for Story 6.4).
8. Existing `renderChunks()` per-draw loop continues to work unchanged — new buffers are created and populated but not yet used for rendering.
9. RAII cleanup in `Renderer::shutdown()` follows correct destruction order (new buffers before DescriptorAllocator, after ImGuiBackend).
10. Zero Vulkan validation layer errors.

## Tasks / Subtasks

- [ ] **Task 1: Add constants to RendererConstants.h** (AC: #4)
  - [ ] Add `MAX_RENDERABLE_SECTIONS = 32768`
  - [ ] Add `SECTION_BOUNDING_RADIUS = sqrt(192.0f)` (~13.856, radius for 16³ section)
  - [ ] Add `INDIRECT_COMMAND_BUFFER_SIZE` and `CHUNK_RENDER_INFO_BUFFER_SIZE` computed from max sections

- [ ] **Task 2: Define GpuChunkRenderInfo struct** (AC: #3)
  - [ ] Add to `ChunkRenderInfo.h` (co-locate with existing CPU-side struct)
  - [ ] Layout: `vec4 boundingSphere` (xyz=center world, w=radius) | `vec4 worldBasePos` (xyz=pos, w=unused) | `uint32_t gigabufferOffset` | `uint32_t quadCount` | `uint32_t[2] pad` = **48 bytes**
  - [ ] `static_assert(sizeof(GpuChunkRenderInfo) == 48)` and `static_assert(offsetof(GpuChunkRenderInfo, quadCount) == 36)` to verify layout
  - [ ] Add helper `GpuChunkRenderInfo buildGpuInfo(const ChunkRenderInfo&)` that computes bounding sphere from worldBasePos

- [ ] **Task 3: Create IndirectDrawBuffer class** (AC: #1, #5)
  - [ ] New files: `IndirectDrawBuffer.h` / `IndirectDrawBuffer.cpp`
  - [ ] Owns two VkBuffers + VmaAllocations: command array and draw count
  - [ ] Factory: `static Result<unique_ptr<IndirectDrawBuffer>> create(VulkanContext&, uint32_t maxCommands)`
  - [ ] Command buffer: `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | STORAGE_BUFFER_BIT | TRANSFER_DST_BIT`, `VMA_MEMORY_USAGE_GPU_ONLY`
  - [ ] Count buffer: same usage flags, 4 bytes
  - [ ] `recordCountReset(VkCommandBuffer)`: calls `vkCmdFillBuffer(cmd, countBuffer, 0, 4, 0)`
  - [ ] Accessors: `getCommandBuffer()`, `getCountBuffer()`, `getMaxCommands()`, `getCommandBufferSize()`
  - [ ] RAII destructor destroys both buffers via `vmaDestroyBuffer`
  - [ ] Follow `QuadIndexBuffer` RAII pattern exactly (private ctor, static factory, delete copy/move)

- [ ] **Task 4: Create ChunkRenderInfoBuffer class** (AC: #2, #6)
  - [ ] New files: `ChunkRenderInfoBuffer.h` / `ChunkRenderInfoBuffer.cpp`
  - [ ] Factory: `static Result<unique_ptr<ChunkRenderInfoBuffer>> create(VulkanContext&, uint32_t maxSections)`
  - [ ] Buffer: `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, `VMA_MEMORY_USAGE_CPU_TO_GPU` (HOST_VISIBLE + DEVICE access), persistently mapped via `VMA_ALLOCATION_CREATE_MAPPED_BIT`
  - [ ] Slot allocation: simple free-list (`std::vector<uint32_t> m_freeSlots`) initialized with all indices [0..maxSections-1]
  - [ ] `allocateSlot() → Result<uint32_t>`: pops from free-list
  - [ ] `freeSlot(uint32_t)`: pushes back to free-list, zeros the slot in mapped memory (quadCount=0)
  - [ ] `update(uint32_t slotIndex, const GpuChunkRenderInfo&)`: writes directly to mapped pointer at `slotIndex * 48`
  - [ ] `getActiveCount()`: returns `maxSections - freeSlots.size()`
  - [ ] Accessor: `getBuffer()`, `getBufferSize()`
  - [ ] RAII destructor: `vmaDestroyBuffer`

- [ ] **Task 5: Update descriptor set layout and write bindings** (AC: #7)
  - [ ] In `Renderer::init()`, extend the `DescriptorLayoutBuilder` chain:
    - Binding 0: STORAGE_BUFFER, VERTEX_BIT (gigabuffer) — **unchanged**
    - Binding 1: STORAGE_BUFFER, VERTEX_BIT | COMPUTE_BIT (ChunkRenderInfo SSBO)
    - Binding 2: STORAGE_BUFFER, COMPUTE_BIT (indirect command buffer)
    - Binding 3: STORAGE_BUFFER, COMPUTE_BIT (draw count buffer)
  - [ ] Write all 4 bindings via `VkWriteDescriptorSet` array (binding 0 already written — update to batch write)
  - [ ] Remove the existing "binding 1 left unwritten" comment and its TODO

- [ ] **Task 6: Update Renderer to own and initialize new buffers** (AC: #8, #9)
  - [ ] Add `unique_ptr<IndirectDrawBuffer> m_indirectDrawBuffer` and `unique_ptr<ChunkRenderInfoBuffer> m_chunkRenderInfoBuffer` members
  - [ ] Create both in `init()` after Gigabuffer, before descriptor writes
  - [ ] Add accessors: `getIndirectDrawBuffer()`, `getMutableChunkRenderInfoBuffer()`
  - [ ] `shutdown()` destruction order: ImGuiBackend → StagingBuffer → new buffers → QuadIndexBuffer → Gigabuffer → DescriptorAllocator → pipeline/layout → frame resources
  - [ ] Forward-declare new classes in Renderer.h

- [ ] **Task 7: Integrate ChunkRenderInfoBuffer into ChunkUploadManager** (AC: #6)
  - [ ] Add `ChunkRenderInfoBuffer&` constructor parameter (non-owning reference)
  - [ ] Add `std::unordered_map<SectionKey, uint32_t, SectionKeyHash> m_slotMap` for SectionKey → GPU slot index
  - [ ] In `uploadSingle()`: after gigabuffer allocation succeeds, call `m_chunkRenderInfoBuffer.allocateSlot()`, store in `m_slotMap`, call `update()` with built `GpuChunkRenderInfo`
  - [ ] In `onChunkUnloaded()`: for each section, look up slot in `m_slotMap`, call `freeSlot()`, erase from map
  - [ ] When remeshing (existing allocation): reuse same slot index, just `update()` with new data
  - [ ] Update `GameApp.cpp` constructor to pass ChunkRenderInfoBuffer to ChunkUploadManager

- [ ] **Task 8: Build and validate** (AC: #10)
  - [ ] Compile with no warnings (`/W4 /WX`)
  - [ ] Run with Vulkan validation layers — zero errors/warnings
  - [ ] Verify chunks still render correctly (no visual regression from existing direct draw path)
  - [ ] Verify ChunkRenderInfoBuffer is populated (log slot count vs resident chunk count)

## Dev Notes

### Architecture Compliance

- **RAII pattern**: Follow `QuadIndexBuffer` / `Gigabuffer` exactly — private constructor, `static create()` factory returning `Result<unique_ptr<T>>`, deleted copy/move operators. [Source: architecture.md#Memory & Ownership]
- **Error handling**: Use `Result<T>` for factory methods. No exceptions. [Source: architecture.md#Error Handling]
- **Naming**: PascalCase classes, `m_` prefix members, camelCase methods. [Source: CLAUDE.md#Naming Conventions]
- **One class per file**: IndirectDrawBuffer and ChunkRenderInfoBuffer get separate .h/.cpp pairs. [Source: CLAUDE.md#Critical Rules]
- **Max 500 lines per file**: Both new classes should be well under this limit.

### Buffer Usage Flags — Critical

```
IndirectDrawBuffer (command array):
  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT   — vkCmdDrawIndexedIndirectCount reads from this
  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT    — compute shader writes to this (Story 6.4)
  VK_BUFFER_USAGE_TRANSFER_DST_BIT      — vkCmdFillBuffer for reset

IndirectDrawBuffer (count):
  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT   — vkCmdDrawIndexedIndirectCount reads count from this
  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT    — compute shader atomicAdd (Story 6.4)
  VK_BUFFER_USAGE_TRANSFER_DST_BIT      — vkCmdFillBuffer to reset to 0

ChunkRenderInfoBuffer:
  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT    — vertex shader reads worldPos, compute shader reads for culling
```

### GpuChunkRenderInfo Layout (std430)

```cpp
// C++ side — matches GLSL std430 exactly
struct GpuChunkRenderInfo
{
    glm::vec4 boundingSphere;   // offset 0:  xyz = center (world space), w = radius
    glm::vec4 worldBasePos;     // offset 16: xyz = section world origin, w = 0
    uint32_t gigabufferOffset;  // offset 32: byte offset into gigabuffer
    uint32_t quadCount;         // offset 36: number of quads
    uint32_t pad[2];            // offset 40: explicit padding to 48 bytes
};
static_assert(sizeof(GpuChunkRenderInfo) == 48);
```

```glsl
// GLSL side (cull.comp / chunk.vert in future stories)
struct ChunkRenderInfo {
    vec4 boundingSphere;    // xyz = center, w = radius
    vec4 worldBasePos;      // xyz = position, w = unused
    uint gigabufferOffset;  // byte offset into gigabuffer
    uint quadCount;         // number of quads
    uint _pad0;
    uint _pad1;
};
```

**Why vec4 instead of vec3 for worldBasePos**: vec3 in std430 has 16-byte alignment but 12-byte size, making cross-language layout verification fragile. Using vec4 guarantees identical C++/GLSL alignment with no ambiguity.

### Bounding Sphere Computation

Each chunk section is 16x16x16 blocks. For a section at world position `(wx, wy, wz)`:
- Center = `(wx + 8.0f, wy + 8.0f, wz + 8.0f)`
- Radius = `sqrt(8² + 8² + 8²)` = `sqrt(192)` ≈ 13.856

Implement as `buildGpuInfo()` helper that converts from existing `ChunkRenderInfo`:

```cpp
inline GpuChunkRenderInfo buildGpuInfo(const ChunkRenderInfo& info)
{
    const auto pos = glm::vec3(info.worldBasePos);
    const auto center = pos + glm::vec3(8.0f);
    return GpuChunkRenderInfo{
        .boundingSphere = glm::vec4(center, SECTION_BOUNDING_RADIUS),
        .worldBasePos = glm::vec4(pos, 0.0f),
        .gigabufferOffset = static_cast<uint32_t>(info.allocation.offset),
        .quadCount = info.quadCount,
        .pad = {0, 0}
    };
}
```

### Buffer Sizing

| Buffer | Formula | Size |
|--------|---------|------|
| Indirect commands | 32768 × 20 bytes | 640 KB |
| Draw count | 4 bytes | 4 bytes |
| ChunkRenderInfo SSBO | 32768 × 48 bytes | 1.5 MB |

These are trivially small compared to the Gigabuffer (256 MB) and QuadIndexBuffer (48 MB).

### VkDrawIndexedIndirectCommand Reference

```cpp
// Vulkan spec struct — do NOT redefine, use the Vulkan-provided type
struct VkDrawIndexedIndirectCommand {
    uint32_t indexCount;     // = quadCount * 6
    uint32_t instanceCount;  // = 1
    uint32_t firstIndex;     // = 0 (shared index buffer starts at 0)
    int32_t  vertexOffset;   // = gigabufferOffset / 2 (byte offset → vertex offset)
    uint32_t firstInstance;  // = slotIndex (used by gl_DrawID for per-draw data)
};
```

**vertexOffset math**: The gigabuffer stores uint32 pairs (8 bytes per quad, 4 vertices per quad). `vertexOffset = byteOffset / 8 * 4 = byteOffset / 2`. This is the existing calculation from `renderChunks()`.

### Descriptor Set Binding Map (Full Layout After This Story)

| Binding | Type | Stage | Buffer | Purpose |
|---------|------|-------|--------|---------|
| 0 | STORAGE_BUFFER | VERTEX | Gigabuffer | Quad data (vertex pulling) |
| 1 | STORAGE_BUFFER | VERTEX \| COMPUTE | ChunkRenderInfoBuffer | Per-chunk metadata (worldPos for shader, culling data for compute) |
| 2 | STORAGE_BUFFER | COMPUTE | IndirectDrawBuffer (commands) | Compute shader writes draw commands |
| 3 | STORAGE_BUFFER | COMPUTE | IndirectDrawBuffer (count) | Compute shader atomicAdd draw count |

### Slot Allocation Strategy

Use a **sparse** approach with free-list. Slots may have gaps (freed but not compacted). The compute shader (Story 6.4) will iterate all slots up to a known max and skip entries where `quadCount == 0`. This is simpler than dense packing and adequate for 32K max slots at 64 threads/workgroup (512 workgroups max).

### What This Story Does NOT Do

- Does NOT write the compute shader (`cull.comp`) — that is Story 6.4
- Does NOT change the `renderChunks()` draw loop to use indirect draw — that is Story 6.4
- Does NOT read from `ChunkRenderInfoBuffer` in the vertex shader via `gl_DrawID` — that is Story 6.4
- Does NOT add frustum culling — that is Story 6.4
- Current per-chunk `vkCmdDrawIndexed` + push constants rendering continues as-is

### Project Structure Notes

New files follow the established `engine/include/voxel/renderer/` + `engine/src/renderer/` mirror pattern:

```
engine/include/voxel/renderer/
  IndirectDrawBuffer.h          (NEW)
  ChunkRenderInfoBuffer.h       (NEW)
  ChunkRenderInfo.h             (MODIFY — add GpuChunkRenderInfo + buildGpuInfo)
  RendererConstants.h           (MODIFY — add MAX_RENDERABLE_SECTIONS)
  Renderer.h                    (MODIFY — add new buffer members + accessors)
  ChunkUploadManager.h          (MODIFY — add ChunkRenderInfoBuffer& + slot map)
engine/src/renderer/
  IndirectDrawBuffer.cpp        (NEW)
  ChunkRenderInfoBuffer.cpp     (NEW)
  Renderer.cpp                  (MODIFY — create/bind/destroy new buffers)
  ChunkUploadManager.cpp        (MODIFY — manage GPU slots on mesh/unmesh)
game/src/
  GameApp.cpp                   (MODIFY — pass ChunkRenderInfoBuffer to upload manager)
```

### Previous Story Intelligence

**From Story 6.0 (Descriptor Infrastructure):**
- `DescriptorLayoutBuilder` uses chained `.addBinding(binding, type, stageFlags)` → `.build(device)`. Extend existing chain, do not recreate.
- `DescriptorAllocator::allocate(layout)` returns `Result<VkDescriptorSet>`. Current descriptor set is allocated once in `Renderer::init()`.
- Binding 1 was declared in the layout but the descriptor write was intentionally skipped (comment: "left unwritten"). This story fills that gap.

**From Story 6.1 (QuadIndexBuffer):**
- RAII factory pattern: `static create(VulkanContext&) → Result<unique_ptr<T>>`. Private ctor. Uses VMA with `VMA_MEMORY_USAGE_GPU_ONLY`. One-time staging upload.
- Destruction: `vmaDestroyBuffer(m_allocator, m_buffer, m_allocation)` in destructor.

**From Story 6.2 (Vertex Pulling Shader — in progress):**
- Push constants: `ChunkPushConstants` is 80 bytes (mat4 VP + float time + vec3 chunkWorldPos). `chunkWorldPos` is set per-draw via push constants today. In Story 6.4 this will transition to `gl_DrawID` into the ChunkRenderInfo SSBO.
- Current draw call: `vkCmdDrawIndexed(cmd, quadCount*6, 1, 0, offset/2, 0)`.
- `shaderDrawParameters` device feature is already enabled (needed for `gl_DrawID`).

**From Story 5.7 (ChunkUploadManager):**
- `uploadSingle()` allocates gigabuffer space, stages via `StagingBuffer::uploadToGigabuffer()`, stores `ChunkRenderInfo` in `m_renderInfos` map.
- `onChunkUnloaded()` queues allocations for deferred free (waits FRAMES_IN_FLIGHT frames).
- Remesh path: free old allocation, upload new mesh, update render info in-place.

### Git Intelligence

Recent commits show consistent patterns:
- `feat(renderer): add QuadIndexBuffer for shared GPU index data`
- `feat(renderer): implement DescriptorAllocator and layout system`
- `feat(renderer): implement GPU-driven chunk rendering via vertex pulling`

Follow the same commit pattern: `feat(renderer): add IndirectDrawBuffer and ChunkRenderInfoBuffer for GPU-driven rendering`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-6.md — Story 6.3 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-009 Gigabuffer, Descriptor Set Layout, Indirect Rendering Pipeline]
- [Source: _bmad-output/project-context.md — Buffer Usage Flags, Memory & Ownership, Error Handling, Naming Conventions]
- [Source: engine/include/voxel/renderer/QuadIndexBuffer.h — RAII factory pattern reference]
- [Source: engine/include/voxel/renderer/Gigabuffer.h — VMA buffer creation pattern]
- [Source: engine/include/voxel/renderer/ChunkRenderInfo.h — Existing CPU-side structs]
- [Source: engine/include/voxel/renderer/ChunkUploadManager.h — Upload pipeline integration point]
- [Source: engine/include/voxel/renderer/Renderer.h — Current descriptor/buffer ownership]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
