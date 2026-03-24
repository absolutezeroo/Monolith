# Epic 3 — Voxel World Core (Chunks + Registry)

**Priority**: P0
**Dependencies**: Epic 1
**Goal**: ChunkSection and ChunkColumn storage working, BlockRegistry populated from JSON, ChunkManager loading/unloading chunks around the player, palette compression for serialization.

---

## Story 3.1: ChunkSection Flat Array Storage

**As a** developer,
**I want** a 16³ block storage unit with fast get/set access,
**so that** chunks can store voxel data efficiently.

**Acceptance Criteria:**
- `ChunkSection` struct: `uint16_t blocks[4096]`, indexed `y*256 + z*16 + x`
- `getBlock(int x, int y, int z) → uint16_t` with `VX_ASSERT` bounds check in debug
- `setBlock(int x, int y, int z, uint16_t id)` with bounds check
- `fill(uint16_t id)` — fills entire section
- `isEmpty() → bool` — true if all blocks are `BLOCK_AIR` (ID 0)
- `countNonAir() → int32_t` — for stats
- Namespace `voxel::world`
- Unit tests: get/set roundtrip, fill, boundary values (0,0,0 and 15,15,15), out-of-bounds assert fires in debug

---

## Story 3.2: ChunkColumn (Vertical Stack of Sections)

**As a** developer,
**I want** a vertical stack of 16 sections representing a full chunk column (256 blocks tall),
**so that** the world has vertical extent.

**Acceptance Criteria:**
- `ChunkColumn` class: array of 16 `std::unique_ptr<ChunkSection>` (null = empty sky)
- `getSection(int y) → ChunkSection*` (null if not yet allocated)
- `getOrCreateSection(int y) → ChunkSection&` (allocates on first write)
- `getBlock(int x, int y, int z) → uint16_t` (translates y to section index + local y)
- `setBlock(int x, int y, int z, uint16_t id)` (allocates section if needed)
- `chunkCoord` member: `glm::ivec2` (x, z world chunk coordinate)
- `isDirty` flag per section (set on `setBlock`, cleared after remesh)
- Unit tests: cross-section access (y=15→16 boundary), null section returns AIR, set triggers allocation

---

## Story 3.3: BlockRegistry + JSON Loading

**As a** developer,
**I want** a registry mapping block names to IDs and storing block properties,
**so that** the engine knows what each block type is.

**Acceptance Criteria:**
- `BlockDefinition` struct per architecture (stringId, numericId, isSolid, isTransparent, hasCollision, lightEmission, lightFilter, hardness, textureIndices[6], dropItem)
- `BlockRegistry` class: `registerBlock(def) → uint16_t`, `getBlock(uint16_t) → const BlockDefinition&`, `getIdByName(string_view) → uint16_t`, `blockCount()`
- ID 0 is always `BLOCK_AIR` (registered automatically)
- Load base block definitions from `assets/scripts/base/blocks.json`
- JSON format: array of objects with stringId, properties, texture names
- Namespace `"base:blockname"` enforced
- Unit tests: register, lookup by ID, lookup by name, ID 0 is air, duplicate name rejected

---

## Story 3.4: ChunkManager + Spatial HashMap

**As a** developer,
**I want** a manager that stores loaded chunks and provides spatial access,
**so that** I can query and modify the world by coordinates.

**Acceptance Criteria:**
- `ChunkManager` class: `std::unordered_map<glm::ivec2, std::unique_ptr<ChunkColumn>, ChunkCoordHash>`
- `getChunk(glm::ivec2 coord) → ChunkColumn*` (null if not loaded)
- `getBlock(glm::ivec3 worldPos) → uint16_t` (translates to chunk + local, returns AIR if not loaded)
- `setBlock(glm::ivec3 worldPos, uint16_t id)` (marks section dirty)
- `loadChunk(glm::ivec2 coord)` — creates empty ChunkColumn (terrain gen comes in Epic 4)
- `unloadChunk(glm::ivec2 coord)` — removes from map (serialization comes later)
- `update(glm::dvec3 playerPos, int renderDistance)` — loads/unloads chunks in spiral from player, max N per frame
- `getLoadedChunkCount()`, `getDirtyChunkCount()`
- `ChunkCoordHash` using XOR-shift combination (per architecture)
- Unit tests: load/unload, getBlock across chunk boundaries, set marks dirty

---

## Story 3.5: Palette Compression Codec

**As a** developer,
**I want** to compress chunk sections using palette encoding with variable bits-per-entry,
**so that** serialization and distant chunk storage use minimal memory.

**Acceptance Criteria:**
- `PaletteCompression` class: `compress(const ChunkSection&) → CompressedSection`, `decompress(const CompressedSection&) → ChunkSection`
- `CompressedSection` struct: `std::vector<uint16_t> palette`, `std::vector<uint64_t> data` (packed bits), `uint8_t bitsPerEntry`
- Bits-per-entry: 0 (single value), 1, 2, 4, 8, 16 (direct)
- Automatic transition: if palette grows beyond 2^bitsPerEntry, resize to next power
- `memoryUsage()` returns actual bytes used
- Unit tests: roundtrip (compress→decompress identity), single-type section = 0 bits, two types = 1 bit, many types transitions, memory usage matches expectations

---

## Story 3.6: Chunk Serialization (Disk I/O)

**As a** developer,
**I want** to save and load chunks to/from disk,
**so that** the world persists between sessions.

**Acceptance Criteria:**
- Save format: palette-compressed sections + LZ4 compression per chunk column
- File layout: one file per region (32×32 chunks), indexed header for random access
- `ChunkSerializer::save(const ChunkColumn&, const fs::path& regionDir)`
- `ChunkSerializer::load(glm::ivec2 coord, const fs::path& regionDir) → Result<ChunkColumn>`
- Region file format: 4-byte header per chunk (offset + size), then concatenated compressed data
- Handles missing files gracefully (returns ChunkNotLoaded error → triggers generation)
- Unit tests: save/load roundtrip, corrupt file handling, empty chunk serialization
