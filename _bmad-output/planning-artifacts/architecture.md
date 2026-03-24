# Architecture — VoxelForge Engine

## Overview

3-layer data-oriented architecture with EnTT ECS for entities, async chunk pipeline via enkiTS, and Vulkan 1.3 GPU-driven renderer using indirect drawing with a gigabuffer pattern.

```
┌──────────────────────────────────────────────────────┐
│  Game Layer (Lua/sol2)                               │
│  Gameplay, mods, content definitions, event handlers │
├──────────────────────────────────────────────────────┤
│  Engine Layer                                        │
│  Vulkan Renderer, ChunkManager, Physics, ScriptHost, │
│  ECS (EnTT), Audio stub, Network stub, Input         │
├──────────────────────────────────────────────────────┤
│  Core Layer (pure C++, zero external dependencies)   │
│  Types, Math, Allocators, Containers, Job System,    │
│  Assert, Result, Log, Platform abstraction           │
└──────────────────────────────────────────────────────┘
```

**Absolute rule**: Core depends on nothing. Engine depends on Core only. Game depends on Engine. No reverse dependencies, no shortcuts. Each layer is a separate CMake target with enforced dependency direction.

---

## Project Tree

```
VoxelForge/
├── CLAUDE.md                          # AI agent constitution
├── CMakeLists.txt                     # Root CMake
├── CMakePresets.json                  # Debug, Release, RelWithDebInfo
├── vcpkg.json                         # All dependencies
├── .clang-format
├── .clang-tidy
├── .editorconfig
├── _bmad/                             # BMAD agents, workflows, config
├── _bmad-output/                      # BMAD artifacts (this file lives here)
├── cmake/
│   ├── CompilerWarnings.cmake         # -Wall -Wextra -Werror, /W4 /WX
│   └── Sanitizers.cmake               # ASan, UBSan, TSan toggles
├── engine/
│   ├── CMakeLists.txt                 # Static library target
│   ├── include/voxel/
│   │   ├── core/
│   │   │   ├── Types.h                # uint8, int32, etc. typedefs
│   │   │   ├── Assert.h               # VX_ASSERT, VX_FATAL macros
│   │   │   ├── Log.h                  # VX_LOG_* macros wrapping spdlog
│   │   │   ├── Result.h               # Result<T> = std::expected<T, EngineError>
│   │   │   └── Allocator.h            # PoolAllocator, ArenaAllocator
│   │   ├── math/
│   │   │   ├── MathTypes.h            # GLM aliases, helpers
│   │   │   ├── AABB.h                 # Intersection, contains, expand, swept
│   │   │   ├── Ray.h                  # Origin + direction
│   │   │   └── CoordUtils.h           # worldToChunk, localToWorld, blockToIndex
│   │   ├── world/
│   │   │   ├── Block.h                # BlockDefinition struct
│   │   │   ├── BlockRegistry.h        # String ID ↔ numeric ID registry
│   │   │   ├── ChunkSection.h         # 16³ flat array storage
│   │   │   ├── ChunkColumn.h          # Stack of 16 sections
│   │   │   ├── ChunkManager.h         # Load/unload, spatial hashmap
│   │   │   ├── PaletteCompression.h   # Variable bits-per-entry codec
│   │   │   ├── WorldGenerator.h       # Noise pipeline + biome selection
│   │   │   └── LightMap.h             # Sky + block light, BFS propagation
│   │   ├── renderer/
│   │   │   ├── VulkanContext.h         # Instance, device, queues, swapchain
│   │   │   ├── Gigabuffer.h           # Single VkBuffer + VmaVirtualBlock
│   │   │   ├── StagingBuffer.h        # Upload pipeline, transfer queue
│   │   │   ├── Renderer.h             # Frame orchestration, draw submission
│   │   │   ├── IndirectBuffer.h       # Draw command buffer, count buffer
│   │   │   ├── ChunkMesh.h            # Binary greedy meshing output
│   │   │   ├── MeshBuilder.h          # Binary greedy meshing algorithm
│   │   │   ├── TextureArray.h         # VK_IMAGE_VIEW_TYPE_2D_ARRAY manager
│   │   │   ├── ShaderManager.h        # SPIR-V loading, pipeline creation
│   │   │   ├── Camera.h               # View/proj matrices, frustum extraction
│   │   │   └── GBuffer.h              # Deferred rendering targets
│   │   ├── ecs/
│   │   │   ├── Components.h           # Position, Velocity, AABB, Health, etc.
│   │   │   └── Systems.h              # Physics, movement, lifetime systems
│   │   ├── physics/
│   │   │   ├── Collision.h            # AABB swept collision, axis clipping
│   │   │   └── DDA.h                  # 3D DDA raycasting (Amanatides & Woo)
│   │   ├── scripting/
│   │   │   ├── ScriptEngine.h         # sol2 state, mod loading, sandbox
│   │   │   └── LuaBindings.h          # C++ ↔ Lua API bindings
│   │   ├── input/
│   │   │   └── InputManager.h         # GLFW callbacks, key/mouse state
│   │   └── game/
│   │       ├── GameLoop.h             # Fixed timestep tick + render interpolation
│   │       ├── CommandQueue.h         # Serializable GameCommand pipeline
│   │       └── EventBus.h            # Typed publish/subscribe system
│   └── src/                           # Mirror of include/ for implementations
├── game/
│   ├── CMakeLists.txt                 # Executable target, links engine
│   └── src/main.cpp                   # Entry point, bootstraps everything
├── tests/
│   ├── CMakeLists.txt                 # Catch2 v3 test target
│   ├── core/                          # TestResult.cpp, TestAllocators.cpp
│   ├── math/                          # TestAABB.cpp, TestCoordUtils.cpp
│   ├── world/                         # TestChunk.cpp, TestPalette.cpp, TestMeshing.cpp, TestWorldGen.cpp
│   ├── physics/                       # TestCollision.cpp, TestDDA.cpp
│   └── renderer/                      # TestGigabuffer.cpp (CPU-side logic only)
├── assets/
│   ├── shaders/
│   │   ├── chunk.vert                 # Vertex pulling from SSBO
│   │   ├── chunk.frag                 # Texture array sampling + AO
│   │   ├── cull.comp                  # Frustum culling, indirect buffer fill
│   │   ├── gbuffer.vert / gbuffer.frag  # Deferred geometry pass
│   │   └── lighting.frag              # Deferred lighting pass
│   ├── textures/
│   │   └── blocks/                    # 16×16 PNGs, one per block face
│   └── scripts/
│       ├── base/                      # Base game Lua scripts
│       │   └── init.lua               # Register default blocks/items
│       └── mods/                      # User mods (each in subfolder)
├── docs/
│   └── api/                           # Generated Doxygen output
└── tools/
    └── shader_compile.sh              # glslangValidator batch compilation
```

