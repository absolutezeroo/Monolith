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
- `BlockDefinition` struct with the complete property set (informed by Luanti ContentFeatures analysis):

```cpp
struct BlockDefinition {
    // --- Identity ---
    std::string stringId;              // "base:stone", "mymod:torch"
    uint16_t numericId = 0;            // Assigned by registry

    // --- Core properties ---
    bool isSolid = true;               // Blocks movement, occludes faces
    bool isTransparent = false;        // Light passes through (glass, leaves)
    bool hasCollision = true;          // Player collides with this block
    float hardness = 1.0f;             // Base break time multiplier
    uint8_t lightEmission = 0;         // 0–15, torch=14
    uint8_t lightFilter = 0;           // Light attenuation passing through (water=2, leaves=1)

    // --- Rendering ---
    RenderType renderType = RenderType::Opaque;  // Opaque, Cutout, Translucent
    ModelType modelType = ModelType::FullCube;    // FullCube, Slab, Stair, Cross, Torch, Connected, JsonModel, MeshModel, Custom
    uint16_t textureIndices[6] = {};              // Per-face texture array layer index
    uint8_t tintIndex = 0;             // 0=none, 1=grass, 2=foliage, 3=water (biome tinting)
    uint8_t waving = 0;                // 0=none, 1=leaves, 2=plants, 3=liquid surface (vertex anim)

    // --- Physics / interaction ---
    bool isClimbable = false;          // Ladders, vines — player can ascend/descend
    uint8_t moveResistance = 0;        // 0–7, cobweb=7, honey=4, water=3 (slows player)
    uint32_t damagePerSecond = 0;      // Passive damage inside block (cactus=1, lava=4)
    uint8_t drowning = 0;             // Breath consumed per second (water=1)
    bool isBuildableTo = false;        // Tall grass, snow — replaced by block placement
    bool isFloodable = false;          // Torches, flowers — destroyed by flowing liquid
    bool isReplaceable = false;        // Air, liquid — can place blocks here without targeting a face

    // --- Tool / mining groups (used by future tool capabilities system) ---
    std::unordered_map<std::string, int> groups;  // "cracky"=3, "choppy"=2, "snappy"=1, "oddly_breakable_by_hand"=3

    // --- Drop ---
    std::string dropItem;              // "base:cobblestone" — default drop (overridden by get_drops callback)

    // --- Sound (stubs — used when audio system is implemented) ---
    std::string soundFootstep;         // "base_footstep_stone"
    std::string soundDig;              // "base_dig_stone"
    std::string soundPlace;            // "base_place_stone"

    // --- Liquid (stubs — used when fluid system is implemented) ---
    LiquidType liquidType = LiquidType::None;  // None, Source, Flowing
    uint8_t liquidViscosity = 0;       // 1=fast (water), 7=slow (lava)
    uint8_t liquidRange = 8;           // How far liquid flows from source
    bool liquidRenewable = true;       // Two sources create a new source between them
    std::string liquidAlternativeFlowing;  // "base:water_flowing"
    std::string liquidAlternativeSource;   // "base:water_source"

    // --- Visual effects ---
    uint32_t postEffectColor = 0;      // ARGB overlay when camera is inside (0x80000044 = blue underwater)

    // --- Mechanical behavior ---
    PushReaction pushReaction = PushReaction::Normal;  // Normal=pushable, Destroy=breaks when pushed, Block=immovable (obsidian, bedrock)
    bool isFallingBlock = false;       // Sand, gravel — falls when unsupported (via ABM + falling entity)

    // --- Redstone/signal (stubs — used when signal system is implemented) ---
    uint8_t powerOutput = 0;           // 0–15, static power output (redstone block=15, redstone torch=15)
    bool isPowerSource = false;        // This block emits power without input
    bool isPowerConductor = true;      // Opaque blocks conduct power, transparent don't
    // Dynamic power: on_powered/get_comparator_output callbacks in Epic 9

    // --- Block states (populated by Story 3.4) ---
    std::vector<BlockStateProperty> properties;
    uint16_t baseStateId = 0;
    uint16_t stateCount = 1;

    // --- Lua callbacks (populated by Epic 9) ---
    // std::optional<sol::function> on_place, on_dig, on_rightclick, etc.
    // Added when sol2 is integrated — fields reserved, not compiled until Epic 9
};

enum class PushReaction : uint8_t { Normal, Destroy, Block };
```

