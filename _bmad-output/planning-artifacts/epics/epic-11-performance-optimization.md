# Epic 11 — Performance Optimization (Beyond LOD)

**Priority**: P2
**Dependencies**: Epic 5, Epic 6, Epic 10
**Goal**: Push render distance to 48-64+ chunks without LOD by eliminating unnecessary work across CPU, VRAM, and RAM. Every technique targets the same principle: don't process what doesn't need processing.

---

## Story 11.1: Lazy Meshing (Skip Fully Enclosed Sections)

**As a** developer,
**I want** the mesher to skip sections that have no exposed faces,
**so that** 60-80% of underground sections are never meshed at all.

**Acceptance Criteria:**
- Before queuing a section for meshing, check all 6 neighbor sections
- If ALL 6 neighbors exist and are fully opaque (every block is solid), skip meshing entirely — section produces zero quads
- `ChunkSection::isFullyOpaque() → bool` — cached flag, updated on any `setBlock` call (dirty on change)
- Optimization: maintain a per-section `opaqueBlockCount` counter — if count == 4096, section is fully opaque
- When a neighbor section is modified (block broken), re-evaluate adjacent sections that were previously skipped
- Skip also applies to gigabuffer: no allocation needed for sections with zero quads
- Metrics in ImGui: "Sections skipped (enclosed): X / Y total (Z%)"
- Unit test: 3×3×3 grid of fully solid sections → only the 26 outer sections mesh, center section skipped
- Performance benchmark: measure total meshing time for a 16-chunk radius with and without lazy meshing

**Technical Notes:**
- This is the single highest-impact optimization — most of the underground is solid stone surrounded by solid stone
- The check is O(1) per section (6 neighbor lookups + opaque flag check)
- Must re-trigger meshing if a block is broken in a neighbor that was causing the skip

---

## Story 11.2: Lazy Generation (Skip Unnecessary Section Fill)

**As a** developer,
**I want** the world generator to skip filling sections that will never be individually accessed,
**so that** terrain generation is 40-60% faster.

**Acceptance Criteria:**
- After heightmap generation for a column, determine per-section content type:
  - **Above terrain**: section is entirely air → store as null (already done in ChunkColumn)
  - **Below terrain floor and above cave range**: section is entirely stone → store metadata `SectionType::SolidStone` without allocating 4096 blocks
  - **In terrain range or cave range**: generate normally (needs individual block data)
- `SectionType` enum: `Null` (air), `Uniform(blockId)`, `Generated` (full block array)
- `getBlock()` on a Uniform section returns the stored blockId without array access
- `setBlock()` on a Uniform section triggers allocation + fill + then sets the individual block
- Noise caching: 2D heightmap and biome maps computed once per column, stored in `ChunkColumn::m_heightCache` and `m_biomeCache`, shared across all 16 sections
- Benchmark: measure generation time per column with and without lazy generation
- Unit test: column over flat plains at y=64 → sections 0-3 are Uniform(stone), 4 is Generated (surface), 5-15 are Null

**Technical Notes:**
- The heightmap tells us the exact boundary between "definitely stone" and "needs detail"
- Cave carving range can be estimated (e.g., y=10 to y=60) — sections fully below cave floor are safe to mark Uniform
- Noise caching alone saves 20-30% generation time (no redundant 2D noise per section)

---

## Story 11.3: Compressed In-Memory Sections

**As a** developer,
**I want** distant sections stored in palette-compressed form in RAM,
**so that** memory usage drops by 60-80% for the loaded world.

**Acceptance Criteria:**
- Sections beyond a configurable distance threshold (default: 8 chunks) are stored compressed in RAM
- `ChunkSection` has two modes: `Flat` (uint16_t[4096], 8 KB) and `Compressed` (palette + bitbuffer, variable size)
- Automatic transition: section starts as Flat during generation/meshing, compressed after meshing completes and section is not dirty
- `getBlock()` on a Compressed section reads from palette+bitbuffer (slower but infrequent for distant chunks)
- `setBlock()` on a Compressed section decompresses to Flat first (rare — player is far away)
- When player approaches a compressed section (crosses threshold), decompress proactively before any access
- Memory tracking in ImGui: "RAM: X MB flat + Y MB compressed + Z MB null = total"
- Compression runs on worker thread (enkiTS job), does not block main thread
- Unit test: compress → getBlock roundtrip matches original for all 4096 positions
- Benchmark: memory usage at 16/32/48 chunks RD with and without compression