---

## System 1: Chunk Storage

### ChunkSection (16³ = 4096 blocks)

```cpp
struct ChunkSection {
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE;
    uint16_t blocks[VOLUME]; // 8 KB, indexed y*256 + z*16 + x
};
```

- Flat array is the runtime format — fastest for meshing iteration (sequential memory access)
- Benchmarked faster than octrees for meshing by Nick McDonald (nickmcd.me)
- `uint16_t` supports 65535 block types — mapped through BlockRegistry

### ChunkColumn

- Stack of 16 ChunkSections = 256 blocks tall
- Sections are **null-initialized** — allocated on first write (empty sky sections = no allocation)
- Keyed by `glm::ivec2{chunkX, chunkZ}` in ChunkManager

### Palette Compression (serialization + distant chunks)

Each section maintains a local palette mapping `localIndex → globalBlockId`:

| Block types in section | Bits per entry | Memory per section |
|----------------------|----------------|-------------------|
| 1 (uniform) | 0 (single-value) | ~8 bytes (just palette) |
| 2 | 1 | 512 bytes |
| 3–4 | 2 | 1 KB |
| 5–16 | 4 | 2 KB |
| 17–256 | 8 | 4 KB |
| 257+ | Direct 16-bit | 8 KB (no palette) |

Disk format: palette header + BitBuffer + RLE + LZ4 → compression ratios 2x–30x.

### Spatial Key Hashing