- `BlockRegistry` class: `registerBlock(def) → uint16_t`, `getBlock(uint16_t) → const BlockDefinition&`, `getIdByName(string_view) → uint16_t`, `blockCount()`
- ID 0 is always `BLOCK_AIR` (registered automatically, isSolid=false, hasCollision=false)
- Load base block definitions from `assets/scripts/base/blocks.json`
- JSON format: array of objects with stringId and any subset of properties (defaults for omitted fields)
- Namespace `"base:blockname"` enforced
- Enum types: `RenderType { Opaque, Cutout, Translucent }`, `ModelType { FullCube, Slab, Stair, Cross, Torch, Connected, JsonModel, MeshModel, Custom }`, `LiquidType { None, Source, Flowing }`
- Stub fields (sound, liquid, postEffect) are stored but not used until their respective epics — zero runtime cost
- Unit tests: register, lookup by ID, lookup by name, ID 0 is air, duplicate name rejected, groups lookup

---

## Story 3.4: Block State System

**As a** developer,
**I want** blocks to have state properties (facing, open/closed, half, shape, etc.),
**so that** doors, stairs, slabs, levers, fences, and other multi-state blocks are possible.

**Why this must be done now:**
The `uint16_t` in ChunkSection represents a block. If it's just a type ID, adding states later forces refactoring chunk storage, palette compression, meshing, rendering, and the Lua API. The state system must exist before palette compression (Story 3.6) and meshing (Epic 5) are built.

**Acceptance Criteria:**

**Design: Flattened state IDs (Minecraft Java approach):**
Each unique combination of `(blockType, state1, state2, ...)` maps to a unique `uint16_t` called a `BlockStateId`. The BlockRegistry manages this mapping.

Example: `base:oak_door` has properties `facing` (4 values), `half` (2), `open` (2), `hinge` (2) = 32 permutations = 32 BlockStateIds allocated.

```cpp
struct BlockStateProperty {
    std::string name;                    // "facing", "open", "half"
    std::vector<std::string> values;     // ["north","south","east","west"] or ["true","false"]
};

struct BlockDefinition {
    // ...existing fields...
    std::vector<BlockStateProperty> properties;    // Empty for simple blocks
    uint16_t baseStateId;                          // First ID of this block's state range
    uint16_t stateCount;                           // Number of permutations (1 for simple blocks)
};
```

**BlockRegistry extended:**
- `registerBlock(def)` allocates `stateCount` consecutive IDs starting from `baseStateId`
- `getBlockType(uint16_t stateId) → const BlockDefinition&` — looks up the parent type regardless of state
- `getStateValues(uint16_t stateId) → StateMap` — returns the property values for this specific state
- `getStateId(uint16_t baseId, StateMap) → uint16_t` — combines base type + property values into a state ID
- `withProperty(uint16_t stateId, string propName, string value) → uint16_t` — returns the ID with one property changed
- Simple blocks (stone, dirt) have `stateCount = 1` and `properties = {}` — zero overhead

**ChunkSection unchanged:**
- Still stores `uint16_t` per block — but now it's a BlockStateId, not just a type ID
- `getBlock()` / `setBlock()` work identically — just the interpretation of the ID changes

**Palette compression works automatically:**
- A section with 4 door states uses 4 palette entries — same algorithm, no changes needed

**Unit tests:**
- Register a simple block → stateCount=1, baseStateId works
- Register a door with 4 properties → stateCount=32, each permutation gets unique ID
- `getStateValues` roundtrips correctly
- `withProperty("facing", "south")` returns correct ID
- Total ID space usage stays under 65535 for reasonable block counts

**What this enables (future stories):**
- Story 5.X (non-cubic meshing): stairs, slabs use state to determine model variant
- Story 9.2 (Lua registration): `voxel.register_block({ properties = { facing = {"north","south","east","west"} } })`
- Story 7.5 (block placement): `get_state_for_placement` callback returns the correct state based on player facing

---

## Story 3.5: ChunkManager + Spatial HashMap

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

## Story 3.6: Palette Compression Codec

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

## Story 3.7: Chunk Serialization (Disk I/O)

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

**World directory structure:**
```
worlds/
└── MyWorld/
    ├── world.json          # Seed, creation date, game version, player position
    ├── region/
    │   ├── r.0.0.dat       # Region file for chunks (0,0) to (31,31)
    │   ├── r.-1.0.dat
    │   └── ...
    └── block_timers.dat    # Active block timer state (persists across sessions)
```

**Graceful shutdown (triggered by window close, Alt+F4, Escape→Quit):**
- On shutdown signal: set `m_shutdownRequested = true` in GameLoop
- GameLoop exits the main loop cleanly (finishes current frame)
- `ChunkManager::saveAllDirty()` — iterates all loaded chunks, saves those with dirty flag
- Save player position + current settings to `world.json`
- Save active block timers to `block_timers.dat`
- Log: "World saved: X chunks written, Y ms"
- Only THEN proceed with Vulkan cleanup

**Autosave:**
- Every 60 seconds (configurable): save all dirty chunks in background
- Spread across multiple frames: save max 4 chunks per tick to avoid stalls
- Log: "Autosave: X chunks" (debug level, not spammy)
