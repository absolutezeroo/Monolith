# Epic 5 — Meshing Pipeline

**Priority**: P0
**Dependencies**: Epic 3
**Goal**: Binary greedy meshing converts chunk data into compact GPU-ready quads, with AO baked in, uploaded to the gigabuffer via the async pipeline.

---

## Story 5.1: Naive Face Culling (Baseline Mesher)

**As a** developer,
**I want** a simple mesher that only emits faces between solid and air blocks,
**so that** I have a working baseline to render chunks before optimizing.

**Acceptance Criteria:**
- `MeshBuilder::buildNaive(const ChunkSection&, neighbors[6]) → ChunkMesh`
- `ChunkMesh` struct: `std::vector<uint64_t> quads` (packed 8-byte format per architecture), `uint32_t quadCount`
- Only emit a face if the adjacent block (in that face's direction) is air or transparent
- Neighbor sections provided for face culling at section boundaries (16→0 edge)
- Quad format: position (6+6+6 bits), width=1/height=1 (no merging yet), block type, face direction, AO=3 (no occlusion)
- Unit test: single block in empty section → 6 faces; two adjacent blocks → 10 faces (shared face culled)
- Performance baseline measured (expected ~500μs/chunk for dense terrain)

---

## Story 5.2: Ambient Occlusion Calculation

**As a** developer,
**I want** per-vertex AO values computed during meshing,
**so that** block edges and corners have realistic shadowing.

**Acceptance Criteria:**
- `vertexAO(bool side1, bool corner, bool side2) → int` (0=full occlusion, 3=none)
- For each face vertex, sample 3 adjacent blocks (2 sides + 1 corner) from the chunk data
- AO values packed into quad data (4×2 bits = 8 bits in the packed uint64_t)
- Quad diagonal flip: when `abs(ao[0]-ao[3]) > abs(ao[1]-ao[2])`, flip the triangulation
- Flip flag encoded in quad data so vertex shader can reconstruct correctly
- Unit test: block in corner of room → AO values match expected pattern; isolated block → all AO=3

---

## Story 5.3: Binary Greedy Meshing Implementation

**As a** developer,
**I want** the binary greedy meshing algorithm,
**so that** meshing runs at ~74μs/chunk with maximum face merging.

**Acceptance Criteria:**
- `MeshBuilder::buildGreedy(const ChunkSection&, neighbors[6]) → ChunkMesh`
- Uses 64-bit bitmasks per slice: bit=1 where face is visible
- XOR with neighbor slice to find emittable faces
- Greedy merging via bitwise operations (leading/trailing zeros, row scanning)
- Merges coplanar adjacent faces of same block type into larger quads
- Quad format: 8 bytes total — position + merged width/height + block type + face + AO packed
- Performance: < 200μs/chunk for typical terrain (benchmark via Catch2 BENCHMARK)
- Unit test: flat ground plane merges into minimal quads; sphere produces correct face count ±5%
- Reference: cgerikj/binary-greedy-meshing algorithm

---

## Story 5.4: Async Mesh Jobs via enkiTS

**As a** developer,
**I want** meshing to run on worker threads via the job system,
**so that** chunk meshing never blocks the main thread.

**Acceptance Criteria:**
- `MeshChunkTask : enki::ITaskSet` — takes snapshot of section + 6 neighbors, produces ChunkMesh
- Snapshot creation: copy section data + neighbor boundary slices before dispatch (immutable input)
- Result delivered via concurrent queue (MPSC: many workers → one main thread consumer)
- Main thread polls results in `ChunkManager::update()`, integrates max N per frame
- Priority: closer chunks mesh first (enkiTS priority levels)
- Cancellation: if chunk unloaded before mesh completes, discard result
- Dirty tracking: re-queue meshing when block changed in section or neighbor

---

## Story 5.5: Mesh Upload to Gigabuffer

**As a** developer,
**I want** completed mesh data uploaded into the gigabuffer,
**so that** the GPU can render chunks via indirect drawing.

**Acceptance Criteria:**
- After mesh job completes and main thread accepts result: allocate space in gigabuffer via VmaVirtualBlock
- Copy mesh data to staging buffer, record `vkCmdCopyBuffer` to gigabuffer at allocated offset
- Track per-chunk: gigabuffer offset, quad count, world position → stored in `ChunkRenderInfo`
- When chunk is unloaded or remeshed: free old gigabuffer allocation before allocating new
- Upload budget respected: max N uploads per frame (integrated with Story 2.5 staging pipeline)
- If gigabuffer is full: log error, skip upload (chunk renders with old mesh or not at all)