`glm::ivec2` hashed with combined XOR-shift (not `std::hash` default which is poor for integer vectors):

```cpp
struct ChunkCoordHash {
    size_t operator()(const glm::ivec2& v) const {
        size_t h = std::hash<int>{}(v.x);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
std::unordered_map<glm::ivec2, std::unique_ptr<ChunkColumn>, ChunkCoordHash> m_chunks;
```

---

## System 2: Async Chunk Pipeline

```
Main Thread                          Worker Threads (enkiTS)
───────────                          ──────────────────────
ChunkManager::update()
│
├─ Determine load/unload set         
│  (spiral from player, nearest      
│   first, max N requests/frame)     
│                                    
├─ Enqueue load requests ──────────► Generate (noise terrain)
│                                    │
│                                    ▼
│                                    Populate (structures, ores, cross-chunk)
│                                    │
│                                    ▼
│                                    Light (BFS flood-fill sky + block)
│                                    │
│                                    ▼
│                                    Mesh (binary greedy meshing)
│                                    │
├─ Integrate results ◄───────────── Return MeshData + LightData
│  (max N per frame)                 
│                                    
├─ GPU Upload                        
│  (staging → gigabuffer offset      
│   via transfer queue)              
│                                    
└─ Update indirect draw buffer       
   (chunk now renderable)            
```

### Pipeline Rules

1. Each stage is an isolated enkiTS job — dispatchable on any worker thread
2. Jobs read **immutable snapshots** — neighbor sections copied before dispatch (3×3×3 for meshing+lighting)
3. Main thread integrates results — never mutate world state from workers
4. **Rate limiting**: max N GPU uploads per frame (default 8, configurable)
5. **Priority by distance**: chunks closer to player process first
6. **Cancellation**: if player moves, cancel jobs for chunks now out of range
7. **Dirty tracking**: when a block changes, mark that section + neighbors as needing remesh

### enkiTS Integration

```cpp
enki::TaskScheduler g_scheduler;
g_scheduler.Initialize(); // Auto-detects core count

// Task with priority
struct MeshChunkTask : enki::ITaskSet {
    ChunkSnapshot snapshot;
    MeshResult result;
    void ExecuteRange(enki::TaskSetPartition, uint32_t) override {
        result = binaryGreedyMesh(snapshot);
    }
};

// Dispatch at priority 2 (0=highest, 4=lowest)
auto task = std::make_unique<MeshChunkTask>(snapshot);
g_scheduler.AddTaskSetToPipe(task.get());
```

---

## System 3: ECS (EnTT)

### What goes in the ECS

```
✅ Players      — Position, Velocity, AABB, Camera, Inventory
✅ Dropped items — Position, Velocity, ItemRef, Lifetime
✅ Projectiles   — Position, Velocity, Damage (future)
✅ Chunk meta    — ChunkCoord, LoadState, DirtyFlags, LODLevel
✅ Block entities — ChestContent, FurnaceState (future)

❌ Voxel data    — dedicated flat array in ChunkManager
❌ Chunk meshes  — managed by Renderer/Gigabuffer
❌ World gen state — internal to WorldGenerator
❌ Light maps    — per-section byte arrays in ChunkColumn
```

### Component Design

Components are plain POD structs, no virtuals, no methods beyond trivial accessors:

```cpp
struct Position { glm::dvec3 value; };          // Double for world precision
struct Velocity { glm::vec3 value; };
struct BoundingBox { glm::vec3 halfExtents; };  // Centered on Position
struct Gravity { float acceleration = 9.81f; };
struct OnGround { bool value = false; };

// Tags (zero-size components)
struct IsPlayer {};
struct NeedsPhysicsUpdate {};
struct MarkedForDestruction {};
```

### Systems

Free functions iterating EnTT views — called explicitly from GameLoop in defined order:

```cpp
void gravitySystem(entt::registry& reg, float dt);
void movementSystem(entt::registry& reg, float dt);
void collisionSystem(entt::registry& reg, ChunkManager& world, float dt);
void lifetimeSystem(entt::registry& reg, float dt);
void cleanupSystem(entt::registry& reg);
```

---

## System 4: Block Registry