**Technical Notes:**
- Reuses the PaletteCompression codec from Epic 3 Story 3.5
- Homogeneous sections (all stone): ~8 bytes compressed vs 8 KB flat = 1000× reduction
- Typical mixed surface section: ~1-2 KB compressed vs 8 KB flat = 4-8× reduction

---

## Story 11.4: Section Deduplication

**As a** developer,
**I want** identical sections to share a single mesh in the gigabuffer,
**so that** VRAM usage drops by 30-40% for repetitive underground geometry.

**Acceptance Criteria:**
- After meshing, compute a hash of the mesh output (quad data)
- `MeshDeduplicationCache`: hashmap of `meshHash → GigabufferAllocation`
- If hash already exists in cache: reuse the existing gigabuffer allocation (increment refcount)
- If hash is new: allocate in gigabuffer as normal, store in cache with refcount=1
- On chunk unload: decrement refcount, free gigabuffer allocation only when refcount reaches 0
- `ChunkRenderInfo` points to the shared gigabuffer offset (multiple sections can share one)
- Metrics in ImGui: "Unique meshes: X / Total sections: Y (Z% dedup rate)"
- Hash function: fast non-cryptographic hash (xxHash or FNV-1a) over the raw quad bytes
- Unit test: two identical sections produce same hash and share one gigabuffer allocation
- Benchmark: VRAM usage at 32 chunks RD with and without dedup

**Technical Notes:**
- High dedup rate underground: fully-enclosed sections are skipped (Story 11.1), sections just below surface tend to have similar geometry
- Dedup works best when combined with lazy meshing — remaining sections have more diversity but still share patterns
- Hash collision: extremely unlikely with xxHash 64-bit, but if it happens, worst case is visual glitch (not crash)

---

## Story 11.5: VRAM Streaming (Active Budget)

**As a** developer,
**I want** only meshes near the camera frustum stored in VRAM,
**so that** the gigabuffer is used efficiently for what's actually renderable.

**Acceptance Criteria:**
- Define a VRAM budget zone: frustum + margin (e.g., frustum extents + 4 chunks in all directions)
- Meshes outside the budget zone are evicted from gigabuffer (freed) but kept as compressed mesh data in RAM
- When camera turns or player moves, meshes entering the budget zone are uploaded from RAM cache
- Priority queue: upload nearest-to-frustum meshes first
- Upload budget respected: max N uploads per frame (same as existing staging pipeline)
- Eviction is lazy: only evict when gigabuffer usage exceeds threshold (e.g., 80%)
- RAM mesh cache: store raw quad bytes in system memory, ready for re-upload without re-meshing
- Metrics in ImGui: "VRAM budget: X / Y MB | RAM cache: Z MB | Evictions/frame: N | Uploads/frame: M"
- Fallback: if RAM cache is also full, evict oldest RAM entries (will need re-mesh on next approach)
- No visual popping: margin zone ensures meshes are uploaded before they enter the frustum

**Technical Notes:**
- This decouples "loaded in RAM" from "present in VRAM" — you can have 64 chunks in RAM but only 24 chunks worth of VRAM
- The gigabuffer effectively becomes a GPU-side cache with LRU eviction
- Combined with deduplication (Story 11.4), shared meshes are only evicted when ALL referencing sections are outside the budget zone
- The margin must be large enough that at normal movement speed, meshes are uploaded before they become visible

---

## Story 11.6: Mesh Caching on Disk

**As a** developer,
**I want** compiled mesh data saved alongside chunk data on disk,
**so that** returning to a previously visited area loads in milliseconds instead of re-meshing.

