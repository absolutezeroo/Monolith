# PRD — VoxelForge Engine

## Executive Summary

VoxelForge is a C++20/Vulkan voxel engine delivering a moddable Minecraft-like experience. V1 targets a playable solo game with infinite world, procedural terrain, block interaction, lighting, and a complete Lua scripting API. The architecture prepares for multiplayer without implementing it.

---

## User Personas

### 1. Engine Developer (primary, V1)

Solo C++ developer building and iterating on the engine itself. Needs clean architecture, fast iteration times, clear conventions, and comprehensive debug tooling. Uses BMAD methodology to stay focused.

### 2. Lua Modder (secondary, V1)

Creates new block types, items, and gameplay mechanics via Lua scripts without recompiling the engine. Needs a well-documented API, sandboxed environment, hot-reload, and example mods.

### 3. Player (tertiary, post-V1)

Plays the game with mods installed. Needs stable 60 FPS, intuitive controls, infinite explorable world, and a mod loading system that just works.

---

## Tech Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C++20, selective C++23 | Best performance + modern features; `std::expected`, `concepts`, `std::mdspan` |
| Renderer | Vulkan 1.3 | GPU-driven rendering, compute shaders, indirect draw |
| Vulkan bootstrap | vk-bootstrap + VMA + volk | Eliminates ~500 lines boilerplate, proven stack |
| ECS | EnTT | Sparse-set, header-only, no imposed scheduler, solo-friendly |
| Job system | enkiTS | zlib license, proven in Avoyd voxel engine, 5 priority levels |
| Chunk storage | Flat array + palette compression | 8 KB runtime, 512 bytes compressed, proven by Minecraft 1.13+ |
| Meshing | Binary greedy meshing | ~74μs/chunk, 8 bytes/quad, reference: cgerikj/binary-greedy-meshing |
| Noise | FastNoiseLite | Single-header, Simplex/Perlin/Cellular, domain warping |
| Scripting | Lua via sol2 + LuaJIT | Fastest C++ Lua binding, near-C perf, trivial sandboxing |
| Math | GLM | Column-major, widely used, header-only |
| Windowing | GLFW 3.4+ | Lightweight, Vulkan-native surface creation |
| Build | CMake 3.25+ + vcpkg | Manifest mode, CMakePresets, compile_commands.json |
| Tests | Catch2 v3 | Inline sections, BDD, simpler than GTest for indie |
| Logging | spdlog | Fast, fmt-based, multiple sinks |
| Debug UI | Dear ImGui | Vulkan backend, immediate mode, F3 overlay |
| Methodology | BMAD v6 + Game Dev Studio module | Structured planning → architecture → epic/story implementation |

---

## Assumptions & Dependencies

### Assumptions

- Target hardware supports Vulkan 1.3 (GPU from 2018+, drivers updated)
- Developer has intermediate C++ knowledge and basic Vulkan understanding
- World height is fixed at 256 blocks (16 sections of 16³) — expandable later
- Single-threaded game logic with multithreaded chunk pipeline is sufficient for V1
- LuaJIT's Lua 5.1 compatibility is acceptable (no Lua 5.4 integers)
- 16×16×16 chunk sections are the optimal size for meshing/culling balance

### External Dependencies

- Vulkan SDK (LunarG) installed on build machine
- SPIR-V shader compiler (`glslangValidator` or `shaderc`)
- vcpkg for all C++ library dependencies
- Node.js 20+ for BMAD method tooling

### Technical Constraints

- No exceptions (`-fno-exceptions`), no RTTI (`-fno-rtti`)
- No C++20 modules (tooling immature across compilers)
- No `std::format` in hot paths (use spdlog/fmtlib instead)
- Gigabuffer max 400 MB — fits in most GPU memory budgets

---

## Domain Data Model

### Core Entities