```cpp
struct BlockDefinition {
    std::string stringId;              // "base:stone", "mymod:crystal"
    uint16_t numericId;                // Assigned at runtime by registry
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    uint8_t lightEmission = 0;         // 0–15
    uint8_t lightFilter = 15;          // 0 = fully opaque to light, 15 = transparent
    float hardness = 1.0f;
    float breakTime = 1.0f;            // Seconds to break by hand
    uint16_t textureIndices[6];        // Index into texture array, per face [+X,-X,+Y,-Y,+Z,-Z]
    std::string dropItem;              // String ID of item dropped on break
};

class BlockRegistry {
    std::vector<BlockDefinition> m_blocks;                 // Indexed by numericId
    std::unordered_map<std::string, uint16_t> m_nameToId;  // "base:stone" → 42
public:
    uint16_t registerBlock(BlockDefinition def);
    const BlockDefinition& getBlock(uint16_t id) const;
    uint16_t getIdByName(std::string_view name) const;
    uint16_t blockCount() const;
};
```

- Populated at startup: JSON base definitions first, then Lua mod scripts
- Numeric IDs are session-stable but NOT persistent across saves — palette compression handles mapping
- Namespace format `"namespace:name"` prevents mod collisions

---

## System 5: Vulkan Renderer

### Init Stack

```
1. volkInitialize()
2. vk-bootstrap → Instance (validation layers in debug)
3. vk-bootstrap → PhysicalDevice (require Vulkan 1.3 features)
4. vk-bootstrap → LogicalDevice + Queues (graphics + transfer + compute)
5. volkLoadDevice(device)
6. VMA allocator (with BDA support + volk function import)
7. Swapchain (via vk-bootstrap)
8. Gigabuffer allocation (256–400 MB DEVICE_LOCAL)
9. Shared quad index buffer (0,1,2,2,3,0 × MAX_QUADS)
10. Shader pipelines (chunk.vert/frag, cull.comp, gbuffer, lighting)
11. Texture array (load block textures as layers)
12. Descriptor sets (bindless texture array, SSBOs)
13. Dear ImGui init (Vulkan backend)
```

### Vulkan 1.3 Required Features

| Feature | Usage |
|---------|-------|
| Dynamic Rendering | Skip VkRenderPass/VkFramebuffer boilerplate |
| Synchronization2 | Simplified pipeline barriers |
| Buffer Device Address (BDA) | Vertex pulling, GPU pointer access |
| Descriptor Indexing | Bindless texture array sampling |
| Maintenance4 | `maxBufferSize` query for gigabuffer validation |

### Gigabuffer Pattern

Single `VkBuffer` of 256–400 MB, `DEVICE_LOCAL`, holding all chunk meshes. Sub-allocation tracked CPU-side via `VmaVirtualBlock`:

```cpp
VmaVirtualBlockCreateInfo vbInfo{};
vbInfo.size = GIGABUFFER_SIZE;
vmaCreateVirtualBlock(&vbInfo, &m_virtualBlock);

// Allocate space for a chunk mesh
VmaVirtualAllocationCreateInfo allocInfo{};
allocInfo.size = meshDataSize;
allocInfo.alignment = 16;
VmaVirtualAllocation allocation;
VkDeviceSize offset;
vmaVirtualAllocate(m_virtualBlock, &allocInfo, &allocation, &offset);
// Upload meshData to gigabuffer at offset via staging buffer
```

Advantages: single bind for all draws, 32-bit offsets (not 64-bit BDA pointers), no driver-level fragmentation, simplified transfer commands.

### Indirect Rendering Pipeline

```
Per frame:
1. Reset draw count to 0 (atomic in buffer)
2. Dispatch cull.comp — iterates all chunk infos
   ├─ Frustum test (bounding sphere vs 6 planes)
   ├─ If visible: atomicAdd(drawCount, 1) → fill VkDrawIndexedIndirectCommand
   └─ Write per-draw data (chunk world pos, material ID) to SSBO
3. Pipeline barrier (compute → indirect)
4. vkCmdDrawIndexedIndirectCount(indirectBuffer, countBuffer, maxDrawCount)
   └─ GPU decides BOTH draw parameters AND number of draws
5. Vertex shader reads quads from gigabuffer via gl_VertexID + vertex pulling
6. Fragment shader samples texture array, applies AO + lighting
```

