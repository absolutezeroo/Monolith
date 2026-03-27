# Story 6.1: Shared Quad Index Buffer

Status: ready-for-dev

## Story

As a developer,
I want a single index buffer shared by all chunk draws,
so that I don't need per-chunk index buffers and all indirect draws reference the same quad tessellation pattern.

## Acceptance Criteria

1. **AC1 — Index pattern generation**: Pre-generated index buffer containing the pattern `{0,1,2, 2,3,0}` repeated for `MAX_QUADS` quads. For quad Q, indices are `{Q*4+0, Q*4+1, Q*4+2, Q*4+2, Q*4+3, Q*4+0}`.
2. **AC2 — GPU-resident buffer**: Uploaded once at init to a `DEVICE_LOCAL` VkBuffer with `VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT`.
3. **AC3 — Correct sizing**: Buffer sized as `MAX_QUADS * 6 * sizeof(uint32_t)`. Must use `uint32_t` indices (vertex IDs exceed 65535 for >16K quads).
4. **AC4 — MAX_QUADS configurable**: `MAX_QUADS` defined in `RendererConstants.h`, default `2'000'000` (2M). Sufficient for 16-chunk render distance (~26K sections, greedy meshing reduces average quad count drastically).
5. **AC5 — Single bind per frame**: Bound once via `vkCmdBindIndexBuffer` at frame start, reused by all subsequent indirect draw calls.
6. **AC6 — Renderer integration**: `Renderer` owns and manages the `QuadIndexBuffer` lifetime. Public accessor exposes `VkBuffer` and `getMaxQuads()` for Stories 6.2–6.4.
7. **AC7 — Zero validation errors**: No Vulkan validation layer errors or warnings from index buffer creation, upload, or binding.

## Tasks / Subtasks