```
World
├── ChunkManager
│   └── ChunkColumn (keyed by ivec3{x,z})
│       └── ChunkSection[16] (16³ = 4096 blocks each)
│           └── uint16_t blocks[4096] (flat array, palette compressed on disk)
│
├── BlockRegistry
│   └── BlockDefinition (stringId, numericId, textures[6], solid, transparent, lightEmission, hardness)
│
├── ItemRegistry
│   └── ItemDefinition (stringId, numericId, stackSize, blockEquivalent)
│
├── WorldGenerator
│   ├── BiomeMap (temperature, humidity → biome type via Whittaker diagram)
│   └── StructureGenerator (trees, caves, ores — cross-chunk aware)
│
├── LightMap
│   └── LightData per block (4-bit sky + 4-bit block = 1 byte)
│
└── ECS (EnTT registry)
    ├── PlayerEntity (Position, Velocity, AABB, Camera, Inventory)
    ├── DroppedItemEntity (Position, Velocity, ItemRef, Lifetime)
    └── ChunkMetaEntity (ChunkCoord, State, DirtyFlags, LODLevel)

CommandQueue
└── GameCommand (type, playerId, tick, payload) — serializable, network-ready

Renderer
├── Gigabuffer (single VkBuffer 256-400 MB, sub-allocated via VmaVirtualBlock)
├── IndirectDrawBuffer (VkDrawIndexedIndirectCommand[], filled by compute shader)
├── TextureArray (VK_IMAGE_VIEW_TYPE_2D_ARRAY, one layer per block texture)
└── GBuffer (albedo + normal + depth → deferred lighting pass)
```

### Key Relationships

- BlockRegistry maps `string ID ↔ numeric ID`; chunks store numeric IDs
- Palette compression maps `local index ↔ global numeric ID` per section
- Meshing reads ChunkSection + 6 neighbors → produces quads in gigabuffer
- Compute culling shader reads chunk metadata → writes indirect draw commands
- Lua scripts call into BlockRegistry, ItemRegistry, World API via sol2 bindings

---

## Functional Requirements

### FR-1: Infinite Voxel World

- FR-1.1: 16×16×16 chunk sections loaded/unloaded dynamically around the player
- FR-1.2: Configurable render distance (8–32 chunks)
- FR-1.3: Chunk serialization to disk (palette compression + LZ4)
- FR-1.4: Double-precision world coordinates to avoid far-origin jittering
- FR-1.5: Spiral loading pattern from player position outward (nearest first)
- FR-1.6: Chunk unloading with dirty-check (save modified chunks before unload)

### FR-2: Procedural Terrain Generation

- FR-2.1: Multi-octave Simplex noise terrain via FastNoiseLite
- FR-2.2: Spline curve remapping for elevation distribution (plains vs mountains)
- FR-2.3: Biome system — temperature × humidity noise maps → Whittaker diagram
- FR-2.4: 3D noise for caves, overhangs, and floating islands
- FR-2.5: Per-biome surface decoration (trees, grass, flowers, cacti)
- FR-2.6: Cross-chunk structure generation (trees crossing chunk boundaries)
- FR-2.7: Deterministic generation (same seed = same world, always)
- FR-2.8: Biome blending at boundaries (weighted height interpolation)

### FR-3: Player-World Interaction

- FR-3.1: First-person camera with mouse look (pitch clamped ±89°)
- FR-3.2: WASD movement + jump + sprint + sneak (crouching prevents edge fall)
- FR-3.3: Place and break blocks via DDA 3D raycasting (max 6 blocks distance)
- FR-3.4: Block selection via hotbar (1-9 keys + scroll wheel)
- FR-3.5: AABB swept collision — axis-clipping Y→X→Z, never penetrates
- FR-3.6: Gravity (9.81 m/s², terminal velocity), ground detection
- FR-3.7: Block highlight overlay on targeted block

### FR-4: Vulkan GPU-Driven Renderer

- FR-4.1: Indirect rendering — single `vkCmdDrawIndexedIndirectCount` for all visible chunks
- FR-4.2: Binary greedy meshing (8 bytes/quad, vertex pulling from SSBO)
- FR-4.3: GPU frustum culling (compute shader testing chunk bounding spheres)
- FR-4.4: Texture arrays (`VK_IMAGE_VIEW_TYPE_2D_ARRAY`) — one layer per block texture
- FR-4.5: Baked per-vertex ambient occlusion (4 levels, quad diagonal flip for anisotropy)
- FR-4.6: Deferred rendering (G-Buffer → lighting pass)
- FR-4.7: Gigabuffer pattern (single VkBuffer, VmaVirtualBlock sub-allocation)
- FR-4.8: Shared index buffer for quads (0,1,2,2,3,0 repeated)
- FR-4.9: Per-draw data via SSBO indexed by `gl_DrawID` (chunk world pos, material)

