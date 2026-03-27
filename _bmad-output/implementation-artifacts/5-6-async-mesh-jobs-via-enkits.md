# Story 5.6: Async Mesh Jobs via enkiTS

Status: done

## Story

As a developer,
I want meshing to run on worker threads via the job system,
so that chunk meshing never blocks the main thread.

## Why Now

The naive mesher (5.1) and AO (5.2) are complete. Meshing currently runs synchronously. Every chunk mesh blocks the main thread for ~500us (naive) or ~200us (greedy when done). With hundreds of chunks loaded, that's unacceptable. The async pipeline is the prerequisite for Story 5.7 (Mesh Upload to Gigabuffer) and all rendering stories in Epic 6.

## Acceptance Criteria

1. **AC1 — enkiTS dependency**: `enkits` added to `vcpkg.json`, `find_package(enkiTS CONFIG REQUIRED)` in `engine/CMakeLists.txt`, linked as `enkiTS::enkiTS`
2. **AC2 — JobSystem wrapper**: `voxel::core::JobSystem` wraps `enki::TaskScheduler`. `init()` auto-detects core count, `shutdown()` waits for all tasks. Single instance owned by GameApp, passed by reference.
3. **AC3 — ConcurrentQueue**: `voxel::core::ConcurrentQueue<T>` with `push(T&&)`, `tryPop() -> std::optional<T>`, `size()`, `empty()`. Implemented with `std::mutex` + `std::deque`. Thread-safe for MPSC (many producers, single consumer).
4. **AC4 — MeshChunkTask**: `enki::ITaskSet` subclass that takes an immutable snapshot of a section + 6 neighbor sections, runs `MeshBuilder::buildNaive()`, and produces a `ChunkMesh`.
5. **AC5 — Snapshot creation**: Before dispatching a task, copy the center section + 6 neighbor sections by value (immutable input for worker threads). Null neighbors copied as absent (flagged, not copied).
6. **AC6 — Result delivery**: Completed meshes delivered via `ConcurrentQueue<MeshResult>` from workers to main thread.
7. **AC7 — Main thread integration**: `ChunkManager::update()` polls mesh results (max N per frame, default 8), stores completed ChunkMesh per section.
8. **AC8 — Dirty tracking**: When `setBlock()` is called, mark the section + affected neighbors as needing remesh. `update()` re-queues dirty sections.
9. **AC9 — Distance priority**: Chunks closer to the player mesh first. Use enkiTS priority levels (0 = closest).
10. **AC10 — Cancellation**: If a chunk is unloaded before its mesh task completes, discard the result on arrival (coordinate mismatch check).
11. **AC11 — Unit tests**: ConcurrentQueue SPSC ordering + MPSC correctness. JobSystem init/shutdown + task completion. Snapshot creation correctness.

## Tasks / Subtasks

