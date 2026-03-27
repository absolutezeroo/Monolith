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

**Quad format — complete 64-bit allocation (reference for ALL meshing/shader stories):**
```
Bit range   Width   Field                  Set by       Used by
─────────   ─────   ─────────────────────  ───────────  ──────────────
[0:5]       6       X position (0–63)      Story 5.1    chunk.vert 6.2
[6:11]      6       Y position (0–63)      Story 5.1    chunk.vert 6.2
[12:17]     6       Z position (0–63)      Story 5.1    chunk.vert 6.2
[18:23]     6       Width - 1 (0–63)       Story 5.3    chunk.vert 6.2
[24:29]     6       Height - 1 (0–63)      Story 5.3    chunk.vert 6.2
[30:45]     16      Block state ID (0–65535) Story 5.3b  chunk.frag 6.2 (texture lookup)
[46:48]     3       Face direction (0–5)   Story 5.3b   chunk.vert 6.2 (normal + corner reconstruction)
[49:50]     2       AO corner 0 (0–3)     Story 5.3b   chunk.frag 6.2
[51:52]     2       AO corner 1 (0–3)     Story 5.3b   chunk.frag 6.2
[53:54]     2       AO corner 2 (0–3)     Story 5.3b   chunk.frag 6.2
[55:56]     2       AO corner 3 (0–3)     Story 5.3b   chunk.frag 6.2
[57]        1       Quad diagonal flip     Story 5.3b   chunk.vert 6.2 (triangle winding)
[58]        1       Is non-cubic model     Story 5.4    chunk.vert 6.2 (model vertex path)
[59:60]     2       Tint index (0–3)       Story 5.5    chunk.frag 6.8 (biome color)
[61]        1       Waving flag (0–1)      Story 5.5    chunk.vert 6.2 (vertex animation)
[62:63]     2       Reserved
```
All stories that touch the quad format MUST reference this table. Any bit allocation change must update this table first.

> **Note (Story 5.3b):** Block state ID expanded from 10→16 bits to match the full `uint16_t` state space of BlockRegistry. This shifted face/AO/flip fields by 6 positions. Sky light and block light fields removed from the quad — Epic 8 Story 8.0 must use a secondary data channel (e.g., a second `uint32_t` per quad or a light SSBO). Tint (2 bits) and waving (1 bit) remain.
>
> **Note (Story 5.2 review):** AO expanded from 4 bits (2 paired values) to 8 bits (4 individual 2-bit corners) to support the full 0–3 AO gradient per vertex. Tint reduced from 3→2 bits (4 tint indices) and waving reduced from 2→1 bit (flag only) to fit within 64 bits.
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
- Quad diagonal flip: when `ao[0]+ao[3] > ao[1]+ao[2]`, flip the triangulation (canonical 0fps.net sum-comparison)
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

## Story 5.4: Non-Cubic Block Model Meshing

**As a** developer,
**I want** a second meshing path for blocks that aren't full cubes,
**so that** torches, flowers, slabs, stairs, fences, and other shaped blocks render correctly.

**Why this must be done now:**
The binary greedy mesher (5.3) only handles full cubes — it merges coplanar faces of the same type. Non-cubic blocks (torches, cross-pattern flowers, half-slabs, stair geometry) need a completely different meshing approach. If we add this after the meshing pipeline is async and uploaded, every interface (ChunkMesh, snapshot, upload, shader) needs retrofitting.

**Acceptance Criteria:**

**Block model definition:**
- `BlockModelType` enum: `FullCube`, `Slab`, `Stair`, `Cross`, `Torch`, `Connected`, `JsonModel`, `MeshModel`, `Custom`
- `BlockDefinition` gains `modelType` field + `customModel` optional
- `FullCube`: handled by binary greedy mesher (unchanged)
- Other types: handled by `ModelMesher` — generates vertex lists from model definitions