### Shader Architecture

**chunk.vert** — Vertex pulling:
- Input: none (no vertex attributes)
- Reads packed quad data from SSBO via `gl_VertexID / 4` (quad index) and `gl_VertexID % 4` (corner)
- Per-draw data (chunk world position) via SSBO indexed by `gl_DrawID`
- Outputs: world position, UV + texture layer, AO value, normal

**chunk.frag** — G-Buffer write:
- Samples `sampler2DArray` with `vec3(u, v, textureLayer)`
- Writes albedo, normal, depth to G-Buffer attachments
- Applies vertex AO as multiplier on albedo

**cull.comp** — Frustum culling:
- Workgroup size: 64
- Reads chunk bounding sphere, tests against 6 frustum planes
- Atomic append to indirect command buffer

**lighting.frag** — Deferred lighting:
- Reads G-Buffer (albedo, normal, depth)
- Combines block light + sky light (interpolated from vertices)
- Day/night multiplier on sky light
- Basic directional sun light (dot product with normal)

### Vertex Format: 8 Bytes Per Quad

Binary greedy meshing output — one `uint64_t` per quad:

```
Bits [0–5]   : x position (6 bits, 0–63)
Bits [6–11]  : y position (6 bits, 0–63)
Bits [12–17] : z position (6 bits, 0–63)
Bits [18–23] : width - 1 (6 bits, merged quad width)
Bits [24–29] : height - 1 (6 bits, merged quad height)
Bits [30–37] : block type / texture index (8 bits)
Bits [38–40] : face direction (3 bits, 6 possible faces)
Bits [41–48] : AO values packed (4 × 2 bits = 8 bits)
Bits [49–63] : reserved (lighting data, flags)
```

Vertex shader reconstructs 4 corners from quad index + corner index, using face direction to determine which axes are width/height.

### Ambient Occlusion

Per-vertex AO calculated during meshing (4 possible values per vertex):

```cpp
int vertexAO(bool side1, bool corner, bool side2) {
    if (side1 && side2) return 0;  // Maximum occlusion
    return 3 - int(side1) - int(side2) - int(corner);
}
// LUT in shader: float ao[4] = { 0.2, 0.5, 0.8, 1.0 };
```

Critical: when AO values are anisotropic, flip quad diagonal to minimize interpolation artifacts. Flip when `abs(ao[0]-ao[3]) > abs(ao[1]-ao[2])`.

### Texture Arrays

`VK_IMAGE_VIEW_TYPE_2D_ARRAY` — one layer per block texture (16×16 pixels):

- No atlas bleeding
- Independent mipmaps per layer
- Normalized 0–1 UVs per layer
- Single bind, single draw call
- Sampled via `texture(blockTextures, vec3(u, v, float(layerIndex)))`
- Vulkan supports 2048+ layers easily

### Deferred Rendering

G-Buffer layout:

| Attachment | Format | Content |
|-----------|--------|---------|
| RT0 | RGBA8_SRGB | Albedo.rgb + AO.a |
| RT1 | RG16_SFLOAT | Normal.xy (octahedral encoding) |
| Depth | D32_SFLOAT | Hardware depth |

Lighting pass reads G-Buffer, applies block light + sky light + sun direction + day/night cycle.

---

## System 6: World Generation

### Noise Pipeline

```
1. Continent noise (2D Simplex, freq 0.001, 6 octaves)
   → Raw elevation value [-1, 1]

2. Spline remapping
   → Maps raw noise to actual height distribution
   → Spline control points define plains/hills/mountains distribution

3. Temperature noise (2D Simplex, freq 0.005, 4 octaves)
   Humidity noise (2D Simplex, freq 0.005, 4 octaves, different seed)
   → Both in [-1, 1], used for biome selection

4. Whittaker diagram lookup
   → (temperature, humidity) → BiomeType enum
   → Each biome defines: base height modifier, surface block, sub-surface block,
     decoration rules, tree types, ore distribution

5. Biome blending at boundaries
   → Sample biomes in 5×5 area around each column
   → Weighted average of height functions by distance to biome center

6. 3D detail noise (freq 0.02, 3 octaves)
   → Carve caves where noise > threshold
   → Create overhangs where 3D noise diverges from 2D heightmap

7. Surface decoration
   → Per-biome rules: tree type + density, grass/flower probability, ore veins
   → Trees: multi-block structures placed during Populate stage (cross-chunk aware)
```