- [ ] Task 1: Add `MAX_QUADS` constant to `RendererConstants.h` (AC: #4)
  - [ ] 1.1 Add `inline constexpr uint32_t MAX_QUADS = 2'000'000;` to `voxel::renderer` namespace
  - [ ] 1.2 Add `inline constexpr VkDeviceSize QUAD_INDEX_BUFFER_SIZE = MAX_QUADS * 6 * sizeof(uint32_t);` for convenience

- [ ] Task 2: Create `QuadIndexBuffer` class (AC: #1, #2, #3)
  - [ ] 2.1 Create header `engine/include/voxel/renderer/QuadIndexBuffer.h`
  - [ ] 2.2 Create implementation `engine/src/renderer/QuadIndexBuffer.cpp`
  - [ ] 2.3 Implement `static Result<std::unique_ptr<QuadIndexBuffer>> create(VulkanContext& context)` factory
  - [ ] 2.4 Generate index data CPU-side in a `std::vector<uint32_t>` inside `create()`
  - [ ] 2.5 Create DEVICE_LOCAL VkBuffer via VMA
  - [ ] 2.6 Upload via one-time staging buffer + command buffer (see Dev Notes)
  - [ ] 2.7 Implement RAII destructor to clean up VkBuffer + VmaAllocation

- [ ] Task 3: Integrate into Renderer (AC: #5, #6)
  - [ ] 3.1 Add `std::unique_ptr<QuadIndexBuffer> m_quadIndexBuffer` member to `Renderer`
  - [ ] 3.2 Create in `Renderer::init()` after Gigabuffer creation
  - [ ] 3.3 Add `bindIndexBuffer(VkCommandBuffer cmd)` method on QuadIndexBuffer
  - [ ] 3.4 Destroy in `Renderer::shutdown()` before VulkanContext resources
  - [ ] 3.5 Add public accessor `getQuadIndexBuffer()` on Renderer

- [ ] Task 4: Add to build system (AC: all)
  - [ ] 4.1 Add both `.h` and `.cpp` to `engine/CMakeLists.txt`

- [ ] Task 5: Verify zero validation errors (AC: #7)
  - [ ] 5.1 Run the application in debug mode, confirm no validation errors related to index buffer

## Dev Notes

### Class Design — Follow Gigabuffer Pattern Exactly

`QuadIndexBuffer` must mirror `Gigabuffer`'s structure:

```cpp
// QuadIndexBuffer.h
#pragma once
#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"
#include <vk_mem_alloc.h>
#include <volk.h>
#include <cstdint>
#include <memory>

namespace voxel::renderer
{
class VulkanContext;

class QuadIndexBuffer
{
  public:
    static core::Result<std::unique_ptr<QuadIndexBuffer>> create(VulkanContext& context);
    ~QuadIndexBuffer();

    QuadIndexBuffer(const QuadIndexBuffer&) = delete;
    QuadIndexBuffer& operator=(const QuadIndexBuffer&) = delete;
    QuadIndexBuffer(QuadIndexBuffer&&) = delete;
    QuadIndexBuffer& operator=(QuadIndexBuffer&&) = delete;

    void bind(VkCommandBuffer cmd) const;

    [[nodiscard]] VkBuffer getBuffer() const { return m_buffer; }
    [[nodiscard]] uint32_t getMaxQuads() const { return MAX_QUADS; }
    [[nodiscard]] uint32_t getIndexCount() const { return MAX_QUADS * 6; }

  private:
    QuadIndexBuffer() = default;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup
};
} // namespace voxel::renderer
```

### Index Data Generation

Generate all indices CPU-side before uploading. Straightforward loop:

```cpp
std::vector<uint32_t> indices(static_cast<size_t>(MAX_QUADS) * 6);
for (uint32_t q = 0; q < MAX_QUADS; ++q)
{
    uint32_t base = q * 4;
    size_t i = static_cast<size_t>(q) * 6;
    indices[i + 0] = base + 0;
    indices[i + 1] = base + 1;
    indices[i + 2] = base + 2;
    indices[i + 3] = base + 2;
    indices[i + 4] = base + 3;
    indices[i + 5] = base + 0;
}
```

Memory: 2M * 6 * 4 = 48 MB CPU-side temporary, freed after upload.

### One-Time Upload Pattern (DO NOT use StagingBuffer)

The existing `StagingBuffer` class is designed for per-frame streaming to the Gigabuffer (16 MB ring buffer, double-buffered). It is **not suitable** for a one-time 48 MB upload. Instead, use the standard Vulkan one-time upload pattern inside `QuadIndexBuffer::create()`:

```cpp
// 1. Create DEVICE_LOCAL destination buffer
VkBufferCreateInfo bufferCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
bufferCI.size = QUAD_INDEX_BUFFER_SIZE;
bufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocCI{};
allocCI.usage = VMA_MEMORY_USAGE_AUTO;
allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
// vmaCreateBuffer(...)

// 2. Create temporary HOST_VISIBLE staging buffer
VkBufferCreateInfo stagingCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
stagingCI.size = QUAD_INDEX_BUFFER_SIZE;
stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

VmaAllocationCreateInfo stagingAllocCI{};
stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                     | VMA_ALLOCATION_CREATE_MAPPED_BIT;
// vmaCreateBuffer(..., &stagingAlloc, &stagingAllocInfo);
// std::memcpy(stagingAllocInfo.pMappedData, indices.data(), dataSize);

// 3. One-time command buffer on graphics queue (or transfer queue)
VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
poolCI.queueFamilyIndex = context.getGraphicsQueueFamily();
// vkCreateCommandPool(...)
// vkAllocateCommandBuffers(...)
// vkBeginCommandBuffer(..., ONE_TIME_SUBMIT)
// vkCmdCopyBuffer(cmd, stagingBuffer, deviceBuffer, 1, &copyRegion)
// vkEndCommandBuffer(cmd)
// vkQueueSubmit2(graphicsQueue, ...) — use sync2 submit
// vkQueueWaitIdle(graphicsQueue) — safe at init time

// 4. Cleanup temporaries
// vkDestroyCommandPool(...)
// vmaDestroyBuffer(staging)
```

Use the **graphics queue** (not transfer queue) to avoid ownership transfer barriers. At init time, `vkQueueWaitIdle` is acceptable.

### VkBuffer Usage Flags

The index buffer **only** needs:
- `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` — required for `vkCmdBindIndexBuffer`
- `VK_BUFFER_USAGE_TRANSFER_DST_BIT` — required for staging upload

Do NOT add `STORAGE_BUFFER_BIT` or `SHADER_DEVICE_ADDRESS_BIT` — not needed for an index buffer.

### Bind Method

Simple single call, bound once per frame before any draw commands:

```cpp
void QuadIndexBuffer::bind(VkCommandBuffer cmd) const
{
    vkCmdBindIndexBuffer(cmd, m_buffer, 0, VK_INDEX_TYPE_UINT32);
}
```

### Renderer Integration Points

**In `Renderer.h`**: Add member and accessor:
```cpp
std::unique_ptr<QuadIndexBuffer> m_quadIndexBuffer;
[[nodiscard]] const QuadIndexBuffer* getQuadIndexBuffer() const { return m_quadIndexBuffer.get(); }
```

**In `Renderer::init()`**: Create after Gigabuffer (follows architecture init stack order):
```cpp
// 8. Gigabuffer (already exists)
auto gigaResult = Gigabuffer::create(m_vulkanContext);
m_gigabuffer = std::move(gigaResult.value());

// 9. Shared quad index buffer (NEW)
auto indexResult = QuadIndexBuffer::create(m_vulkanContext);
if (!indexResult.has_value())
{
    return std::unexpected(indexResult.error());
}
m_quadIndexBuffer = std::move(indexResult.value());
```

**In `Renderer::shutdown()`**: Destroy between StagingBuffer and Gigabuffer (line ~769):
```cpp
m_stagingBuffer.reset();      // existing
m_quadIndexBuffer.reset();    // NEW — after StagingBuffer, before Gigabuffer
m_gigabuffer.reset();         // existing
```

**In `Renderer::beginFrame()`**: The index buffer is NOT bound here yet — that happens in Story 6.2 when the chunk rendering draw call is wired up. For now, just ensure the buffer exists and is accessible.

### Destruction Order

The current `Renderer::shutdown()` order (from `Renderer.cpp:754`):
1. ImGuiBackend
2. StagingBuffer
3. Gigabuffer
4. SwapchainResources (depth image)
5. Pipelines + pipeline layout
6. Semaphores, fences, command pools

After this story, insert `QuadIndexBuffer` between StagingBuffer and Gigabuffer:
1. ImGuiBackend
2. StagingBuffer
3. **QuadIndexBuffer** (NEW)
4. Gigabuffer
5. SwapchainResources (depth image)
6. Pipelines + pipeline layout
7. Semaphores, fences, command pools

### CMakeLists.txt Addition

Add to the source file list in `engine/CMakeLists.txt` alongside existing renderer files:
```cmake
src/renderer/QuadIndexBuffer.cpp
```

Add to the header list (if tracked):
```cmake
include/voxel/renderer/QuadIndexBuffer.h
```

### What NOT To Do

- **Do NOT create a per-chunk index buffer** — the whole point is one shared buffer
- **Do NOT use the existing StagingBuffer** — it's too small (16 MB) and designed for per-frame streaming
- **Do NOT use uint16_t indices** — MAX_QUADS * 4 = 8M vertices, exceeds uint16 range
- **Do NOT add shader or descriptor code** — that's Stories 6.0 and 6.2
- **Do NOT bind the index buffer in the render loop yet** — Story 6.2 wires the draw call
- **Do NOT use BDA (Buffer Device Address) for this buffer** — standard index buffer binding suffices

### Unit Testing

Per the project testing strategy, GPU resource creation is NOT unit tested (use validation layers instead). Confirm:
- Application starts without validation errors
- QuadIndexBuffer reports correct `getMaxQuads()` and `getIndexCount()`
- Gigabuffer stats unaffected (buffers are independent)

Optionally, a CPU-only test can verify index generation logic:
```cpp
TEST_CASE("Quad index pattern is correct", "[renderer]")
{
    // Test a small subset — e.g., first 3 quads
    // Verify: {0,1,2,2,3,0, 4,5,6,6,7,4, 8,9,10,10,11,8}
}
```

### Project Structure Notes

Files follow existing renderer conventions exactly:
```
engine/
├── include/voxel/renderer/
│   ├── QuadIndexBuffer.h       ← CREATE (new file)
│   ├── Gigabuffer.h            (reference pattern)
│   ├── Renderer.h              ← MODIFY (add member + accessor)
│   └── RendererConstants.h     ← MODIFY (add MAX_QUADS)
└── src/renderer/
    ├── QuadIndexBuffer.cpp     ← CREATE (new file)
    ├── Gigabuffer.cpp          (reference pattern)
    └── Renderer.cpp            ← MODIFY (init + shutdown)
engine/CMakeLists.txt           ← MODIFY (add source file)
```

### Dependencies

- **Story 6.0 (Descriptor Infrastructure)**: Independent — 6.1 does not use descriptors. Both modify `Renderer.h` but touch separate members. If 6.0 is already implemented when 6.1 starts, the dev agent should work with the updated Renderer. If not, no conflict.
- **Story 6.2 (Vertex Pulling Shader)**: Depends on 6.1 — the shader uses the index buffer via `vkCmdDrawIndexedIndirect`. Story 6.2 will call `m_quadIndexBuffer->bind(cmd)` before draw calls.
- **Stories 5.6/5.7 (Async Mesh + Mesh Upload)**: Independent — those upload mesh data to Gigabuffer, not the index buffer.

### References

- [Source: architecture.md — System 5: Vulkan Renderer, Init Stack Step 9]
- [Source: architecture.md — GPU-Driven Rendering: Index Buffer Pattern]
- [Source: architecture.md — ADR-009: Gigabuffer Pattern (for VMA creation patterns)]
- [Source: epics/epic-6.md — Story 6.1 acceptance criteria]
- [Source: engine/include/voxel/renderer/Gigabuffer.h — RAII/factory pattern reference]
- [Source: engine/src/renderer/Gigabuffer.cpp — VMA buffer creation reference]
- [Source: engine/include/voxel/renderer/RendererConstants.h — constants location]
- [Source: project-context.md — naming conventions, error handling, code organization rules]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
| Date | Change |
|------|--------|
| 2026-03-27 | Story created by create-story workflow |

### File List