**Model types for V1:**
- `Slab`: half-height box (top or bottom based on block state `half` property)
- `Stair`: 3 boxes combined (base + two step parts), state-dependent rotation
- `Cross`: two diagonal quads intersecting (flowers, tall grass, saplings)
- `Torch`: thin vertical box + angled variant for wall torches
- `Connected`: fences, walls, glass panes — base center post + conditional arm boxes on 4 sides depending on adjacent block connectivity (Luanti `NODEBOX_CONNECTED` equivalent)
- `Custom`: list of boxes defined in Lua (`node_box` style like Luanti)
- `JsonModel`: Minecraft-style JSON model with rotated cuboid elements and per-face UV mapping
- `MeshModel`: external `.obj` mesh loaded from file (Luanti `NDT_MESH` equivalent)

**JsonModel — rotated cuboid elements (Minecraft-style):**

```lua
voxel.register_block({
    id = "mymod:windmill_blade",
    model_type = "json_model",
    model = {
        elements = {
            {
                from = {6, 0, 6},
                to = {10, 16, 10},
                rotation = { origin = {8, 8, 8}, axis = "y", angle = 45 },
                faces = {
                    north = { texture = "blade_side", uv = {6, 0, 10, 16} },
                    south = { texture = "blade_side", uv = {6, 0, 10, 16} },
                    east  = { texture = "blade_side" },
                    west  = { texture = "blade_side" },
                    up    = { texture = "blade_top" },
                    down  = { texture = "blade_bottom" },
                },
            },
            {
                from = {7, 4, 0},
                to = {9, 12, 16},
                faces = {
                    north = { texture = "blade_end" },
                    south = { texture = "blade_end" },
                    east  = { texture = "blade_flat" },
                    west  = { texture = "blade_flat" },
                },
            },
        },
    },
})
```

**JsonModel C++ implementation:**
```cpp
struct ModelElement {
    glm::vec3 from;                    // Min corner (0-16 range, in 1/16th block units)
    glm::vec3 to;                      // Max corner
    struct {
        glm::vec3 origin;
        char axis;                     // 'x', 'y', 'z'
        float angle;                   // Must be -45, -22.5, 0, 22.5, or 45
    } rotation;
    struct FaceData {
        std::string texture;
        std::array<float, 4> uv;      // {u1, v1, u2, v2} — defaults to face bounds
        int rotation = 0;              // UV rotation: 0, 90, 180, 270
        bool cullface = true;          // Cull if neighbor is solid on this face
    };
    std::unordered_map<std::string, FaceData> faces;  // "north","south","east","west","up","down"
};

struct BlockModel {
    std::vector<ModelElement> elements;
    bool ambientOcclusion = true;
};
```

- Model elements are pre-baked at registration time into a vertex list (not computed during meshing)
- The rotation is applied to the vertices at registration → stored as transformed positions
- Block state variants: each block state can reference a different model or the same model with a Y rotation
- `model_variants` in registration maps state values to models:
```lua
model_variants = {
    ["facing=north"] = { model = "mymod:blade", y_rotation = 0 },
    ["facing=east"]  = { model = "mymod:blade", y_rotation = 90 },
    ["facing=south"] = { model = "mymod:blade", y_rotation = 180 },
    ["facing=west"]  = { model = "mymod:blade", y_rotation = 270 },
}
```

**MeshModel — external .obj loading (Luanti NDT_MESH equivalent):**

```lua
voxel.register_block({
    id = "mymod:statue",
    model_type = "mesh_model",
    mesh = "models/statue.obj",           -- Relative to mod directory
    textures = { all = "statue_tex.png" },
    collision_boxes = {                    -- Manual collision (mesh collision too expensive)
        { 0.2, 0, 0.2,  0.8, 1.5, 0.8 },
    },
})
```

- `.obj` files loaded via a simple OBJ parser (position + UV + normal, no materials — texture from BlockDefinition)
- Vertex limit per model: 1024 vertices max (prevents giant meshes that kill performance)
- Models are loaded once at startup, stored in a `ModelRegistry`
- During meshing: non-cube blocks look up their pre-baked vertex list from `ModelRegistry`, apply block position offset, append to the section's model vertex buffer
- `ModelRegistry::loadOBJ(path) → Result<ModelHandle>` — parses, validates vertex count, stores
- Collision: uses `collision_boxes` from BlockDefinition (not mesh geometry — too expensive for physics)