### Determinism

Same seed MUST produce same world:
- All noise functions seeded deterministically from world seed
- Structure placement uses seeded RNG per chunk coordinate
- No floating-point order-of-operations variance (use consistent evaluation order)

---

## System 7: Physics

### AABB Swept Collision

Never allows intersection — clips movement delta against voxel AABBs:

```
For each axis (Y first for gravity, then X, then Z):
  1. Extend player AABB by velocity vector on this axis
  2. Collect all solid blocks overlapping the extended AABB
  3. Sort candidates by distance along movement direction
  4. For each candidate, clip the delta on this axis
  5. Apply clipped delta, update position
  6. If clipped to zero on Y-axis and moving down → set OnGround = true
```

### DDA 3D Raycasting

Amanatides & Woo algorithm for exact voxel grid traversal:

```
Input: ray origin, ray direction, max distance
For each step:
  1. Compute tMax for each axis (distance to next voxel boundary)
  2. Advance along axis with smallest tMax
  3. Test block at current voxel position
  4. If solid: return hit (block pos, previous pos for placement, face, distance)
  5. If distance > max: return miss
```

---

## System 8: Lighting

### Dual Light System

Two independent 4-bit light channels per block, packed as 1 byte:

```cpp
struct LightData {
    uint8_t packed; // [sky:4 | block:4]
    uint8_t sky() const { return (packed >> 4) & 0xF; }
    uint8_t block() const { return packed & 0xF; }
};
```

### BFS Propagation

**Block light**: seed from light-emitting blocks (torch=14, glowstone=15), BFS with -1 attenuation per step, stop at 0 or opaque block.

**Sky light**: seed at 15 for all surface-exposed blocks, propagate downward (no attenuation for direct downward), BFS horizontally with -1 attenuation.

**On block place/break**: remove affected light (reverse BFS to find affected area), then re-propagate from remaining sources.

### Integration with Renderer

Light values are baked into mesh vertices during meshing (averaged from 4 neighboring light values per vertex). Fragment shader multiplies albedo by light value mapped through a gamma curve.

---

## System 9: Scripting (Lua)

### Architecture

```
ScriptEngine (C++)
├── sol::state (Lua VM via LuaJIT)
├── Sandbox (disabled: os, io, debug, loadfile)
├── API tables
│   ├── voxel.register_block(def)
│   ├── voxel.register_item(def)
│   ├── voxel.get_block(x,y,z)
│   ├── voxel.set_block(x,y,z,id)
│   ├── voxel.raycast(origin, dir, dist)
│   └── voxel.on(event_name, callback)
├── Mod loader
│   ├── Discover mods in assets/scripts/mods/
│   ├── Load order: base/ first, then mods alphabetically
│   └── Each mod gets isolated environment (no global pollution)
└── Hot-reload
    └── Re-execute mod scripts on command, re-register everything
```

### Rate Limiting

Lua scripts are rate-limited on expensive operations:
- `set_block`: max 1000 calls per tick per mod
- `raycast`: max 100 calls per tick per mod
- Exceeded limits → log warning, skip call, don't crash

---

## System 10: Network-Readiness

### Command Pattern

```cpp
struct GameCommand {
    enum class Type : uint8_t {
        PlaceBlock, BreakBlock, MovePlayer, UseItem,
        ChatMessage, ToggleSprint, Jump
    };
    Type type;
    uint32_t playerId;
    uint32_t tick;
    std::variant<PlaceBlockData, BreakBlockData, MoveData, UseItemData, ...> payload;
};

class CommandQueue {
    ConcurrentQueue<GameCommand> m_queue;
public:
    void push(GameCommand cmd);
    std::optional<GameCommand> tryPop();
};
```

