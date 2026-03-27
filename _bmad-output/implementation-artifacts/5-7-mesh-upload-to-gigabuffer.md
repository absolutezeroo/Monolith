# Story 5.7: Mesh Upload to Gigabuffer

Status: review

## Story

As a developer,
I want completed mesh data uploaded into the gigabuffer via the staging buffer,
so that the GPU can render chunks via indirect drawing.

## Why Now

Story 5.6 delivers async meshing: worker threads produce `ChunkMesh` results, and the main thread polls them into `ChunkManager::m_meshes`. But those meshes are CPU-side `std::vector<uint64_t>` — the GPU cannot see them. The Gigabuffer (256 MB DEVICE_LOCAL VkBuffer) and StagingBuffer (16 MB HOST_VISIBLE ring buffer) were built in Epic 2 but have never been used. This story bridges the gap: CPU mesh data goes into the gigabuffer with per-section render metadata tracked for indirect draw dispatch in Epic 6.

## Acceptance Criteria

1. **AC1 — ChunkRenderInfo**: A struct tracks per-section GPU state: gigabuffer allocation (offset+size+handle), quad count, section world base position, and a state enum (`None`, `Resident`, `PendingFree`).
2. **AC2 — Gigabuffer allocation on mesh ready**: When `ChunkManager::update()` stores a new mesh result, the upload system allocates `quadCount * 8` bytes in the gigabuffer via `Gigabuffer::allocate()`.
3. **AC3 — Staging upload**: Mesh data (`ChunkMesh::quads.data()`, `quadCount * sizeof(uint64_t)`) is copied into the staging buffer via `StagingBuffer::uploadToGigabuffer()` targeting the allocated gigabuffer offset. `flushTransfers()` is already called by `Renderer::endFrame()`.
4. **AC4 — Render info tracking**: A `ChunkRenderInfoMap` maps `(chunkCoord, sectionY)` to `ChunkRenderInfo`. The map is accessible for Epic 6 indirect draw dispatch.
5. **AC5 — Free on remesh**: When a section is re-meshed (new mesh replaces old), the old gigabuffer allocation is queued for deferred free (not freed immediately — GPU may still be reading it). After `FRAMES_IN_FLIGHT` (2) frames elapse, the deferred allocation is freed.
6. **AC6 — Free on unload**: When a chunk is unloaded, all its section allocations are queued for deferred free and the render infos are removed.
7. **AC7 — Upload budget**: At most `StagingBuffer::DEFAULT_MAX_UPLOADS` (8) uploads per frame. If more meshes are ready, defer remaining to the next frame via a pending upload queue.
8. **AC8 — Gigabuffer full handling**: If `Gigabuffer::allocate()` returns `OutOfMemory`, log a warning and skip the upload. The section has no render info (won't be drawn). Do not crash.
9. **AC9 — Empty mesh handling**: If a mesh result has `quadCount == 0` (empty section), do not allocate gigabuffer space. Store a `ChunkRenderInfo` with state `None` to prevent re-upload attempts.
10. **AC10 — Unit tests**: Deferred free queue FIFO ordering. ChunkRenderInfo lifecycle (allocate → resident → deferred free → freed). Upload queue draining across multiple frames. Empty mesh skip.

## Tasks / Subtasks

- [x] **Task 1: Create ChunkRenderInfo types** (AC: #1, #4)
  - [x] 1.1 Create `engine/include/voxel/renderer/ChunkRenderInfo.h`
  - [x] 1.2 Define `RenderState` enum: `None`, `Resident`, `PendingFree`
  - [x] 1.3 Define `ChunkRenderInfo` struct (see Design section)
  - [x] 1.4 Define `SectionKey` struct + hash for map key
  - [x] 1.5 Define `using ChunkRenderInfoMap = std::unordered_map<SectionKey, ChunkRenderInfo, SectionKeyHash>`

- [x] **Task 2: Create ChunkUploadManager** (AC: #2, #3, #4, #7, #8, #9)
  - [x] 2.1 Create `engine/include/voxel/renderer/ChunkUploadManager.h`
  - [x] 2.2 Create `engine/src/renderer/ChunkUploadManager.cpp`
  - [x] 2.3 Constructor takes `Gigabuffer&`, `StagingBuffer&` (non-owning references)
  - [x] 2.4 Implement `uploadMesh(SectionKey key, ChunkMesh&& mesh)` — allocates + stages
  - [x] 2.5 Implement `processUploads(ChunkManager& cm, const glm::dvec3& playerPos)` — polls new meshes from ChunkManager, queues uploads, drains upload queue up to budget
  - [x] 2.6 Implement `getRenderInfo(SectionKey key) -> const ChunkRenderInfo*` for Epic 6
  - [x] 2.7 Implement `getAllRenderInfos() -> const ChunkRenderInfoMap&` for indirect draw

- [x] **Task 3: Implement deferred free** (AC: #5, #6)
  - [x] 3.1 Add `DeferredFree` struct: `{ GigabufferAllocation alloc; uint32_t framesRemaining; }`
  - [x] 3.2 Add `std::vector<DeferredFree> m_deferredFrees` to ChunkUploadManager
  - [x] 3.3 Implement `queueDeferredFree(GigabufferAllocation alloc)` — pushes with `framesRemaining = FRAMES_IN_FLIGHT`
  - [x] 3.4 Implement `processDeferredFrees()` — called once per frame, decrements counters, frees allocations at 0
  - [x] 3.5 On remesh: queue old allocation for deferred free before allocating new
  - [x] 3.6 On unload: queue all section allocations for deferred free

- [x] **Task 4: Implement upload queue** (AC: #7)
  - [x] 4.1 Add `struct PendingUpload { SectionKey key; ChunkMesh mesh; }` to ChunkUploadManager
  - [x] 4.2 Add `std::vector<PendingUpload> m_pendingUploads` member
  - [x] 4.3 In `processUploads()`: new meshes go to pending queue first, then drain up to `maxUploadsPerFrame` from queue
  - [x] 4.4 Closer chunks (by distance to player) should be prioritized in the pending queue

- [x] **Task 5: Handle chunk unload cleanup** (AC: #6)
  - [x] 5.1 Add `onChunkUnloaded(glm::ivec2 coord)` method to ChunkUploadManager
  - [x] 5.2 Iterates all section indices (0-15), queues deferred frees for any Resident sections
  - [x] 5.3 Removes all SectionKey entries for that chunk from the render info map
  - [x] 5.4 Removes any pending uploads for that chunk from `m_pendingUploads`

- [x] **Task 6: Wire into GameApp** (AC: #2, #3)
  - [x] 6.1 Add `std::unique_ptr<voxel::renderer::ChunkUploadManager> m_uploadManager` to GameApp.h
  - [x] 6.2 In `GameApp::init()`, after renderer init: create ChunkUploadManager with references to renderer's Gigabuffer and StagingBuffer
  - [x] 6.3 In `GameApp::tick()`: call `m_uploadManager->processUploads(m_chunkManager, playerPos)` and `m_uploadManager->processDeferredFrees()`
  - [x] 6.4 Renderer needs to expose non-const Gigabuffer and StagingBuffer accessors (or pass them during init)
  - [x] 6.5 Member declaration order: `m_uploadManager` after `m_renderer` (depends on Renderer's Gigabuffer), before `m_chunkManager` in destruction order doesn't matter since ChunkUploadManager holds non-owning refs

- [x] **Task 7: Expose Renderer internals for upload** (AC: #2, #3)
  - [x] 7.1 Add `Gigabuffer* getMutableGigabuffer()` to Renderer (or pass Gigabuffer/StagingBuffer at ChunkUploadManager creation time)
  - [x] 7.2 Add `StagingBuffer* getMutableStagingBuffer()` to Renderer
  - [x] 7.3 These are non-owning pointers — Renderer retains ownership

- [x] **Task 8: Update debug overlay** (AC: #4)
  - [x] 8.1 In `GameApp::buildDebugOverlay()`, add: number of resident sections, pending uploads count, deferred frees count
  - [x] 8.2 Add accessor methods to ChunkUploadManager: `residentCount()`, `pendingUploadCount()`, `deferredFreeCount()`

- [x] **Task 9: Unit tests — ChunkRenderInfo and deferred free** (AC: #10)
  - [x] 9.1 Create `tests/renderer/TestChunkUpload.cpp`
  - [x] 9.2 Test: DeferredFree queue — add 3 allocations, tick 2 frames, verify all freed on correct frame
  - [x] 9.3 Test: ChunkRenderInfo lifecycle — None → Resident → PendingFree (via deferred) → freed
  - [x] 9.4 Test: Empty mesh (quadCount=0) stores None state, no gigabuffer allocation
  - [x] 9.5 Test: Upload queue drains N per frame — add 20 meshes, verify only 8 uploaded per processUploads call (using mock/fake Gigabuffer with VmaVirtualBlock only)
  - [x] 9.6 Test: Remesh — old allocation deferred-freed, new allocation created at different offset
  - [x] 9.7 Test: Unload removes all sections and queues deferred frees

- [x] **Task 10: Build system updates**
  - [x] 10.1 Add `src/renderer/ChunkUploadManager.cpp` to `engine/CMakeLists.txt`
  - [x] 10.2 Add `tests/renderer/TestChunkUpload.cpp` to `tests/CMakeLists.txt`
  - [x] 10.3 Verify all existing tests still pass

## Dev Notes

### Architecture Compliance

- **One class per file**: ChunkRenderInfo.h (types only), ChunkUploadManager.h/.cpp
- **Namespace**: `voxel::renderer` for all new types (mesh upload is a renderer concern)
- **Error handling**: No exceptions. `Gigabuffer::allocate()` returns `Result<GigabufferAllocation>` — check with `.has_value()`, log on failure, skip upload. `StagingBuffer::uploadToGigabuffer()` returns `Result<void>` — check similarly.
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members
- **Max 500 lines per file** — ChunkUploadManager should be well under this
- **Threading**: All upload operations happen on the main thread. Worker threads only produce `ChunkMesh` (Story 5.6). No mutex needed in ChunkUploadManager.

### Existing Code to Reuse — DO NOT REINVENT

- **`Gigabuffer::allocate(size, alignment=16)` → `Result<GigabufferAllocation>`**: Already implemented. Returns `{offset, size, handle}`. Alignment of 16 bytes is correct for SSBO access. [Source: engine/include/voxel/renderer/Gigabuffer.h:58]
- **`Gigabuffer::free(const GigabufferAllocation&)`**: Already implemented. CPU-only metadata operation. [Source: engine/include/voxel/renderer/Gigabuffer.h:61]
- **`StagingBuffer::uploadToGigabuffer(data, size, dstOffset)` → `Result<void>`**: Already implemented. Copies to HOST_VISIBLE ring buffer and records a `PendingTransfer`. Silently succeeds (no-op) if rate limit reached (`pendingTransfers.size() >= maxUploadsPerFrame`). [Source: engine/include/voxel/renderer/StagingBuffer.h:66]
- **`StagingBuffer::flushTransfers(gigabuffer)`**: Called automatically by `Renderer::endFrame()`. Batches all pending copies into one `vkCmdCopyBuffer`. [Source: engine/src/renderer/Renderer.cpp endFrame]
- **Transfer semaphore sync**: `Renderer::endFrame()` already makes the graphics queue wait on the transfer semaphore at `VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT` if any transfer occurred. No additional sync needed. [Source: engine/src/renderer/Renderer.cpp endFrame]
- **`StagingBuffer::beginFrame(frameIndex)`**: Called by `Renderer::beginFrame()`. Waits on per-frame fence, resets ring buffer. [Source: engine/src/renderer/StagingBuffer.cpp]
- **`ChunkMesh::quads`** / **`ChunkMesh::quadCount`**: The raw data to upload. Size = `quadCount * 8` bytes. [Source: engine/include/voxel/renderer/ChunkMesh.h:114-118]
- **`RendererConstants::FRAMES_IN_FLIGHT = 2`**: Use this for deferred free frame count. [Source: engine/include/voxel/renderer/RendererConstants.h]
- **`ChunkManager::m_meshes` (from Story 5.6)**: Maps `MeshKey{coord, sectionY}` → `ChunkMesh`. Story 5.7 consumes these meshes.
- **`ChunkManager::getMesh(coord, sectionY)` (from Story 5.6)**: Returns `const ChunkMesh*` or nullptr. Use to check for newly available meshes.
- **`GigabufferAllocation` struct**: Already defined with `{offset, size, handle}`. [Source: engine/include/voxel/renderer/Gigabuffer.h:17-22]
- **`Renderer::getGigabuffer()` → `const Gigabuffer*`**: Already exists (const). Need mutable accessor too. [Source: engine/include/voxel/renderer/Renderer.h:94]
- **Gigabuffer stats**: `usedBytes()`, `freeBytes()`, `allocationCount()` — already implemented for debug display. [Source: engine/include/voxel/renderer/Gigabuffer.h:63-65]

### What NOT To Do

- Do NOT create a custom allocator or VkBuffer — the Gigabuffer already exists and is the single buffer for all mesh data
- Do NOT create custom transfer command buffers — `StagingBuffer::uploadToGigabuffer()` handles all staging internally
- Do NOT call `vkCmdCopyBuffer` directly — `StagingBuffer::flushTransfers()` batches these
- Do NOT free gigabuffer allocations immediately on remesh — the GPU may still be reading the old data. Use deferred free (2 frames)
- Do NOT implement indirect draw commands, descriptor sets, or shaders — that's Epic 6
- Do NOT implement chunk loading/unloading spiral — that's a ChunkManager responsibility (already exists)
- Do NOT move mesh building to the upload manager — `ChunkManager::update()` (Story 5.6) handles mesh job polling. The upload manager only consumes finished meshes.
- Do NOT use `std::shared_ptr` — value semantics and unique_ptr only
- Do NOT bypass the staging buffer rate limit — if 8 uploads/frame is reached, queue the rest for next frame
- Do NOT hold raw `VkBuffer` handles beyond what Gigabuffer/StagingBuffer expose — use the abstraction layer

### ChunkRenderInfo Design

```cpp
// ChunkRenderInfo.h
#pragma once
#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/RendererConstants.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace voxel::renderer
{

enum class RenderState : uint8_t
{
    None,          // No mesh data (empty section or not yet uploaded)
    Resident,      // Mesh data in gigabuffer, ready for draw
    PendingFree,   // Old allocation queued for deferred free
};

/// GPU-side render metadata for one chunk section.
struct ChunkRenderInfo
{
    GigabufferAllocation allocation{};   // Gigabuffer sub-allocation
    uint32_t quadCount = 0;              // Number of quads (for draw command)
    glm::ivec3 worldBasePos{0};          // Section world origin (chunkX*16, sectionY*16, chunkZ*16)
    RenderState state = RenderState::None;
};

/// Key for identifying a chunk section in the render info map.
struct SectionKey
{
    glm::ivec2 chunkCoord{0, 0};
    int sectionY = 0;

    bool operator==(const SectionKey& other) const
    {
        return chunkCoord == other.chunkCoord && sectionY == other.sectionY;
    }
};

struct SectionKeyHash
{
    size_t operator()(const SectionKey& k) const noexcept
    {
        size_t h = std::hash<int>{}(k.chunkCoord.x);
        h ^= std::hash<int>{}(k.chunkCoord.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.sectionY) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

using ChunkRenderInfoMap = std::unordered_map<SectionKey, ChunkRenderInfo, SectionKeyHash>;

} // namespace voxel::renderer
```

### ChunkUploadManager Design

```cpp
// ChunkUploadManager.h
#pragma once
#include "voxel/renderer/ChunkRenderInfo.h"
#include "voxel/renderer/ChunkMesh.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace voxel::world { class ChunkManager; }

namespace voxel::renderer
{

class Gigabuffer;
class StagingBuffer;

class ChunkUploadManager
{
public:
    ChunkUploadManager(Gigabuffer& gigabuffer, StagingBuffer& stagingBuffer);

    /// Poll ChunkManager for new meshes, allocate gigabuffer space, stage uploads.
    /// Call once per frame from the main thread.
    void processUploads(world::ChunkManager& chunkManager, const glm::dvec3& playerPos);

    /// Tick deferred free queue. Call once per frame.
    void processDeferredFrees();

    /// Called when a chunk is unloaded — frees all section allocations (deferred).
    void onChunkUnloaded(glm::ivec2 coord);

    // Accessors for Epic 6 and debug overlay
    [[nodiscard]] const ChunkRenderInfoMap& getAllRenderInfos() const;
    [[nodiscard]] const ChunkRenderInfo* getRenderInfo(const SectionKey& key) const;
    [[nodiscard]] uint32_t residentCount() const;
    [[nodiscard]] uint32_t pendingUploadCount() const;
    [[nodiscard]] uint32_t deferredFreeCount() const;

private:
    struct PendingUpload
    {
        SectionKey key;
        ChunkMesh mesh;
        float distanceSq = 0.0f;  // For priority sorting
    };

    struct DeferredFree
    {
        GigabufferAllocation allocation;
        uint32_t framesRemaining = FRAMES_IN_FLIGHT;
    };

    bool uploadSingle(const SectionKey& key, const ChunkMesh& mesh);

    Gigabuffer& m_gigabuffer;
    StagingBuffer& m_stagingBuffer;

    ChunkRenderInfoMap m_renderInfos;
    std::vector<PendingUpload> m_pendingUploads;
    std::vector<DeferredFree> m_deferredFrees;
};

} // namespace voxel::renderer
```

### processUploads Flow

```
processUploads(chunkManager, playerPos):
  1. Scan chunkManager for new mesh results not yet in m_renderInfos
     (or whose mesh is newer than the current render info).
     For each new mesh:
       - If quadCount == 0: store ChunkRenderInfo{state=None}, skip upload
       - If existing Resident allocation: queue deferred free for old allocation
       - Add to m_pendingUploads with distance to player

  2. Sort m_pendingUploads by distanceSq (ascending = closest first)

  3. Drain m_pendingUploads up to MAX_UPLOADS_PER_FRAME:
     For each pending upload:
       - Call uploadSingle(key, mesh)
       - If upload succeeds: remove from queue
       - If gigabuffer full: log warning, remove from queue (skip this section)
       - If staging buffer full this frame: stop draining (try next frame)
```

### uploadSingle Flow

```
uploadSingle(key, mesh):
  1. auto allocResult = m_gigabuffer.allocate(mesh.quadCount * sizeof(uint64_t))
  2. If error (OutOfMemory):
       - VX_LOG_WARN("Gigabuffer full — cannot upload section ...")
       - Store ChunkRenderInfo{state=None} to prevent retry
       - Return false
  3. auto uploadResult = m_stagingBuffer.uploadToGigabuffer(
         mesh.quads.data(),
         mesh.quadCount * sizeof(uint64_t),
         allocResult->offset)
  4. If error (OutOfMemory — staging ring full):
       - Free the gigabuffer allocation (it was never written)
       - Return false (will retry next frame)
  5. Store ChunkRenderInfo{
       allocation = *allocResult,
       quadCount = mesh.quadCount,
       worldBasePos = ivec3(key.chunkCoord.x * 16, key.sectionY * 16, key.chunkCoord.y * 16),
       state = Resident
     }
  6. Return true
```

### Deferred Free Design

```
processDeferredFrees():
  Iterate m_deferredFrees:
    - Decrement framesRemaining
    - If framesRemaining == 0:
      - Call m_gigabuffer.free(alloc)
      - Remove from vector (swap-and-pop for O(1))
```

This ensures the GPU has finished reading the old data (2 frames of double-buffered rendering have elapsed) before the virtual allocation is released.

### World Base Position Calculation

Chunk coordinates are `glm::ivec2{chunkX, chunkZ}`. Section indices are `0-15`. The world base position of a section is:
```cpp
glm::ivec3 worldBasePos{
    key.chunkCoord.x * ChunkSection::SIZE,   // X
    key.sectionY * ChunkSection::SIZE,        // Y
    key.chunkCoord.y * ChunkSection::SIZE     // Z (note: ivec2.y = world Z)
};
```
**Critical**: `glm::ivec2` for chunk coordinates uses `.x` for world X and `.y` for world Z. This matches the existing `ChunkManager` convention. [Source: engine/include/voxel/world/ChunkManager.h — `ChunkCoordHash` uses `v.x` and `v.y`]

### Integration with Story 5.6 Output

Story 5.6 adds `ChunkManager::getMesh(coord, sectionY) -> const ChunkMesh*`. The upload manager calls this to discover new meshes. To detect "new" vs "already uploaded", the upload manager needs a way to know if a mesh has changed. Options:
- **Approach A (simple)**: Track a generation counter per mesh in ChunkManager. When mesh changes, generation increments. Upload manager stores last-uploaded generation.
- **Approach B (simpler)**: ChunkManager provides a list of sections that have new meshes since last poll. Add `drainNewMeshes() -> std::vector<MeshResult>` that moves completed meshes out.

**Recommended: Approach B** — ChunkManager already has `m_meshResults` (ConcurrentQueue) and `m_meshes` map. Add `consumeNewMeshes(std::vector<MeshReadyEntry>& out)` that fills a vector with `{key, ChunkMesh}` for sections updated since last consume, then clears the flag. This is clean, avoids double-scanning, and gives the upload manager ownership of the mesh data.

If the dev agent finds that Story 5.6 hasn't been implemented yet when starting this story, they should add `consumeNewMeshes()` to ChunkManager directly.

### GameApp Integration

```cpp
// GameApp.h — add member
#include "voxel/renderer/ChunkUploadManager.h"
std::unique_ptr<voxel::renderer::ChunkUploadManager> m_uploadManager;

// GameApp::init() — after m_renderer.init():
m_uploadManager = std::make_unique<voxel::renderer::ChunkUploadManager>(
    *m_renderer.getMutableGigabuffer(),
    *m_renderer.getMutableStagingBuffer());

// GameApp::tick(dt):
// After m_chunkManager.update(playerPos):
m_uploadManager->processUploads(m_chunkManager, m_camera.getPosition());
m_uploadManager->processDeferredFrees();

// When unloading chunks (future):
// m_uploadManager->onChunkUnloaded(coord);
```

**Destruction order**: `m_uploadManager` holds non-owning references to Gigabuffer/StagingBuffer (owned by m_renderer). It must be destroyed BEFORE m_renderer. Declare it AFTER m_renderer in the member list (C++ destroys members in reverse order):
```cpp
voxel::renderer::Renderer m_renderer;  // Destroyed LAST — owns Gigabuffer/StagingBuffer
std::unique_ptr<voxel::renderer::ChunkUploadManager> m_uploadManager;  // Destroyed before Renderer
```

### Renderer Modification

Add mutable accessors:
```cpp
// Renderer.h — add to public:
[[nodiscard]] Gigabuffer* getMutableGigabuffer() { return m_gigabuffer.get(); }
[[nodiscard]] StagingBuffer* getMutableStagingBuffer() { return m_stagingBuffer.get(); }
```
These are non-owning pointers. Renderer retains ownership via `unique_ptr`.

### Testing Strategy

Tests use `VmaVirtualBlock` directly (no GPU needed) to verify allocation/deferred-free logic. This matches the existing pattern in `tests/renderer/TestGigabuffer.cpp` which creates VmaVirtualBlocks without any Vulkan device.

```cpp
// Test helper — fake Gigabuffer using raw VmaVirtualBlock
// Tests allocate/free logic without needing a Vulkan device.
// ChunkUploadManager's upload path can be tested by:
//   1. Testing allocation + deferred free with VmaVirtualBlock (CPU-only)
//   2. Verifying processUploads correctly queues and drains
//   3. Mocking StagingBuffer is harder — test the queue/priority logic separately
```

The StagingBuffer requires Vulkan to construct, so tests should focus on:
- ChunkRenderInfo state machine (None → Resident, Resident → deferred free → None)
- DeferredFree queue timing (freed after exactly FRAMES_IN_FLIGHT ticks)
- PendingUpload queue ordering (closest first)
- Empty mesh handling (quadCount=0 → no allocation)
- Upload budget enforcement (max N per call)

### Debug Overlay Enhancement

Add to `buildDebugOverlay()`:
```cpp
if (m_uploadManager)
{
    ImGui::Text("GPU Sections: %u resident, %u pending, %u deferred-free",
        m_uploadManager->residentCount(),
        m_uploadManager->pendingUploadCount(),
        m_uploadManager->deferredFreeCount());
}
```

### File Structure

```
engine/include/voxel/renderer/
  ChunkRenderInfo.h            <- CREATE: SectionKey, ChunkRenderInfo, RenderState, map types
  ChunkUploadManager.h         <- CREATE: Upload orchestration class

engine/src/renderer/
  ChunkUploadManager.cpp       <- CREATE: Implementation

engine/include/voxel/renderer/
  Renderer.h                   <- MODIFY: Add getMutableGigabuffer(), getMutableStagingBuffer()

engine/include/voxel/world/
  ChunkManager.h               <- MODIFY: Add consumeNewMeshes() or equivalent mesh-ready API

game/src/
  GameApp.h                    <- MODIFY: Add m_uploadManager member
  GameApp.cpp                  <- MODIFY: Init + tick integration + debug overlay

engine/CMakeLists.txt          <- MODIFY: Add ChunkUploadManager.cpp to sources
tests/renderer/
  TestChunkUpload.cpp          <- CREATE: CPU-side upload manager tests
tests/CMakeLists.txt           <- MODIFY: Add TestChunkUpload.cpp
```

### Previous Story Intelligence

**From Story 5.6 (ready-for-dev) — what it produces:**
- `JobSystem` wrapping enkiTS (engine/include/voxel/core/JobSystem.h)
- `ConcurrentQueue<T>` (engine/include/voxel/core/ConcurrentQueue.h)
- `MeshJobInput`, `MeshResult`, `MeshChunkTask` (engine/include/voxel/renderer/MeshJobTypes.h)
- `ChunkManager::update(playerPos)` — polls mesh results, dispatches jobs
- `ChunkManager::getMesh(coord, sectionY)` — access completed meshes
- `MeshKey` struct + hash in ChunkManager for mesh storage
- Max 8 mesh results polled per frame, max 4 new dispatches per frame

**From Story 5.3b (in-progress) — quad format update:**
- Block state ID expanded from 10 to 16 bits (bits [30:45])
- Face direction moved to bits [46:48]
- AO corners at bits [49:56]
- Quad diagonal flip at bit [57]
- Total quad size remains 8 bytes (`uint64_t`)
- Upload size calculation: `quadCount * sizeof(uint64_t)` = `quadCount * 8`

**From Story 2.4/2.5 (done) — Gigabuffer + StagingBuffer:**
- Gigabuffer: 256 MB, VmaVirtualBlock, TLSF allocator, O(1) alloc/free
- StagingBuffer: 16 MB ring, 8 MB per frame, 8 uploads/frame default
- Transfer semaphore signaling + graphics wait already wired in Renderer::endFrame()
- Per-frame fence wait in StagingBuffer::beginFrame() ensures prior transfers complete

### Git Intelligence

Recent commits follow pattern: `feat(scope): description`. For this story:
- `feat(renderer): add ChunkRenderInfo types for GPU mesh tracking`
- `feat(renderer): implement ChunkUploadManager for mesh-to-gigabuffer upload`
- Or combined: `feat(renderer): implement mesh upload to gigabuffer via staging pipeline`

### Potential Pitfalls

1. **StagingBuffer rate limiting is silent**: `uploadToGigabuffer()` returns success even when rate-limited (it just doesn't record the copy). The upload manager must track its own count per frame to know when to stop. Check `stagingBuffer.pendingTransferCount()` before each upload.
2. **Gigabuffer alignment**: Gigabuffer allocations use 16-byte alignment by default. Quad data is 8 bytes per element. 16-byte alignment is correct (aligns to vec4 boundary for SSBO reads).
3. **Staging buffer capacity per frame**: Each frame gets 8 MB of staging space. A fully meshed section might have ~2000 quads × 8 bytes = 16 KB. 8 MB / 16 KB = 500 sections per frame — well within budget. Staging capacity is not the bottleneck; the 8-uploads-per-frame rate limit is.
4. **ChunkManager mesh ownership**: `getMesh()` returns a `const ChunkMesh*`. The upload manager needs to copy the quad data (via staging), not take ownership. After upload, the CPU-side mesh in ChunkManager can be discarded to save memory. Consider adding `ChunkManager::releaseMesh()` to free CPU memory after upload.
5. **Destruction order in GameApp**: `m_uploadManager` must be declared after `m_renderer` so it's destroyed first. If declared before, the Gigabuffer reference would dangle during destruction.

### Performance Expectations

| Metric | Expected | Notes |
|--------|----------|-------|
| Gigabuffer allocate | <1us | CPU-only VmaVirtualBlock TLSF |
| Gigabuffer free | <1us | CPU-only metadata |
| Staging memcpy (16 KB) | <5us | L1-resident ring buffer |
| processUploads (8 sections) | <50us | 8 allocates + 8 memcpys |
| processDeferredFrees (typical) | <10us | Vector scan + swap-pop |
| Memory per resident section | ~16 KB average | ~2000 quads × 8 bytes |
| Max sections in 256 MB | ~16,000 | Varies with mesh density |

### References

- [Source: engine/include/voxel/renderer/Gigabuffer.h — GigabufferAllocation, allocate(), free()]
- [Source: engine/src/renderer/Gigabuffer.cpp — VmaVirtualBlock allocation implementation]
- [Source: engine/include/voxel/renderer/StagingBuffer.h — uploadToGigabuffer(), flushTransfers(), beginFrame()]
- [Source: engine/src/renderer/StagingBuffer.cpp — Ring buffer, PendingTransfer, fence sync]
- [Source: engine/include/voxel/renderer/ChunkMesh.h — ChunkMesh{quads, quadCount}, 64-bit quad format]
- [Source: engine/include/voxel/renderer/Renderer.h — getGigabuffer(), beginFrame/endFrame, StagingBuffer ownership]
- [Source: engine/src/renderer/Renderer.cpp — flushTransfers called in endFrame, transfer semaphore wait]
- [Source: engine/include/voxel/renderer/RendererConstants.h — FRAMES_IN_FLIGHT = 2]
- [Source: engine/include/voxel/world/ChunkManager.h — getChunk(), setBlock(), ChunkCoordHash]
- [Source: game/src/GameApp.h — Member declaration order, destruction order]
- [Source: game/src/GameApp.cpp — init(), tick(), render(), buildDebugOverlay()]
- [Source: tests/renderer/TestGigabuffer.cpp — CPU-only VmaVirtualBlock test pattern]
- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md — Story 5.7 AC]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 5 Vulkan Renderer, ADR-009 Gigabuffer]
- [Source: _bmad-output/implementation-artifacts/5-6-async-mesh-jobs-via-enkits.md — MeshResult, getMesh(), update() flow]
- [Source: _bmad-output/project-context.md — Threading rules, naming conventions, FRAMES_IN_FLIGHT]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- Build fix: added missing `<algorithm>` include in TestChunkUpload.cpp
- Build fix: removed unused variable `a` in test cleanup loop
- Bug fix: `processDeferredFrees()` — changed to decrement-then-check pattern so items are freed after exactly FRAMES_IN_FLIGHT ticks (matching AC5)
- Design decision: Used `consumeNewMeshes()` on ChunkManager (Approach B from story notes) for clean mesh-ready signaling without generation counters

### Completion Notes List
- Created ChunkRenderInfo.h with RenderState enum, ChunkRenderInfo struct, SectionKey + hash, and ChunkRenderInfoMap typedef
- Created ChunkUploadManager (header + cpp) with full upload pipeline: processUploads, uploadSingle, deferred free, pending upload queue with distance-based priority, chunk unload cleanup
- Added `consumeNewMeshes()` API to ChunkManager for efficient new-mesh discovery
- Added `getMutableGigabuffer()` and `getMutableStagingBuffer()` to Renderer for upload system access
- Wired ChunkUploadManager into GameApp: creation in init(), processUploads + processDeferredFrees in tick(), debug overlay display
- 9 unit tests covering: SectionKey equality/hashing, ChunkRenderInfo defaults, map insert/lookup, deferred free FIFO timing, render info lifecycle, empty mesh handling, upload queue draining, remesh allocation replacement, unload cleanup
- All 482,958 assertions in 158 test cases pass with zero regressions

### Change Log
- 2026-03-27: Implemented Story 5.7 — Mesh Upload to Gigabuffer (all 10 tasks complete)

### File List
- engine/include/voxel/renderer/ChunkRenderInfo.h (NEW)
- engine/include/voxel/renderer/ChunkUploadManager.h (NEW)
- engine/src/renderer/ChunkUploadManager.cpp (NEW)
- engine/include/voxel/renderer/Renderer.h (MODIFIED — added getMutableGigabuffer, getMutableStagingBuffer)
- engine/include/voxel/world/ChunkManager.h (MODIFIED — added consumeNewMeshes, MeshReadyEntry, m_newMeshKeys)
- engine/src/world/ChunkManager.cpp (MODIFIED — added consumeNewMeshes impl, track new mesh keys in pollMeshResults)
- game/src/GameApp.h (MODIFIED — added m_uploadManager member, ChunkUploadManager include)
- game/src/GameApp.cpp (MODIFIED — create upload manager in init, call in tick, debug overlay display)
- engine/CMakeLists.txt (MODIFIED — added ChunkUploadManager.cpp)
- tests/CMakeLists.txt (MODIFIED — added TestChunkUpload.cpp)
- tests/renderer/TestChunkUpload.cpp (NEW)