**Acceptance Criteria:**
- After meshing a section, save the raw quad data to a mesh cache file alongside the region file
- Cache format: section coordinate → (meshHash, quadCount, raw quad bytes), LZ4 compressed
- On chunk load: check mesh cache first — if section data hasn't changed (compare data hash), load cached mesh directly
- Cache invalidation: if section block data hash differs from cached hash, discard cache entry and re-mesh
- Separate cache file per region (32×32 chunks), independent of world save files
- Cache is optional: if corrupted or missing, fall back to normal meshing (no data loss)
- `--clear-mesh-cache` command line flag to force rebuild
- Metrics in ImGui: "Mesh cache hits: X / Y loads (Z% hit rate)"
- Benchmark: compare chunk load time with and without mesh cache (target: 90%+ reduction in reload scenarios)

**Technical Notes:**
- NVMe SSD read speed (~3-7 GB/s) is orders of magnitude faster than re-meshing on CPU
- The mesh cache turns "CPU-bound reload" into "IO-bound reload"
- Cache size on disk is small: LZ4-compressed mesh data, ~1-5 KB per section × thousands of sections = ~10-50 MB per region
- Cache grows with explored area — can add a max cache size with LRU eviction if needed

---

## Story 11.7: SIMD-Accelerated Meshing (AVX2)

**As a** developer,
**I want** the binary greedy meshing inner loop accelerated with AVX2 intrinsics,
**so that** meshing throughput doubles or quadruples.

**Acceptance Criteria:**
- Identify the hot loop in binary greedy meshing: the bitmask scanning and merging operations
- Replace scalar 64-bit operations with AVX2 256-bit equivalents where applicable:
  - Process 4 slices simultaneously instead of 1
  - Use `_mm256_and_si256`, `_mm256_andnot_si256`, `_mm256_cmpeq_epi64` for mask operations
  - Use `_tzcnt_u64` / `_lzcnt_u64` (BMI1, available on i5 11600KF) for bit scanning
- Maintain a scalar fallback path for CPUs without AVX2 (compile-time or runtime detection)
- `#ifdef __AVX2__` guard or runtime `cpuid` check
- Output must be bit-identical to scalar version (same quads, same order)
- Benchmark: compare meshing time per chunk — scalar vs AVX2
- Target: 2-4× speedup on the meshing inner loop (overall section mesh time: ~20-40μs from ~74μs)
- Unit test: scalar and AVX2 paths produce identical mesh output for a battery of test sections

**Technical Notes:**
- The binary greedy meshing algorithm is naturally SIMD-friendly: it operates on bitmasks that fit in 64-bit integers, and AVX2 processes 4×64-bit in one instruction
- The i5 11600KF supports AVX2, BMI1, BMI2 — all useful for bit manipulation
- This is the highest complexity story in this epic — only pursue after all other optimizations are measured and confirmed beneficial
- FastNoise2 already demonstrates SIMD noise generation; similar approach applies here

---

## Story 11.8: Hybrid Render Distance (Per-Direction Asymmetry)

**As a** developer,
**I want** render distance that extends further in the camera look direction than behind,
**so that** the player perceives a large world while loading fewer total chunks.

**Acceptance Criteria:**
- Render distance parameterized as: `forward` (default 48), `side` (default 32), `behind` (default 16), `up` (default 8), `down` (default 4)
- ChunkManager load/unload evaluates each chunk against an ellipsoidal or frustum-aligned volume instead of a sphere
- Forward direction updated smoothly from camera yaw (low-pass filtered to prevent thrashing when looking around quickly)
- Chunks behind the player unloaded aggressively (they'll be reloaded from mesh cache if the player turns around)
- Chunks below the player unloaded aggressively (rarely visible unless flying)
- Net effect: same number of loaded chunks as a 24-chunk sphere, but perceived distance of 48+ chunks forward
- Configurable via ImGui sliders per direction
- Smooth transitions: don't unload/reload rapidly when player spins — use hysteresis (unload at distance+4, load at distance)
- Metrics in ImGui: "Load shape: F48/S32/B16/U8/D4 | Loaded: X chunks"

**Technical Notes:**
- This is a perception hack — the player mostly looks forward, so forward distance matters most
- Combined with mesh caching (Story 11.6), turning around reloads from cache almost instantly
- Hysteresis is critical to prevent thrashing at boundaries
- The asymmetric shape can be visualized in the debug overlay as a wireframe volume around the player