### FR-5: Lighting System

- FR-5.1: Block light — emission 0-15, BFS flood-fill propagation with -1 attenuation
- FR-5.2: Sky light — propagation from surface downward, 15 at top, attenuated by depth
- FR-5.3: Dynamic light updates on block place/break (remove then re-propagate)
- FR-5.4: Day/night cycle — time-of-day multiplier on sky light contribution
- FR-5.5: Light data packed as 1 byte per block (4-bit sky + 4-bit block)

### FR-6: Content System (Registries)

- FR-6.1: BlockRegistry — `uint16_t` numeric IDs (65535 types), `"namespace:name"` string IDs
- FR-6.2: ItemRegistry — items bound to blocks + standalone items
- FR-6.3: Base definitions loaded from JSON, extended/overridden by Lua mods
- FR-6.4: Palette compression per chunk section (local index ↔ global ID mapping)
- FR-6.5: Block properties: solid, transparent, lightEmission, lightFilter, hardness, textures[6]
- FR-6.6: Namespaced IDs to prevent mod collisions (`"base:stone"`, `"mymod:custom_ore"`)

### FR-7: Lua Scripting

- FR-7.1: Lua integration via sol2 + LuaJIT runtime
- FR-7.2: Registration API: `voxel.register_block({...})`, `voxel.register_item({...})`
- FR-7.3: Event hooks: `on_block_placed`, `on_block_broken`, `on_player_join`, `on_tick`
- FR-7.4: World API: `get_block(x,y,z)`, `set_block(x,y,z,id)`, `raycast(origin,dir,dist)`
- FR-7.5: Sandboxing — `os`, `io`, `debug`, `loadfile` disabled; filesystem restricted to mod folder
- FR-7.6: Hot-reload — re-execute mod scripts on command without engine restart
- FR-7.7: Rate limiting — max N `set_block` calls per tick to prevent abuse
- FR-7.8: Mod loading order — base game loads first, then mods alphabetically (configurable)

### FR-8: Debug UI

- FR-8.1: Dear ImGui overlay toggled by F3
- FR-8.2: Display: FPS, player position, current chunk, facing direction, draw call count, chunk load queue size
- FR-8.3: Wireframe mode toggle, chunk boundary visualization
- FR-8.4: Chunk state visualization (loaded/meshing/ready/dirty color coding)

---

## Non-Functional Requirements

### NFR-1: Performance

- 60 FPS minimum at 16 chunks render distance on NVIDIA GTX 1660
- Meshing < 200μs per chunk (single-thread, measured via BENCHMARK)
- GPU upload < 1ms per frame (max 8 chunk uploads/frame, configurable)
- Initial world load < 5 seconds (spawn area visible and playable)
- Memory: < 2 GB RAM, < 512 MB VRAM for 16-chunk render distance

### NFR-2: Portability

- **Primary**: Windows 10+ (MSVC 2022)
- **Secondary**: Linux (GCC 13+, Clang 16+)
- Vulkan 1.3 required — no OpenGL fallback
- CMake 3.25+, vcpkg manifest mode
- CI builds on both platforms

### NFR-3: Maintainability

- Strict 3-layer architecture (Core / Engine / Game) — no reverse dependencies
- All conventions documented in `project-context.md`
- Catch2 unit tests: target >60% coverage on Core, >40% on Engine
- Doxygen-generated documentation with call graphs
- Max ~500 lines per file, one class per file

### NFR-4: Extensibility (Modding)

- Any new block/item type addable via Lua without recompilation
- Lua API documented with examples
- Mods isolated (sandboxed, no cross-mod interference)
- Mod loading discoverable (drop folder in `assets/scripts/mods/`)

### NFR-5: Network-Readiness

- Command Pattern for all game actions (serializable command objects)
- Tick-based simulation (20 ticks/sec, fixed 50ms timestep)
- Game state / render state separation (render interpolates between ticks)
- Conceptual client/server even in singleplayer (same-process, in-memory communication)
- Event system for all state changes (no direct mutation from input handlers)

---

## Epics Overview