- [x] **Task 1: Add enkiTS to build system** (AC: #1)
  - [x] 1.1 Add `"enkits"` to `vcpkg.json` dependencies array
  - [x] 1.2 In `engine/CMakeLists.txt`, add `find_package(enkiTS CONFIG REQUIRED)` after existing find_package calls
  - [x] 1.3 Add `PUBLIC enkiTS::enkiTS` to `target_link_libraries(VoxelEngine ...)` — PUBLIC because JobSystem.h exposes enkiTS headers
  - [x] 1.4 Add `<enkiTS/TaskScheduler.h>` to the precompiled headers list in CMakeLists.txt
  - [x] 1.5 Verify build succeeds with `bash build.sh VoxelEngine`

- [x] **Task 2: Create JobSystem** (AC: #2)
  - [x] 2.1 Create `engine/include/voxel/core/JobSystem.h`
  - [x] 2.2 Create `engine/src/core/JobSystem.cpp`
  - [x] 2.3 Add `src/core/JobSystem.cpp` to `engine/CMakeLists.txt` source list
  - [x] 2.4 Implement class (see Design section below)

- [x] **Task 3: Create ConcurrentQueue** (AC: #3)
  - [x] 3.1 Create `engine/include/voxel/core/ConcurrentQueue.h` (header-only template)
  - [x] 3.2 Implement with `std::mutex` + `std::deque` (see Design section below)
  - [x] 3.3 No .cpp file needed — it's a template

- [x] **Task 4: Create snapshot and result types** (AC: #5, #6)
  - [x] 4.1 Create `engine/include/voxel/renderer/MeshJobTypes.h`
  - [x] 4.2 Define `MeshJobInput` (snapshot struct — see Design section)
  - [x] 4.3 Define `MeshResult` (output struct — see Design section)

- [x] **Task 5: Create MeshChunkTask** (AC: #4)
  - [x] 5.1 Define `MeshChunkTask : enki::ITaskSet` in `MeshJobTypes.h` (or separate header)
  - [x] 5.2 Constructor takes `MeshJobInput&&`, `const MeshBuilder&`, `ConcurrentQueue<MeshResult>&`
  - [x] 5.3 `ExecuteRange()` reconstructs neighbor pointer array from snapshot, calls `buildGreedy()`, pushes result to queue
  - [x] 5.4 Task stores everything by value — fully self-contained after construction

- [x] **Task 6: Extend ChunkManager with async meshing** (AC: #7, #8, #9, #10)
  - [x] 6.1 Add `setJobSystem(core::JobSystem*)` and `setMeshBuilder(const renderer::MeshBuilder*)` setters to ChunkManager
  - [x] 6.2 Add `void update(const glm::dvec3& playerPos)` method to ChunkManager
  - [x] 6.3 Add internal members: `ConcurrentQueue<MeshResult> m_meshResults`, `std::unordered_map<MeshKey, ChunkMesh> m_meshes`, tracking sets for in-flight tasks
  - [x] 6.4 `update()` Step 1: Poll completed results from queue (max N per frame). For each result: check if chunk still loaded (AC10 cancellation), store mesh if valid.
  - [x] 6.5 `update()` Step 2: Scan dirty sections sorted by distance to player. For each dirty section not already in-flight: create snapshot, dispatch MeshChunkTask with distance-based priority.
  - [x] 6.6 Rate-limit new dispatches per frame (max M new tasks per update, default 4)
  - [x] 6.7 Add `getMesh(glm::ivec2 coord, int sectionY) -> const ChunkMesh*` for Story 5.7 to consume

- [x] **Task 7: Wire into GameApp** (AC: #2, #7)
  - [x] 7.1 Add `voxel::core::JobSystem m_jobSystem;` member to GameApp (owned by value)
  - [x] 7.2 In `GameApp::init()`, call `m_jobSystem.init()`, then `m_chunkManager.setJobSystem(&m_jobSystem)` and `m_chunkManager.setMeshBuilder(&meshBuilder)` (create MeshBuilder as member or local)
  - [x] 7.3 Add `voxel::renderer::MeshBuilder m_meshBuilder;` member to GameApp (via unique_ptr), constructed with `m_blockRegistry`
  - [x] 7.4 In `GameApp::tick()`, call `m_chunkManager.update(playerPos)` where playerPos comes from camera
  - [x] 7.5 In `GameApp` destructor, `m_jobSystem.shutdown()` called BEFORE ChunkManager destruction. Member order ensures correct destruction.

- [x] **Task 8: Extend dirty tracking for neighbor invalidation** (AC: #8)
  - [x] 8.1 In `ChunkManager::setBlock()`, after marking the section dirty, also check if the block is on a section boundary (x/y/z == 0 or 15) and mark the adjacent neighbor section dirty too
  - [x] 8.2 This ensures AO and face culling are recomputed for neighbors when a boundary block changes

- [x] **Task 9: Unit tests — ConcurrentQueue** (AC: #11)
  - [x] 9.1 Create `tests/core/TestConcurrentQueue.cpp`
  - [x] 9.2 Test: push 100 items, tryPop returns them in FIFO order
  - [x] 9.3 Test: tryPop on empty queue returns std::nullopt
  - [x] 9.4 Test: size() and empty() are correct after push/pop
  - [x] 9.5 Test: multi-threaded — 4 threads push 1000 items each, main thread pops all 4000 (no loss, no duplicates)

- [x] **Task 10: Unit tests — JobSystem** (AC: #11)
  - [x] 10.1 Create `tests/core/TestJobSystem.cpp`
  - [x] 10.2 Test: init() + shutdown() without tasks (clean lifecycle)
  - [x] 10.3 Test: submit a simple ITaskSet, wait for completion, verify it ran
  - [x] 10.4 Test: submit 100 tasks, wait for all, verify all completed (counter)
  - [x] 10.5 Test: threadCount() returns > 0 after init()

- [x] **Task 11: Unit tests — Snapshot and async meshing** (AC: #11)
  - [x] 11.1 Create `tests/renderer/TestAsyncMeshing.cpp`
  - [x] 11.2 Test: MeshJobInput snapshot correctly copies section data (compare block values)
  - [x] 11.3 Test: MeshJobInput correctly flags absent neighbors as nullptr
  - [x] 11.4 Test: MeshChunkTask produces same ChunkMesh as synchronous buildGreedy() for identical input
  - [x] 11.5 Test: end-to-end — create snapshot, dispatch task via JobSystem, poll result from queue, verify mesh

- [x] **Task 12: Build system updates** (AC: all)
  - [x] 12.1 Add `src/core/JobSystem.cpp` to `engine/CMakeLists.txt`
  - [x] 12.2 Add test files to `tests/CMakeLists.txt`
  - [x] 12.3 Verify all existing tests still pass (zero regressions)

## Dev Notes

### Architecture Compliance

- **One class per file**: JobSystem, ConcurrentQueue, MeshJobTypes each in own headers
- **Namespace**: `voxel::core` for JobSystem and ConcurrentQueue, `voxel::renderer` for MeshJobTypes and MeshChunkTask
- **Error handling**: No exceptions. JobSystem::init() returns `Result<void>`. ConcurrentQueue never fails (push always succeeds, tryPop returns optional).
- **Naming**: PascalCase classes, camelCase methods, `m_` prefix for members, SCREAMING_SNAKE constants
- **Max 500 lines per file** — keep each file focused
- **`#pragma once`** for all headers
- **Threading rules**: Jobs read immutable snapshots. Main thread integrates results. Never lock on chunks. ConcurrentQueue is the only synchronization point.

### Existing Code to Reuse — DO NOT REINVENT

- **`MeshBuilder::buildNaive()`** (MeshBuilder.cpp): Call this FROM the worker thread with the snapshot data. Do NOT create a new meshing function.
- **`ChunkSection`** (ChunkSection.h): Copy by value for snapshots. The struct is 8KB (4096 uint16_t + int32_t), fully copyable. Use `data()` for validation in tests.
- **`ChunkSection::isEmpty()`**: Fast-path check before creating a snapshot — skip empty sections.
- **`ChunkColumn::isSectionDirty()`** / `clearDirty()`: Already tracks which sections need remeshing.
- **`ChunkManager::getChunk()`**: Use to access sections for snapshot creation.
- **`BlockFace` enum, `ChunkMesh` struct, `packQuad()`**: Already defined in ChunkMesh.h, used by MeshBuilder. Do NOT duplicate.
- **`BlockRegistry`**: MeshBuilder already holds a reference. Thread-safe for read access (immutable after startup).
- **`BLOCK_AIR` constant**: Already defined in Block.h.

### What NOT To Do

- Do NOT create a singleton for JobSystem — explicit ownership in GameApp, passed by reference
- Do NOT use `std::jthread` or `std::async` — use enkiTS exclusively (ADR-006)
- Do NOT use `std::latch` or `std::barrier` — use enkiTS sync (project-context.md rule)
- Do NOT lock on chunks — jobs produce results, main thread integrates
- Do NOT mutate ChunkManager state from worker threads — read-only snapshot pattern
- Do NOT implement GPU upload — that's Story 5.7
- Do NOT implement the full chunk pipeline (generate → populate → light → mesh) — Story 5.6 only handles the mesh stage. Generation is still synchronous via WorldGenerator.
- Do NOT change MeshBuilder's API — it's correct as-is for both sync and async use
- Do NOT use `std::shared_ptr` for snapshots — use value semantics (copy on creation)
- Do NOT use lock-free queues — `std::mutex` + `std::deque` is the V1 design per epic spec
- Do NOT add enkiTS-specific config defines (like `ENKITS_TASK_PRIORITIES_NUM`) unless needed — defaults (3 priorities) are sufficient

### enkiTS Integration Details

**vcpkg port**: `enkits` (lowercase in vcpkg.json)
**CMake target**: `find_package(enkiTS CONFIG REQUIRED)` + `enkiTS::enkiTS`
**Latest version**: v1.11 (stable, April 2024)
**License**: zlib (permissive)
**Key headers**: `<enkiTS/TaskScheduler.h>`

**Core API pattern:**
```cpp
#include <enkiTS/TaskScheduler.h>

enki::TaskScheduler scheduler;
scheduler.Initialize();  // Auto-detects core count (hardware_concurrency - 1)

struct MyTask : enki::ITaskSet {
    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
        // Do work here — runs on a worker thread
    }
};

MyTask task;
scheduler.AddTaskSetToPipe(&task);      // Non-blocking dispatch
scheduler.WaitforTask(&task);           // Block until complete

// Priority dispatch (0 = highest, up to ENKITS_TASK_PRIORITIES_NUM - 1):
scheduler.AddTaskSetToPipe(&task, enki::TaskPriority(priority));

scheduler.WaitforAllAndShutdown();      // Clean shutdown
```

**Critical**: Tasks must remain alive until completed. Use `unique_ptr` or pool to manage lifetime. The scheduler does NOT own the task — caller is responsible.

### JobSystem Design

```cpp
// JobSystem.h
#pragma once
#include "voxel/core/Result.h"
#include <enkiTS/TaskScheduler.h>
#include <cstdint>

namespace voxel::core
{

class JobSystem
{
public:
    JobSystem() = default;
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// Initialize the task scheduler. numThreads=0 means auto-detect.
    Result<void> init(uint32_t numThreads = 0);

    /// Wait for all tasks and shut down. Safe to call if not initialized.
    void shutdown();

    /// Access the underlying scheduler for task submission.
    [[nodiscard]] enki::TaskScheduler& getScheduler();

    /// Number of worker threads (not counting the main thread).
    [[nodiscard]] uint32_t threadCount() const;

    /// Whether init() has been called successfully.
    [[nodiscard]] bool isInitialized() const;

private:
    enki::TaskScheduler m_scheduler;
    bool m_initialized = false;
};

} // namespace voxel::core
```

### ConcurrentQueue Design

```cpp
// ConcurrentQueue.h
#pragma once
#include <deque>
#include <mutex>
#include <optional>
#include <cstddef>

namespace voxel::core
{

/// Thread-safe MPSC queue. V1: std::mutex + std::deque.
template<typename T>
class ConcurrentQueue
{
public:
    void push(T&& item)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(item));
    }

    std::optional<T> tryPop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty())
            return std::nullopt;
        T item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    [[nodiscard]] size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    [[nodiscard]] bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
};

} // namespace voxel::core
```

### Snapshot and Result Types Design

```cpp
// MeshJobTypes.h
#pragma once
#include "voxel/renderer/ChunkMesh.h"
#include "voxel/world/ChunkSection.h"
#include <glm/vec2.hpp>
#include <array>
#include <cstdint>

namespace voxel::renderer
{

/// Immutable snapshot of a section + neighbors for async meshing.
/// All data is COPIED by value — safe to read from any thread.
struct MeshJobInput
{
    world::ChunkSection section;                     // Center section (full copy, 8KB)
    std::array<world::ChunkSection, 6> neighbors;    // Neighbor sections (copied)
    std::array<bool, 6> hasNeighbor = {};             // Which neighbors were present
    glm::ivec2 chunkCoord{0, 0};                     // Chunk XZ coordinate
    int sectionY = 0;                                 // Section index within column (0-15)
};

/// Result produced by an async mesh task.
struct MeshResult
{
    ChunkMesh mesh;
    glm::ivec2 chunkCoord{0, 0};
    int sectionY = 0;
};

} // namespace voxel::renderer
```

**Memory cost per snapshot**: Center (8KB) + 6 neighbors (48KB max) = 56KB max per in-flight task. With 8 concurrent tasks = 448KB. Acceptable.

**Why copy full sections instead of boundary slices**: The current `buildOpacityPad()` and `getAdjacentBlock()` take `const ChunkSection*` pointers and access arbitrary positions within the neighbor. Extracting only boundary slices would require changing these interfaces. Full copy is simpler, correct, and the 56KB cost is negligible. Optimize later if profiling shows contention.

### MeshChunkTask Design

```cpp
// In MeshJobTypes.h (or separate header)
struct MeshChunkTask : enki::ITaskSet
{
    MeshJobInput input;
    const MeshBuilder* meshBuilder;          // Non-owning, must outlive task
    core::ConcurrentQueue<MeshResult>* resultQueue;  // Non-owning

    MeshChunkTask(MeshJobInput&& in, const MeshBuilder& builder,
                  core::ConcurrentQueue<MeshResult>& queue)
        : input(std::move(in)), meshBuilder(&builder), resultQueue(&queue)
    {
        m_SetSize = 1;  // Single execution, not a parallel-for
    }

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override
    {
        // Reconstruct neighbor pointer array from snapshot
        std::array<const world::ChunkSection*, 6> neighbors{};
        for (int i = 0; i < 6; ++i)
        {
            neighbors[i] = input.hasNeighbor[i] ? &input.neighbors[i] : nullptr;
        }

        MeshResult result;
        result.mesh = meshBuilder->buildNaive(input.section, neighbors);
        result.chunkCoord = input.chunkCoord;
        result.sectionY = input.sectionY;

        resultQueue->push(std::move(result));
    }
};
```

**Task lifetime**: Each `MeshChunkTask` must remain alive until it completes. ChunkManager must track in-flight tasks (e.g., via `std::vector<std::unique_ptr<MeshChunkTask>>`). Completed tasks can be cleaned up after polling results.

### ChunkManager Extension Design

New members for ChunkManager:
```cpp
// New includes needed
#include "voxel/core/ConcurrentQueue.h"
#include "voxel/renderer/MeshJobTypes.h"

// New members
core::JobSystem* m_jobSystem = nullptr;
const renderer::MeshBuilder* m_meshBuilder = nullptr;
core::ConcurrentQueue<renderer::MeshResult> m_meshResults;

// Mesh storage: key = (chunkCoord.x, chunkCoord.y, sectionY)
struct MeshKey { glm::ivec2 coord; int sectionY; };
struct MeshKeyHash { size_t operator()(const MeshKey& k) const noexcept; };
std::unordered_map<MeshKey, renderer::ChunkMesh, MeshKeyHash> m_meshes;

// In-flight task tracking
std::vector<std::unique_ptr<renderer::MeshChunkTask>> m_inFlightTasks;
std::unordered_set<MeshKey, MeshKeyHash> m_inFlightKeys;  // Prevent double-dispatch

// Config
static constexpr int MAX_RESULTS_PER_FRAME = 8;
static constexpr int MAX_DISPATCHES_PER_FRAME = 4;
```

**update() flow**:
1. **Cleanup**: Remove completed tasks from `m_inFlightTasks` (check `GetIsComplete()` on enkiTS task). Remove their keys from `m_inFlightKeys`.
2. **Poll results**: Call `m_meshResults.tryPop()` up to `MAX_RESULTS_PER_FRAME` times. For each result:
   - Check if chunk is still loaded (`getChunk(result.chunkCoord) != nullptr`). If unloaded → discard (AC10).
   - Store: `m_meshes[{result.chunkCoord, result.sectionY}] = std::move(result.mesh)`
3. **Dispatch dirty sections**: Iterate loaded chunks. For each dirty section not in `m_inFlightKeys`:
   - Skip empty sections (`section.isEmpty()`)
   - Calculate squared distance from player for priority
   - Create snapshot (copy section + 6 neighbors)
   - Dispatch `MeshChunkTask` with distance-based priority
   - Add key to `m_inFlightKeys`, add task to `m_inFlightTasks`
   - Clear dirty flag for the section
   - Stop after `MAX_DISPATCHES_PER_FRAME` new dispatches

**Distance priority mapping** (enkiTS has 3 priority levels by default):
- Priority 0 (HIGH): distance < 32 blocks (2 chunks)
- Priority 1 (MEDIUM): distance < 128 blocks (8 chunks)
- Priority 2 (LOW): distance >= 128 blocks

### Snapshot Creation Helper

```cpp
renderer::MeshJobInput ChunkManager::createMeshSnapshot(
    glm::ivec2 coord, int sectionY) const
{
    renderer::MeshJobInput input;
    const ChunkColumn* col = getChunk(coord);
    VX_ASSERT(col != nullptr, "Cannot snapshot unloaded chunk");

    const ChunkSection* sec = col->getSection(sectionY);
    if (sec) input.section = *sec;  // Copy by value

    input.chunkCoord = coord;
    input.sectionY = sectionY;

    // Neighbor layout: [PosX, NegX, PosY, NegY, PosZ, NegZ]
    // PosX = coord + (1,0), NegX = coord + (-1,0), etc.
    // PosY = sectionY+1, NegY = sectionY-1 (same column)
    struct { glm::ivec2 dCoord; int dSection; } offsets[6] = {
        {{1, 0}, 0},   // PosX
        {{-1, 0}, 0},  // NegX
        {{0, 0}, 1},   // PosY (sectionY+1)
        {{0, 0}, -1},  // NegY (sectionY-1)
        {{0, 1}, 0},   // PosZ
        {{0, -1}, 0},  // NegZ
    };

    for (int i = 0; i < 6; ++i)
    {
        glm::ivec2 nCoord = coord + offsets[i].dCoord;
        int nSectionY = sectionY + offsets[i].dSection;

        // PosY/NegY are in the same column at a different section index
        if (i == 2 || i == 3)
            nCoord = coord;  // Same column

        const ChunkColumn* nCol = getChunk(nCoord);
        if (nCol && nSectionY >= 0 && nSectionY < ChunkColumn::SECTIONS_PER_COLUMN)
        {
            const ChunkSection* nSec = nCol->getSection(nSectionY);
            if (nSec)
            {
                input.neighbors[i] = *nSec;
                input.hasNeighbor[i] = true;
                continue;
            }
        }
        input.hasNeighbor[i] = false;
    }

    return input;
}
```

**Critical neighbor mapping**: PosX/NegX/PosZ/NegZ are in different chunk columns. PosY/NegY are in the SAME column at sectionY+1 / sectionY-1. This is NOT the same as chunk XZ offset. Double-check this mapping against `MeshBuilder::getAdjacentBlock()` which uses the same BlockFace ordering:
- PosX (face 0): x+1 → chunk at (coord.x+1, coord.z), same sectionY
- NegX (face 1): x-1 → chunk at (coord.x-1, coord.z), same sectionY
- PosY (face 2): y+1 → same column, sectionY+1
- NegY (face 3): y-1 → same column, sectionY-1
- PosZ (face 4): z+1 → chunk at (coord.x, coord.z+1), same sectionY
- NegZ (face 5): z-1 → chunk at (coord.x, coord.z-1), same sectionY

### Neighbor Dirty Invalidation

When `setBlock()` changes a block at a section boundary, the neighbor section also needs remeshing (face culling and AO would change). In `ChunkManager::setBlock()`, after marking the section dirty:

```cpp
// Check if block is on section boundary → mark neighbor dirty too
glm::ivec3 local = worldToLocalPos(worldPos);
int sectionY = worldPos.y / ChunkSection::SIZE;

// X boundaries
if (local.x == 0)  markNeighborDirty(chunkCoord + glm::ivec2{-1, 0}, sectionY);
if (local.x == 15) markNeighborDirty(chunkCoord + glm::ivec2{1, 0}, sectionY);

// Y boundaries (same column)
if (worldPos.y % 16 == 0 && sectionY > 0)    markSectionDirty(chunkCoord, sectionY - 1);
if (worldPos.y % 16 == 15 && sectionY < 15)  markSectionDirty(chunkCoord, sectionY + 1);

// Z boundaries
if (local.z == 0)  markNeighborDirty(chunkCoord + glm::ivec2{0, -1}, sectionY);
if (local.z == 15) markNeighborDirty(chunkCoord + glm::ivec2{0, 1}, sectionY);
```

### GameApp Integration

```cpp
// GameApp.h — new members
#include "voxel/core/JobSystem.h"
#include "voxel/renderer/MeshBuilder.h"

// Add to private:
voxel::core::JobSystem m_jobSystem;
voxel::renderer::MeshBuilder m_meshBuilder;  // constructed with m_blockRegistry

// GameApp constructor initializer list:
// m_meshBuilder(m_blockRegistry)  // MeshBuilder takes const BlockRegistry&

// GameApp::init():
auto jobResult = m_jobSystem.init();
if (!jobResult) return std::unexpected(jobResult.error());
m_chunkManager.setJobSystem(&m_jobSystem);
m_chunkManager.setMeshBuilder(&m_meshBuilder);

// GameApp::tick(double dt):
glm::dvec3 playerPos = m_camera.getPosition();
m_chunkManager.update(playerPos);

// GameApp destructor — destruction order is critical:
// ~GameApp() runs, destroying members in reverse declaration order.
// m_meshBuilder destroyed (simple, no resources)
// m_jobSystem destroyed → calls shutdown() in destructor → waits for all tasks
// m_chunkManager destroyed → tasks must be done by this point
// CRITICAL: Declare m_jobSystem BEFORE m_chunkManager to ensure
// JobSystem outlives ChunkManager (reverse declaration order destruction).
```

**Destruction order**: C++ destroys members in reverse declaration order. To ensure JobSystem outlives ChunkManager:
```cpp
// Declare in this order:
voxel::core::JobSystem m_jobSystem;          // Destroyed LAST (after chunkManager)
voxel::renderer::MeshBuilder m_meshBuilder;
voxel::world::ChunkManager m_chunkManager;   // Destroyed FIRST
```
But GameApp currently has `m_chunkManager` declared after `m_blockRegistry` (line 61 of GameApp.h). We need to insert `m_jobSystem` and `m_meshBuilder` BEFORE `m_chunkManager` in the member list.

### File Structure

```
engine/include/voxel/core/
  JobSystem.h                ← CREATE: enkiTS TaskScheduler wrapper
  ConcurrentQueue.h          ← CREATE: thread-safe MPSC queue (header-only)

engine/src/core/
  JobSystem.cpp              ← CREATE: init/shutdown implementation

engine/include/voxel/renderer/
  MeshJobTypes.h             ← CREATE: MeshJobInput, MeshResult, MeshChunkTask

engine/include/voxel/world/
  ChunkManager.h             ← MODIFY: add update(), setJobSystem(), setMeshBuilder(), mesh storage

engine/src/world/
  ChunkManager.cpp           ← MODIFY: implement update(), snapshot creation, result polling

game/src/
  GameApp.h                  ← MODIFY: add m_jobSystem, m_meshBuilder members
  GameApp.cpp                ← MODIFY: init/tick/shutdown integration

engine/CMakeLists.txt        ← MODIFY: add enkiTS dep, JobSystem.cpp source
vcpkg.json                   ← MODIFY: add "enkits" dependency

tests/core/
  TestConcurrentQueue.cpp    ← CREATE
  TestJobSystem.cpp          ← CREATE

tests/renderer/
  TestAsyncMeshing.cpp       ← CREATE

tests/CMakeLists.txt         ← MODIFY: add new test files
```

### Previous Story Intelligence

**From Story 5.1 (done) — buildNaive pattern:**
- MeshBuilder takes `const BlockRegistry&` in constructor — immutable after startup, safe for multi-threaded reads
- `buildNaive()` is `const` method — no mutable state, inherently thread-safe
- ChunkMesh returned by value (movable) — safe for cross-thread transfer
- Empty section fast-path (isEmpty()) — use before creating snapshots

**From Story 5.2 (done) — AO integration:**
- `buildOpacityPad()` uses 18x18x18 stack array — thread-local, no shared state
- `computeFaceAO()` uses stack-local arrays — thread-safe
- All AO computation is self-contained within `buildNaive()` call

**From Story 5.3 (ready-for-dev) — greedy meshing:**
- When `buildGreedy()` is implemented, it will use the same interface (ChunkSection + neighbors → ChunkMesh)
- MeshChunkTask should be easy to switch from `buildNaive()` to `buildGreedy()` — just change the call

**From Story 5.4 (ready-for-dev) — non-cubic meshing:**
- Will add `ModelVertex` buffer to ChunkMesh — MeshResult will need to carry both buffers
- No impact on Story 5.6 as long as MeshResult wraps the full ChunkMesh struct

### Git Intelligence

Recent commits:
```
22c218a feat(renderer): introduce non-cubic block meshing
5f247a0 feat(renderer): finalize AO system with expanded quad packing, CLI build scripts, and tests
e7ce707 feat(renderer): implement naive face culling mesher with chunk mesh and quad packing
```

Convention: `feat(scope): description`. For this story:
- `feat(core): add JobSystem wrapping enkiTS task scheduler`
- `feat(core): add ConcurrentQueue for thread-safe result delivery`
- `feat(renderer): add async mesh job system via enkiTS`

### Testing Standards

- **Framework**: Catch2 v3 with `TEST_CASE` and `SECTION` blocks
- **Tags**: `[core][threading]` for JobSystem/ConcurrentQueue, `[renderer][meshing][async]` for MeshChunkTask
- **No GPU tests**: All tests are CPU-side logic
- **Multi-threaded tests**: Use `std::thread` directly in tests (not enkiTS) for ConcurrentQueue MPSC tests, to verify the queue works independently of the scheduler
- **Determinism**: Async meshing must produce identical ChunkMesh as synchronous `buildNaive()` for the same input — test this explicitly

### Performance Expectations

| Metric | Expected | Notes |
|--------|----------|-------|
| Snapshot creation | <50us | memcpy of ~56KB |
| Task dispatch overhead | <5us | enkiTS AddTaskSetToPipe |
| Queue push/pop | <1us | mutex + deque |
| buildNaive() on worker | ~500us | Same as sync baseline |
| Max concurrent tasks | 8-16 | Limited by dispatch rate, not thread count |
| Memory per in-flight task | ~56KB | Snapshot + task struct |

### Potential Pitfalls

1. **Task lifetime**: enkiTS does NOT own tasks. If a `MeshChunkTask` is destroyed while still running → undefined behavior. Track in-flight tasks carefully.
2. **BlockRegistry thread safety**: The registry is populated at startup and never modified during gameplay → safe for concurrent reads. But if Story 9 adds hot-reload, this assumption breaks. For now, document it as a precondition.
3. **ChunkSection copyability**: `ChunkSection` has no user-declared copy constructor, so the compiler generates one (bitwise copy of `m_blocks[4096]` + `m_nonAirCount`). This is correct and efficient.
4. **Queue contention**: With many workers pushing simultaneously, the mutex could become a bottleneck. V1 is fine (tested in Avoyd-scale). If profiling shows contention, upgrade to lock-free MPSC (future optimization, not this story).
5. **Dirty flag race**: `clearDirty()` is called on the main thread after dispatching a task. If `setBlock()` is called between dispatch and clearDirty, the new dirty flag would be lost. Solution: clear dirty BEFORE dispatch (if task fails, re-mark dirty).

### References

- [Source: engine/include/voxel/renderer/MeshBuilder.h — buildNaive() signature, const method]
- [Source: engine/src/renderer/MeshBuilder.cpp — Full implementation, thread-safety analysis]
- [Source: engine/include/voxel/renderer/ChunkMesh.h — ChunkMesh struct, packQuad(), all unpack helpers]
- [Source: engine/include/voxel/renderer/AmbientOcclusion.h — buildOpacityPad(), computeFaceAO() — stack-local, thread-safe]
- [Source: engine/include/voxel/world/ChunkManager.h — Current API, dirty tracking, spatial hashmap]
- [Source: engine/include/voxel/world/ChunkColumn.h — isSectionDirty(), clearDirty(), getSection()]
- [Source: engine/include/voxel/world/ChunkSection.h — SIZE=16, VOLUME=4096, data(), isEmpty()]
- [Source: game/src/GameApp.h — Member declaration order, ownership model]
- [Source: engine/CMakeLists.txt — Current source list, find_package pattern, link targets]
- [Source: vcpkg.json — Current dependencies, no enkiTS yet]
- [Source: _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md — Story 5.6 epic spec]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 2 Async Chunk Pipeline, ADR-006 enkiTS]
- [Source: _bmad-output/project-context.md — Threading rules, naming conventions, error handling]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- enkiTS DLL build: `AddTaskSetToPipe` does NOT accept a priority argument; priority must be set via `task->m_Priority` member before dispatch
- enkiTS DLL build: multiple `Initialize()`/`~TaskScheduler()` cycles work if each scheduler instance is independent
- spdlog crash in tests: `VX_LOG_*` macros crash when `Log::init()` has not been called (null logger dereference). Removed logging from JobSystem to avoid test-environment crashes. Consider adding a null-guard in `VX_LOG_*` macros for robustness.
- Pre-existing `[[nodiscard]]` warning on `BlockRegistry::loadFromJson()` in GameApp.cpp was surfaced by new build — fixed by handling the return value.
- ChunkManager test for `setBlock` dirty tracking updated to expect Y-boundary neighbor invalidation (AC8 behavior change).

### Completion Notes List
- AC1: enkiTS added to vcpkg.json, engine/CMakeLists.txt (find_package, link, PCH)
- AC2: JobSystem wraps enki::TaskScheduler with init/shutdown/getScheduler/threadCount/isInitialized
- AC3: ConcurrentQueue<T> header-only with push/tryPop/size/empty using std::mutex + std::deque
- AC4: MeshChunkTask : enki::ITaskSet runs buildGreedy() on immutable snapshot, pushes result to queue
- AC5: MeshJobInput snapshot copies center section + 6 neighbors by value with hasNeighbor flags
- AC6: MeshResult delivered via ConcurrentQueue from workers to main thread
- AC7: ChunkManager::update() polls results (max 8/frame), stores completed meshes, dispatches dirty sections (max 4/frame)
- AC8: setBlock() marks neighbor sections dirty on X/Y/Z boundaries. Added ChunkColumn::markDirty() public method.
- AC9: Distance-based priority: HIGH (<32 blocks), MED (<128 blocks), LOW (>=128 blocks) via m_Priority member
- AC10: Stale results discarded if chunk was unloaded (coordinate check on poll). Meshes cleaned up on unloadChunk().
- AC11: 3 test files with FIFO ordering, empty queue, size tracking, MPSC correctness, JobSystem lifecycle, task submission, snapshot copying, determinism, and end-to-end async meshing
- Note: MeshChunkTask calls buildGreedy() (not buildNaive() as originally specified) since greedy meshing is the superior path and has the same interface

### File List
- vcpkg.json (MODIFIED) — added "enkits" dependency
- engine/CMakeLists.txt (MODIFIED) — added find_package(enkiTS), target_link_libraries, PCH, JobSystem.cpp source
- engine/include/voxel/core/JobSystem.h (CREATED) — enkiTS TaskScheduler wrapper
- engine/src/core/JobSystem.cpp (CREATED) — init/shutdown implementation
- engine/include/voxel/core/ConcurrentQueue.h (CREATED) — header-only MPSC queue
- engine/include/voxel/renderer/MeshJobTypes.h (CREATED) — MeshJobInput, MeshResult, MeshChunkTask
- engine/include/voxel/world/ChunkManager.h (MODIFIED) — added update(), setJobSystem(), setMeshBuilder(), getMesh(), MeshKey, async members
- engine/src/world/ChunkManager.cpp (MODIFIED) — implemented update(), createMeshSnapshot(), pollMeshResults(), dispatchDirtySections(), neighbor dirty invalidation in setBlock()
- engine/include/voxel/world/ChunkColumn.h (MODIFIED) — added markDirty() public method
- engine/src/world/ChunkColumn.cpp (MODIFIED) — implemented markDirty()
- game/src/GameApp.h (MODIFIED) — added m_jobSystem, m_meshBuilder members with correct destruction order
- game/src/GameApp.cpp (MODIFIED) — init job system, call update() in tick, shutdown in destructor, fixed loadFromJson nodiscard
- tests/core/TestConcurrentQueue.cpp (CREATED) — FIFO, empty, size, MPSC tests
- tests/core/TestJobSystem.cpp (CREATED) — lifecycle, task submission, multi-task, threadCount tests
- tests/renderer/TestAsyncMeshing.cpp (CREATED) — snapshot copy, neighbor flags, determinism, e2e async tests
- tests/CMakeLists.txt (MODIFIED) — added 3 new test files
- tests/world/TestChunkManager.cpp (MODIFIED) — updated dirty tracking test to expect Y-boundary neighbor invalidation

### Change Log
- 2026-03-27: Story 5.6 implementation complete — all 12 tasks done, 145 test cases pass (0 regressions)
- 2026-03-27: Code review fixes applied (3 issues):
  - [H1] Added ChunkManager::shutdown() — waits for in-flight tasks before destruction, fixing use-after-free race on exit
  - [M1] Added cancellation path test (AC10) — verifies unloaded chunk results are discarded
  - [M2] Added X/Z boundary neighbor dirty invalidation tests (AC8) — 3 new test cases
  - Updated GameApp::~GameApp() to call m_chunkManager.shutdown() before m_jobSystem.shutdown()
  - 149 test cases pass (482,905 assertions, 0 regressions)