```lua
-- Custom nodebox (axis-aligned boxes only — simpler than JsonModel)
voxel.register_block({
    id = "mymod:shelf",
    model_type = "custom",
    node_boxes = {
        { 0, 0, 0,    1, 0.0625, 1 },    -- bottom plate
        { 0, 0.5, 0,  1, 0.5625, 1 },     -- middle shelf
    },
})

-- Connected nodebox (fence)
voxel.register_block({
    id = "base:wooden_fence",
    model_type = "connected",
    connects_to = { "group:fence", "group:solid" },
    connect_boxes = {
        center = { 0.375, 0, 0.375,  0.625, 1, 0.625 },
        north  = { 0.375, 0.2, 0,    0.625, 0.8, 0.375 },
        south  = { 0.375, 0.2, 0.625, 0.625, 0.8, 1 },
        east   = { 0.625, 0.2, 0.375, 1, 0.8, 0.625 },
        west   = { 0, 0.2, 0.375,    0.375, 0.8, 0.625 },
    },
})
```

**Connected model meshing:**
- During meshing, for each Connected block: check 4 horizontal neighbors against `connects_to` list
- `connects_to` supports group matching: `"group:solid"` matches any block with `groups["solid"] > 0`
- Emit center box always + conditional arm boxes based on which neighbors connect
- Connection state can also be stored in block states (4 bits, one per direction) for faster lookup during meshing

**Meshing integration:**
- `MeshBuilder::buildSection()` first runs binary greedy meshing on FullCube blocks, then iterates non-cube blocks and appends their model vertices
- Non-cube blocks are excluded from the greedy merge bitmask (treated as "air" for merging purposes, but their faces are still culled against adjacent solid blocks)
- Non-cube vertices use the same 8-byte packed quad format where possible, or a secondary vertex format for arbitrary geometry
- `ChunkMesh` gains a second buffer: `std::vector<ModelVertex> modelVertices` for non-cube geometry
- Both buffers uploaded to gigabuffer (at separate offsets tracked in ChunkRenderInfo)

**Face culling for non-cubes:**
- A slab adjacent to a full cube: the cube's face toward the slab IS emitted (slab doesn't fully cover the face)
- A full cube adjacent to a slab: the slab's face toward the cube is NOT emitted (cube fully covers it)
- `BlockDefinition::isFullFace(face)` returns true only for faces that fully cover the 1×1 area

**Unit tests:** Section with a slab on stone → slab top face emitted, stone side face emitted, stone top face toward slab emitted, slab bottom face NOT emitted.

---

## Story 5.5: Block Tinting + Waving Animation in Vertex Format

**As a** developer,
**I want** per-vertex color tint and waving animation flags stored in mesh data,
**so that** grass/leaves change color by biome AND leaves/plants/water animate with wind.

**Why this must be done now:**
Block tinting multiplies a color onto the texture (green grass in plains, brown in desert). Waving applies a vertex displacement in the shader (leaves sway, plants bob, water surface ripples). Both need bits in the quad format. If we add them after the vertex format is finalized and shaders are written, every quad needs reformatting and every shader needs updating.

**Acceptance Criteria:**

**Vertex format extension — complete bit allocation of reserved bits:**
- The 15 reserved bits in the quad format (bits 49–63) are now fully allocated:
  - Bits 49–56: light data (8 bits — claimed by Story 8.0: skyLight:4 + blockLight:4)
  - Bits 57–59: tint color index (3 bits — 0 = no tint, 1–7 = tint palette index)
  - Bits 60–61: waving type (2 bits — 0=none, 1=leaves, 2=plants, 3=liquid surface)
  - Bits 62–63: reserved (2 bits — future use: connected state, random offset, etc.)