| # | Epic | Stories | Priority | Dependencies | Description |
|---|------|---------|----------|-------------|-------------|
| 1 | Foundation | 6 | P0 | None | Core layer, CMake, vcpkg, logging, math types, Catch2 tests, CI |
| 2 | Vulkan Bootstrap | 6 | P0 | Epic 1 | GLFW window, Vulkan init (volk+vk-bootstrap+VMA), triangle, gigabuffer, staging, camera |
| 3 | Voxel World Core | 9 | P0 | Epic 1 | Structural refactoring (3.0a+3.0b), ChunkSection, ChunkColumn, BlockRegistry, Block State System, ChunkManager, palette compression, serialization |
| 4 | Terrain Generation | 5 | P0 | Epic 3 | FastNoiseLite, heightmap terrain, spline remapping, biomes, caves, tree placement |
| 5 | Meshing Pipeline | 7 | P0 | Epic 3 | Binary greedy meshing, non-cubic block models, block tinting vertex format, 8-byte quad format, face culling, AO calculation, mesh upload to gigabuffer |
| 6 | GPU-Driven Rendering | 9 | P0 | Epic 2, 5 | Descriptor infrastructure, indirect draw pipeline, compute culling, vertex pulling, texture arrays, deferred G-Buffer, transparent/translucent pass, block tinting shader |
| 7 | Player Interaction | 5 | P0 | Epic 3, 6 | FPS controls, AABB collision, DDA raycasting, block place/break, hotbar, gravity |
| 8 | Lighting | 5 | P1 | Epic 3, 6 | Mesher light integration, block light BFS, sky light, dynamic updates, day/night cycle, deferred lighting pass |
| 9 | Lua Scripting | 11 | P1 | Epic 3, 7 | sol2 integration, block registration with 85 callbacks/APIs (placement, destruction, interaction, tick/ABM/LBM, neighbor, entity, inventory, visual, 34 global events), world API, mod loading, hot-reload |
| 10 | Polish & LOD | 4 | P2 | Epic 6, 8 | Tommo's cave culling, LOD (POP buffers or clipmaps), HZB occlusion culling, SSAO |

### Epic Dependency Graph

```
Epic 1 (Foundation)
├── Epic 2 (Vulkan Bootstrap)
│   └── Epic 6 (GPU-Driven Rendering) ←── Epic 5
│       ├── Epic 8 (Lighting)
│       │   └── Epic 10 (Polish & LOD)
│       └── Epic 7 (Player Interaction) ←── Epic 3
│           └── Epic 9 (Lua Scripting) ←── Epic 3
├── Epic 3 (Voxel World Core)
│   ├── Epic 4 (Terrain Generation)
│   └── Epic 5 (Meshing Pipeline)
```

---

## V1 Done Criteria (Global Acceptance)

V1 is considered **DONE** when ALL of the following are true:

1. **Playable**: Player can walk in an infinite procedurally-generated world, place and break blocks, with gravity and collision
2. **Rendered**: Vulkan GPU-driven pipeline renders all visible chunks via indirect draw, with AO and basic lighting
3. **Lit**: Block light and sky light propagate correctly, dynamic updates work on place/break
4. **Moddable**: At least 1 Lua mod runs successfully (registers a custom block type, reacts to events)
5. **Performant**: 60 FPS at 16 chunks RD on GTX 1660, meshing < 200μs/chunk
6. **Stable**: No crashes in 30 minutes of normal gameplay, no memory leaks (ASan clean)
7. **Tested**: Core and Engine have unit tests, `ctest` passes
8. **Documented**: All BMAD artifacts complete, Doxygen generates, README exists

---

## Out of Scope (V1)

- Multiplayer networking (prepared architecturally, not implemented)
- Entity AI / mobs
- Full inventory, crafting, and survival mechanics
- Audio / music
- Main menu / save management UI
- Gamepad / controller support
- Water/fluid simulation
- Particle effects
- macOS support (MoltenVK possible but untested)

---

## Glossary