All input handlers push commands to the queue. The simulation loop pops and processes them. Nothing mutates game state directly.

### Game Loop

```cpp
void GameLoop::run() {
    constexpr double TICK_RATE = 1.0 / 20.0; // 50ms per tick
    double accumulator = 0.0;

    while (m_running) {
        double frameTime = m_timer.elapsed();
        accumulator += frameTime;

        // Input → commands
        m_input.poll();

        // Fixed-step simulation
        while (accumulator >= TICK_RATE) {
            m_simulation.tick(TICK_RATE); // Processes command queue
            accumulator -= TICK_RATE;
        }

        // Render with interpolation
        double alpha = accumulator / TICK_RATE;
        m_renderer.render(alpha); // Lerps between prev and current state
    }
}
```

### Event Bus

```cpp
class EventBus {
    std::unordered_map<EventType, std::vector<std::function<void(const Event&)>>> m_listeners;
public:
    void subscribe(EventType type, std::function<void(const Event&)> callback);
    void publish(const Event& event);
};
```

Used for inter-system communication: block placed → update light → remesh neighbors → notify Lua hooks.

---

## Culling Strategies

### 1. Frustum Culling (compute shader, every frame)

Test chunk bounding sphere against 6 frustum planes. Already integrated in the indirect rendering compute pass.

### 2. Tommo's Cave Culling (CPU, on mesh build)

**Phase 1 — Connectivity graph** (per section, computed during meshing):
- Flood-fill through non-opaque blocks
- Record which section faces connect to which other faces
- 15 possible pairs (C(6,2)), stored as 16-bit bitmask per section

**Phase 2 — Visibility BFS** (per frame, CPU):
- Start from camera section
- BFS to neighbors: only traverse if connectivity graph confirms entry_face → exit_face
- Only traverse in direction away from camera (dot(normal, viewDir) < 0)
- Eliminates 50–99% of underground geometry

### 3. Hierarchical Z-Buffer (GPU, optional/future)

Two-pass occlusion culling:
1. Render chunks visible in previous frame → Z-buffer
2. Build HZB mip chain via compute shader
3. Test all chunks against HZB
4. Render newly visible chunks

---

## LOD Strategy

### POP Buffers (V1 target)

Snap vertex positions to coarser grids at distance:
- LOD 0: full resolution (near field)
- LOD 1: snap to 2-block grid
- LOD 2: snap to 4-block grid
- Geomorphing in vertex shader for seamless transitions

### Far-Draw Sprites (future)

For very distant chunks: compress each visible block to 4 bytes, ray/box intersection in pixel shader. No mesh generation needed.

---

## Architecture Decision Records (ADR)

### ADR-001: C++20 with Selective C++23 Features

**Context**: Which C++ standard to target.
**Decision**: C++20 as base with `-std=c++20`. Opt-in C++23 features: `std::expected`, `std::mdspan`, multidimensional `operator[]`.
**Rationale**: C++20 has the best cross-compiler support (MSVC 2022, GCC 13+, Clang 16+). The selected C++23 features are already supported by all three compilers. C++20 modules excluded due to immature CMake/IDE tooling.
**Consequences**: Need polyfill for `std::expected` if targeting older compilers. Cannot use `std::generator`, `std::print`, or other C++23-only features.

### ADR-002: Vulkan 1.3 Without OpenGL Fallback

**Context**: Graphics API choice.
**Decision**: Vulkan 1.3 exclusively. Required features: dynamic rendering, synchronization2, BDA, descriptor indexing.
**Rationale**: GPU-driven rendering (indirect draw, compute culling) requires Vulkan. OpenGL cannot efficiently support these patterns. Vulkan 1.3 promotes critical extensions to core. All target GPUs (GTX 1660+, 2018+) support Vulkan 1.3.
**Consequences**: No macOS native support (MoltenVK possible but untested). Excludes GPUs older than ~2018.

### ADR-003: EnTT Over Flecs for ECS

**Context**: ECS framework selection.
**Decision**: EnTT (sparse-set, header-only C++17).
**Rationale**: Solo project needs full control without imposed scheduler (we use enkiTS). Header-only simplifies the build. Excellent documentation and API. Flecs would make sense for multi-developer project needing built-in scheduler and query language.
**Consequences**: System scheduling is our responsibility via enkiTS. No built-in entity explorer (use Dear ImGui custom panels).