**Tinting (unchanged from before):**
- `TintPalette`: small table (max 8 entries) mapping index → RGB color, uploaded as a small SSBO or UBO
  - Index 0: no tint (white, 1.0/1.0/1.0)
  - Index 1: grass tint (varies by biome temperature/humidity)
  - Index 2: foliage tint (leaves)
  - Index 3: water tint
  - Index 4–7: reserved for mods

**Waving:**
- During meshing, the mesher reads `BlockDefinition::waving` (0–3)
- Packed into bits 60–61 of each quad
- Values: 0=static, 1=leaves (slow XZ sway), 2=plants (faster Y+XZ bob), 3=liquid surface (wave pattern)
- The actual animation is applied in `chunk.vert` (Story 6.2) — this story only stores the flag

**Meshing integration:**
- During meshing, the mesher queries `BlockDefinition::tintIndex` (0 = none, 1 = grass, 2 = foliage, 3 = water)
- The tint index is packed into bits 57–59 of each quad
- For tinted blocks, the mesher also looks up the biome at the block's position to determine which palette entry to use
- Palette is per-chunk (each chunk may span a biome boundary — use nearest biome sample)

**Shader integration (wired in Story 6.2):**
- `chunk.frag` reads tint index from vertex data
- Looks up color from tint palette SSBO/UBO
- Multiplies `albedo.rgb *= tintColor.rgb`
- Non-tinted blocks (index 0) multiply by white (no change)

**Lua API (wired in Story 9.2):**
```lua
voxel.register_block({
    id = "base:grass",
    tint = "grass",        -- Uses grass biome coloring
    textures = { top = "grass_top.png", side = "grass_side.png", bottom = "dirt.png" },
})
```

**Biome color calculation:**
- Temperature/humidity at block position → color via a gradient texture or LUT
- Minecraft uses a triangular gradient map (grass.png, foliage.png) indexed by temperature × humidity
- V1: simple LUT with 8 biome types × 3 tint types = 24 colors, hardcoded
- Future: data-driven gradient textures loaded from assets

**Unit tests:** Tint index packing/unpacking roundtrip. Grass block in plains biome → tint index 1, grass block in desert → tint index 1 (same index, different palette color per chunk).

---

## Story 5.6: Async Mesh Jobs via enkiTS

**As a** developer,
**I want** meshing to run on worker threads via the job system,
**so that** chunk meshing never blocks the main thread.

**Acceptance Criteria:**

**JobSystem infrastructure (prerequisite — nothing async exists yet):**
- Create `voxel::core::JobSystem` class in `engine/include/voxel/core/JobSystem.h` wrapping `enki::TaskScheduler`
- `JobSystem::init()` auto-detects core count, `shutdown()` waits for completion
- Owned by GameApp, passed by reference to ChunkManager and any future async consumer
- Single instance for the entire engine — not a singleton, explicit ownership

**ConcurrentQueue (prerequisite — needed for worker→main result delivery):**
- Create `voxel::core::ConcurrentQueue<T>` in `engine/include/voxel/core/ConcurrentQueue.h`
- V1 implementation: `std::mutex` + `std::deque` (simple, correct, sufficient for now)
- API: `push(T&&)`, `tryPop() → std::optional<T>`, `size()`, `empty()`
- Unit tests: single-producer/single-consumer ordering, multi-producer correctness

**Async meshing:**
- `MeshChunkTask : enki::ITaskSet` — takes snapshot of section + 6 neighbors, produces ChunkMesh
- Snapshot creation: copy section data + neighbor boundary slices before dispatch (immutable input)
- Result delivered via `ConcurrentQueue<MeshResult>` (MPSC: many workers → one main thread consumer)
- Main thread polls results in `ChunkManager::update()`, integrates max N per frame
- Priority: closer chunks mesh first (enkiTS priority levels)
- Cancellation: if chunk unloaded before mesh completes, discard result
- Dirty tracking: re-queue meshing when block changed in section or neighbor

---

## Story 5.7: Mesh Upload to Gigabuffer

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