| Term | Definition |
|------|-----------|
| **Chunk Section** | 16×16×16 block volume, the atomic unit of storage and meshing |
| **Chunk Column** | Vertical stack of 16 sections (256 blocks tall) at one (x,z) position |
| **Palette Compression** | Per-section mapping of local indices → global block IDs, with variable bits-per-entry |
| **Binary Greedy Meshing** | Meshing algorithm using 64-bit bitmasks to merge coplanar faces via bitwise ops |
| **Gigabuffer** | Single large VkBuffer holding all chunk meshes, sub-allocated via VmaVirtualBlock |
| **Indirect Draw** | GPU-driven rendering where a compute shader fills draw commands; CPU issues one call |
| **Vertex Pulling** | Shader reads vertex data directly from SSBO instead of traditional VBO/VAO |
| **BFS Flood-Fill** | Breadth-first search propagation for light values across voxel grid |
| **DDA Raycasting** | Digital Differential Analyzer — exact grid traversal for voxel ray intersection |
| **AABB Swept Collision** | Collision resolution by clipping movement delta against axis-aligned bounding boxes |
| **Command Pattern** | Game actions as serializable objects routed through a queue (network-ready) |
| **Tick-Based Simulation** | Fixed timestep game loop (20 ticks/sec) with render interpolation between ticks |
| **Deferred Rendering** | Two-pass rendering: geometry → G-Buffer, then lighting applied in screen space |
| **Texture Array** | `VK_IMAGE_VIEW_TYPE_2D_ARRAY` — stack of textures sampled by layer index, no atlas bleeding |
| **Cave Culling (Tommo)** | Connectivity-graph algorithm eliminating 50-99% of underground geometry from rendering |
| **POP Buffers** | LOD technique snapping vertex positions to coarser grids with geomorphing transitions |
| **DOD** | Data-Oriented Design — SoA layouts, cache-friendly iteration, minimal indirection |
| **ADR** | Architecture Decision Record — documented rationale for a technical choice |

---

## Reference Projects & Resources

### Voxel Engines

| Project | URL | Key Learning |
|---------|-----|-------------|
| Luanti (ex-Minetest) | github.com/minetest/minetest | Lua modding API, client-server architecture |
| Project Ascendant | vkguide.dev/docs/ascendant/ | GPU-driven Vulkan voxel rendering |
| VoxelCore | github.com/MihailRis/voxelcore | C++ + EnTT + Lua structure |
| Craft | github.com/fogleman/Craft | Minimal voxel engine (~3500 lines C) |
| ClassiCube | github.com/ClassiCube/ClassiCube | Cross-platform C99 voxel engine |
| Cubiquity | github.com/DavidWilliams81/cubiquity | Sparse Voxel DAG experiments |
| Veloren | gitlab.com/veloren/veloren | Rust ECS multiplayer voxel RPG |

### Vulkan Learning

| Resource | URL | Content |
|----------|-----|---------|
| vkguide.dev | vkguide.dev | Vulkan 1.3 tutorial, GPU-driven rendering |
| vulkan-tutorial.com | vulkan-tutorial.com | Exhaustive fundamentals |
| Sascha Willems Examples | github.com/SaschaWillems/Vulkan | 80+ standalone samples |
| Exile Voxel Pipeline | thenumb.at/Voxel-Meshing-in-Exile/ | Complete pipeline with shader code |
| How I Learned Vulkan | edw.is/learning-vulkan/ | GfxDevice abstraction, vertex pulling |

### Voxel Techniques

| Topic | URL | Author |
|-------|-----|--------|
| Meshing in Minecraft | 0fps.net/2012/06/30/meshing-in-a-minecraft-game/ | Mikola Lysenko |
| Binary Greedy Meshing | github.com/cgerikj/binary-greedy-meshing | cgerikj |
| Ambient Occlusion | 0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/ | Mikola Lysenko |
| Cave Culling | tomcc.github.io/2014/08/31/visibility-1.html | Tommaso Checchi |
| POP Buffer LOD | 0fps.net/2018/03/03/a-level-of-detail-method-for-blocky-voxels/ | Mikola Lysenko |
| Palette Compression | voxel.wiki/wiki/palette-compression/ | Voxel Wiki |
| 3D DDA Raycasting | voxel.wiki/wiki/raycasting/ | Voxel Wiki |
| AABB Voxel Collision | medium.com/@andrebluntindie | Andre Blunt |

### Networking (for future phases)

| Resource | URL | Author |
|----------|-----|--------|
| Client-Server Architecture | gabrielgambetta.com/client-server-game-architecture.html | Gabriel Gambetta |
| Networking for Game Programmers | gafferongames.com | Glenn Fiedler |