### ADR-004: Voxel Data Outside ECS

**Context**: Whether chunks should be ECS entities with voxel data as components.
**Decision**: Chunks have dedicated `ChunkManager` with spatial storage. ECS holds only chunk metadata entities (position, state, dirty flags).
**Rationale**: Chunks are not entities — they have spatial access patterns (neighbors, slices) incompatible with ECS iteration. Meshing needs direct flat array access, not ECS indirection. Mixing the two creates coupling between unrelated systems.
**Consequences**: Two storage systems to maintain (ChunkManager + ECS registry).

### ADR-005: Binary Greedy Meshing

**Context**: Meshing algorithm selection.
**Decision**: Binary greedy meshing using 64-bit bitmask operations.
**Rationale**: ~74μs/chunk average vs ~500μs for classic greedy (30x speedup). Output format is 8 bytes/quad, directly compatible with vertex pulling. Open-source reference implementation exists (cgerikj/binary-greedy-meshing).
**Consequences**: More complex to implement and debug than classic greedy. Limited to 64 blocks per axis (sufficient for 16³ sections).

### ADR-006: enkiTS as Job System

**Context**: Threading library for async chunk pipeline.
**Decision**: enkiTS (dougbinks/enkiTS).
**Rationale**: zlib license, lightweight, C/C++11. Proven in production in Avoyd (commercial voxel engine). 5 priority levels, zero-allocation scheduling, dependency support. Taskflow is more powerful for complex DAGs but overkill for our linear pipeline.
**Consequences**: Simple integration, minimal dependency footprint.

### ADR-007: Lua/sol2/LuaJIT Over Wren

**Context**: Scripting language for modding.
**Decision**: Lua via sol2 bindings with LuaJIT runtime.
**Rationale**: Massive ecosystem, sol2 is the fastest and most ergonomic C++ Lua binding (header-only, MIT). LuaJIT approaches C performance for hot loops. Trivial sandboxing. Luanti proves the Lua model works for voxel engine modding at scale (thousands of mods). Wren is interesting but ecosystem too small, no JIT.
**Consequences**: LuaJIT locked to Lua 5.1 semantics (no 5.4 native integers). sol2 handles compatibility layer.

### ADR-008: Exceptions Disabled

**Context**: Error handling strategy.
**Decision**: Compile with `-fno-exceptions`. Use `std::expected<T,E>` + assertions + `std::abort()`.
**Rationale**: Exceptions increase binary size, degrade instruction cache, make control flow unpredictable in real-time code. Industry consensus in game development. `std::expected` with monadic chaining (`.and_then()`, `.or_else()`) offers better ergonomics than classic error codes.
**Consequences**: STL allocation failures crash instead of throwing. Custom allocators return nullptr. EnTT must be configured for no-exceptions mode.

### ADR-009: Gigabuffer Pattern for GPU Mesh Storage

**Context**: How to manage GPU memory for chunk meshes.
**Decision**: Single VkBuffer of 256–400 MB, sub-allocated via VmaVirtualBlock.
**Rationale**: Proven by Project Ascendant (vkguide.dev). Eliminates driver-level fragmentation, simplifies transfers (staging → offset), enables indirect rendering with single bind. 32-bit offsets instead of 64-bit BDA pointers.
**Consequences**: Large upfront allocation. Need defrag/compaction if internal fragmentation grows (measure before implementing).

### ADR-010: Command Pattern + Tick-Based Simulation

**Context**: Preparing for future multiplayer without implementing it.
**Decision**: All game actions are serializable `GameCommand` objects in a queue. Simulation runs at fixed 20 ticks/sec. Render interpolates between ticks.
**Rationale**: Quake/Source/Minecraft pattern. Near-zero cost in initial complexity. Makes future multiplayer incremental (send commands over network) rather than surgical (refactor entire game loop). Reference: Gabriel Gambetta, Glenn Fiedler.
**Consequences**: No direct state mutation from input. Slight command serialization overhead (negligible).
