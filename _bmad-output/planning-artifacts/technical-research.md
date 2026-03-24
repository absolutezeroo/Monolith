# Technical Research — VoxelForge Engine

> Phase 1 analysis artifact. Deep technical findings for agent reference during implementation.
> Covers: architecture patterns, rendering techniques, algorithms, benchmarks, code examples.

---

## 1. Data-Oriented Design for Voxel Engines

### Struct of Arrays (SoA) vs Array of Structs (AoS)

SoA maximizes cache locality when a system only reads one attribute at a time. In a voxel engine, the mesher only needs block IDs, not light or metadata — SoA lets it stream through memory without cache pollution.

**Key insight from John Lin (voxely.net):** no single voxel format suits all subsystems. The recommended approach is a canonical "raw" format (flat array) with conversions to specialized formats per system: palette for serialization, octree for long-range raycasting if needed, bitmask for meshing.

### Three-Layer Separation

Industry consensus from multiple production voxel engines:

- **Core**: must compile standalone with zero external dependencies. This enables unit testing without GPU, OS, or framework. Contains: math types, allocators (pool, arena, stack), containers, platform abstraction, job system interface.
- **Engine**: depends only on Core. Contains all subsystems: renderer, chunk manager, physics, script host, audio, network interface. Each system communicates through well-defined interfaces (events, command queues, shared ECS components).
- **Game**: ideally entirely script-driven. Luanti proves this works at scale — thousands of mods, gameplay entirely in Lua.

The critical boundary: Engine exposes an API, Game consumes it. Game never reaches into Engine internals.

---

## 2. ECS Comparison: EnTT vs Flecs

### EnTT (Recommended)

- **Storage model**: Sparse-set — each component type has its own packed array + sparse index array
- **Iteration**: Ultra-fast for single-component views; slightly slower for multi-component joins than archetype-based
- **Threading**: No built-in scheduler — user manages parallelism (we use enkiTS)
- **API**: Header-only C++17, ~30k GitHub stars, MIT license
- **DLL support**: Works across DLL boundaries (important for future plugin system)
- **Memory**: Lower overhead per entity than archetype systems for small registries

### Flecs

- **Storage model**: Archetype-based — entities with same component set share a table
- **Iteration**: Faster for multi-component queries (data is colocated in archetype tables)
- **Threading**: Built-in multithreaded scheduler with automatic dependency resolution
- **API**: C/C++, query language DSL, entity explorer web UI
- **Production use**: Project Ascendant (vkguide.dev) uses Flecs

### Decision rationale

For a solo developer: EnTT's simplicity wins. No scheduler to configure, no archetype fragmentation to manage. The job system (enkiTS) handles threading explicitly. Flecs would be the choice if multiple developers needed the scheduler's automatic parallelism.

### Critical rule: chunks outside ECS

Both EnTT and Flecs are designed for entity iteration — iterating all entities with component X. Voxel chunks need spatial queries: "give me the chunk at (3, 7)" or "give me the 6 neighbors of this section." These are lookup patterns, not iteration patterns. Storing chunk data in ECS creates unnecessary indirection and prevents the flat-array access patterns that meshing requires.

Use ECS for: players, mobs, items, projectiles, chunk metadata (position, state, dirty flags).
Use ChunkManager for: voxel block data, light maps, mesh data references.

---

## 3. Chunk Storage Deep Dive

### Flat Array Performance

Nick McDonald (nickmcd.me) benchmarked flat arrays vs octrees for meshing:
- Flat array: fastest meshing (sequential memory access, no pointer chasing)
- Octree: slower meshing despite less memory (tree traversal overhead, cache misses)
- Conclusion: use flat arrays for runtime, compress only for serialization and distant storage

### Memory Budget

At 16 chunks render distance (33×33 columns × 16 sections):
- Uncompressed: 33² × 16 × 8KB = ~140 MB (worst case, all sections allocated)
- With null sections (empty sky): ~30-50 MB typical (most sections above ground are null)
- With palette compression on distant chunks: ~10-20 MB

### Palette Compression Details (Minecraft 1.13+ approach)

```
Section data = palette[] + bitBuffer + bitsPerEntry

palette: array of global block IDs used in this section
bitBuffer: packed array of local palette indices
bitsPerEntry: 1, 2, 4, 8, or 16 (auto-grows)

To read block at index i:
  localIndex = bitBuffer.get(i, bitsPerEntry)
  globalId = palette[localIndex]

To write block:
  if blockId not in palette:
    palette.add(blockId)
    if palette.size > 2^bitsPerEntry:
      grow bitsPerEntry to next power of 2
      rebuild bitBuffer with new width
  localIndex = palette.indexOf(blockId)
  bitBuffer.set(i, localIndex, bitsPerEntry)
```

Compression ratios measured by zeux.io (Roblox engineer):
- RLE + LZ4 on top of palette: 2x–30x depending on chunk content
- Homogeneous chunks (air, stone): essentially free
- Complex surface chunks: ~2-4x compression

### Hybrid Row Optimization (zeux.io)

For sections with many uniform rows (common in underground stone):
- Each row (16 blocks) stored as either: a full 16×uint16 array, or a single uint16 (uniform row)
- Reduces memory for deep stone/air sections without the complexity of full palette compression
- Can coexist with palette compression (use hybrid for RAM, palette for disk)

---

## 4. Binary Greedy Meshing Algorithm

### How It Works

For each of the 3 axes and 2 directions (6 face orientations total):

```
For each slice perpendicular to the axis (64 slices for a 64-block span):
  1. Build a 64-bit mask: bit[i] = 1 if block at position i is solid
  2. Build neighbor mask: bit[i] = 1 if the adjacent block (in face direction) is solid
  3. Face mask = solid_mask AND NOT neighbor_mask
     (faces exist where we're solid but neighbor isn't)
  4. Group by block type — each type gets its own face mask
  5. Greedy merge rows within each type's mask:
     - For each set bit, find the run length (count trailing ones)
     - Check if subsequent rows have the same run at the same position
     - Merge vertically as long as rows match
     - Emit one quad for the merged rectangle
     - Clear processed bits
```

The key innovation: step 5 operates on 64 bits simultaneously via bitwise ops (`__builtin_ctzll`, `__builtin_clzll`, AND, XOR, NOT), processing 64 potential faces in a single CPU instruction.

### Performance Data

From cgerikj/binary-greedy-meshing benchmarks (Ryzen 3800x, 16³ sections):
- **Average**: 74μs per chunk
- **Range**: 50–200μs depending on surface complexity
- **Comparison**: Classic greedy meshing ~300-500μs, naive culling ~500μs
- **V2 improvements**: "several times faster" than V1 (contributions by Ethan Gore, Finding Fortune)
- **Rust port**: ~30x faster than block-mesh-rs reference

### Output Format: 8 Bytes Per Quad

```
uint64_t packed_quad:
  [5:0]   x position (0–63)
  [11:6]  y position (0–63)
  [17:12] z position (0–63)
  [23:18] width - 1 (0–63, merged quad width)
  [29:24] height - 1 (0–63, merged quad height)
  [37:30] block type index (0–255)
  [40:38] face direction (0–5: +X,-X,+Y,-Y,+Z,-Z)
  [48:41] AO values (4 × 2 bits, one per quad corner)
  [63:49] reserved (lighting, flags)
```

This format is consumed directly by the vertex shader via vertex pulling — no unpacking to traditional vertex buffers needed.

---

## 5. Vulkan GPU-Driven Rendering

### The Gigabuffer Pattern (Project Ascendant)

Victor Blanco's architecture for Project Ascendant (documented at vkguide.dev):

- **Single VkBuffer**: 400 MB allocated at startup
- **Sub-allocation**: VmaVirtualBlock tracks free/used regions CPU-side (no actual Vulkan sub-allocation)
- **Why one buffer**: eliminates per-chunk buffer creation/destruction, allows single bind for all draws, 32-bit offsets instead of 64-bit BDA pointers, simplifies transfer commands
- **Upload**: staging buffer → vkCmdCopyBuffer to gigabuffer at offset
- **Defrag**: not needed initially — VmaVirtualBlock handles fragmentation; compact if metrics show >30% waste

### Indirect Rendering Pipeline Detail

```
CPU side (per frame):
  1. Upload ChunkRenderInfo[] to SSBO (positions, offsets, quad counts, bounding spheres)
  2. Update frustum planes uniform

GPU side:
  3. Reset drawCount to 0 (vkCmdFillBuffer)
  4. Dispatch cull.comp:
     - 64 threads per workgroup
     - Each thread tests one chunk
     - Frustum test: sphere vs 6 planes
     - If visible: atomicAdd(drawCount) → write VkDrawIndexedIndirectCommand
  5. Pipeline barrier: COMPUTE_WRITE → INDIRECT_READ
  6. vkCmdDrawIndexedIndirectCount:
     - indirectBuffer = filled by compute
     - countBuffer = atomic counter from compute
     - maxDrawCount = total chunk count (safety cap)
     - GPU reads count from buffer — CPU never knows how many draws
  7. Vertex shader: vertex pulling from gigabuffer
  8. Fragment shader: texture array sampling
```

### Vertex Pulling

Instead of traditional VBO with vertex attributes:
- Gigabuffer bound as SSBO (storage buffer)
- Vertex shader receives NO vertex input attributes
- Reads data from SSBO using `gl_VertexID`:
  - `quadIndex = gl_VertexID / 4`
  - `cornerIndex = gl_VertexID % 4`
  - Unpack the 8-byte quad at `gigabuffer[chunkOffset + quadIndex]`
  - Reconstruct corner position from face direction + width/height + corner index
- Per-draw data (chunk world position) from another SSBO indexed by `gl_DrawID`

Benefits: no VAO/VBO setup per chunk, arbitrary vertex format (8 bytes vs typical 32+ bytes), single buffer bind.

### Vulkan Init Stack Details

**volk** — loads all Vulkan function pointers at runtime:
```cpp
volkInitialize(); // Before any Vulkan call
// After instance creation:
volkLoadInstance(instance);
// After device creation:
volkLoadDevice(device);
```
Eliminates link-time dependency on vulkan-1.dll. Integrates with VMA via `vmaImportVulkanFunctionsFromVolk()`.

**vk-bootstrap** — builder pattern for Instance/Device/Swapchain:
```cpp
auto inst = vkb::InstanceBuilder{}
    .set_app_name("VoxelForge")
    .require_api_version(1, 3, 0)
    .request_validation_layers(true)
    .build().value();
```
Eliminates ~500 lines of boilerplate. Handles physical device selection with feature requirements.

**VMA** — GPU memory management:
- `VMA_MEMORY_USAGE_AUTO`: VMA picks optimal memory type
- Staging buffers: `HOST_ACCESS_SEQUENTIAL_WRITE_BIT | CREATE_MAPPED_BIT`
- ReBAR support: automatic fallback if not available
- VmaVirtualBlock: CPU-side bookkeeping for gigabuffer sub-allocation

---

## 6. Ambient Occlusion Implementation

### Per-Vertex AO (0fps.net technique)

For each vertex of a face, sample 3 adjacent blocks relative to the face orientation:
- `side1`: block adjacent along one face-tangent axis
- `side2`: block adjacent along the other face-tangent axis
- `corner`: block at the diagonal (adjacent to both sides)

```cpp
int vertexAO(bool side1, bool corner, bool side2) {
    if (side1 && side2) return 0;  // Both sides occluded → max darkness
    return 3 - (int)side1 - (int)side2 - (int)corner;
}
// Values: 0 (darkest) to 3 (brightest)
// Shader LUT: float ao[] = { 0.2, 0.5, 0.8, 1.0 };
```

### Quad Diagonal Flip

Standard quad triangulation: triangle 1 = (0,1,2), triangle 2 = (2,3,0).
GPU interpolates AO values barycentrically PER TRIANGLE, not bilinearly across the quad.

When diagonal AO values differ significantly, the triangulation diagonal causes visible artifacts:
```cpp
// If opposite corners have very different AO, flip the diagonal
bool shouldFlip = abs(ao[0] - ao[3]) > abs(ao[1] - ao[2]);
// If flipped: triangle 1 = (1,2,3), triangle 2 = (3,0,1)
```

This must be encoded in the quad data and respected by the vertex shader's corner reconstruction.

---

## 7. Culling Techniques

### Tommo's Cave Culling (Minecraft)

Two-phase algorithm by Tommaso Checchi (Bedrock Edition):

**Phase 1 — Connectivity graph** (precomputed during meshing):
- For each 16³ section, flood-fill through non-opaque blocks
- Record which section faces can "see" which other faces
- 6 faces → C(6,2) = 15 possible face pairs
- Stored as a 16-bit bitmask per section (bit i = pair i is connected)
- Example: if a section is entirely solid, bitmask = 0 (no face sees any other face)
- Example: if a section is entirely empty, bitmask = 0x7FFF (all 15 pairs connected)

**Phase 2 — Visibility BFS** (per frame):
- Start BFS from the section containing the camera
- For each neighbor section: check if the connectivity graph allows traversal from the entry face to any exit face
- Only expand in the hemisphere facing away from camera (dot(faceNormal, viewDir) < 0)
- Provides in one pass: visibility set, depth ordering, rebuild priority, frustum culling

Measured results: eliminates 50–99% of geometry in cave-heavy areas.

### Hierarchical Z-Buffer (future)

Used by UE5/Nanite for general-purpose occlusion:
1. Render known-visible objects → Z-buffer
2. Build HZB: compute shader generates mip chain (each texel = max depth of 4 parent texels)
3. Test candidates: project bounding box to screen, sample HZB at appropriate mip level
4. If max HZB depth < min projected depth → occluded, skip

For voxels: complement cave culling with HZB for above-ground occlusion (mountains blocking terrain behind).

---

## 8. LOD Techniques

### POP Buffers (0fps.net, Mikola Lysenko)

**Concept**: at each LOD level, snap vertex positions to a coarser grid (powers of 2). Merged faces at one LOD "pop" apart at the next level.

**Implementation**:
```glsl
// In vertex shader:
float lodScale = pow(2.0, float(lodLevel));
vec3 snappedPos = floor(pos / lodScale) * lodScale;
float morphFactor = smoothstep(lodFadeStart, lodFadeEnd, distToCamera);
vec3 finalPos = mix(pos, snappedPos, morphFactor);
```

**Properties**:
- No seams between LOD levels (positions converge at boundaries)
- No separate mesh generation per LOD (same mesh, different snap)
- Geomorphing eliminates popping artifacts
- Memory cost: only need to store full-resolution mesh + LOD level per chunk

### Clipmaps (Voxel Farm / Procedural World)

Concentric rings around the viewer:
- Ring 0: 1 voxel = 1 block (full resolution)
- Ring 1: 1 voxel = 2×2×2 blocks
- Ring 2: 1 voxel = 4×4×4 blocks
- Each ring is a separate mesh, regenerated as player moves

Works in 3D (not just heightmap). More complex than POP buffers but handles arbitrary voxel shapes at distance.

### Ascendant Approach: Far-Draw Sprites

For extreme distance: don't mesh at all. Compress each visible block face to 4 bytes:
```
drawflags:4 | position:12 | blockType:16
```
Pixel shader performs ray/box intersection per-block. No geometry generation, trivial memory cost. Transitions: SurfaceNets smoothed mesh for mid-range, sprites for far-field.

---

## 9. Physics Algorithms

### AABB Swept Collision (Minecraft-style)

The key principle: NEVER allow intersection. Clip the movement vector before applying it.

```
function resolveMovement(player, velocity, world):
  // Resolve each axis independently, order matters
  for axis in [Y, X, Z]:  // Y first for gravity
    expandedAABB = player.aabb.expand(velocity[axis])
    candidates = world.getSolidBlocks(expandedAABB)
    for block in candidates sorted by distance:
      velocity[axis] = clipAxis(player.aabb, block.aabb, velocity[axis], axis)
    player.position[axis] += velocity[axis]
    
  return velocity  // May be modified (clipped)
```

Critical details:
- Y first so gravity resolution happens before horizontal (prevents sliding along walls when falling)
- Sort candidates by distance to avoid tunneling through thin walls
- Step-up: if horizontal collision at foot level and air above → auto-step up 1 block

### DDA 3D Raycasting (Amanatides & Woo)

```
function raycast(origin, direction, maxDist):
  // Initialize
  currentBlock = floor(origin)
  step = sign(direction)  // +1 or -1 per axis
  tDelta = abs(1.0 / direction)  // Distance to cross one cell per axis
  
  // Distance from origin to first cell boundary per axis
  tMax.x = (step.x > 0 ? ceil(origin.x) - origin.x : origin.x - floor(origin.x)) / abs(direction.x)
  tMax.y = ...
  tMax.z = ...
  
  while distance < maxDist:
    // Advance along axis with smallest tMax
    if tMax.x < tMax.y and tMax.x < tMax.z:
      currentBlock.x += step.x
      tMax.x += tDelta.x
      face = step.x > 0 ? WEST : EAST
    elif tMax.y < tMax.z:
      // ... Y axis
    else:
      // ... Z axis
      
    block = world.getBlock(currentBlock)
    if block.isSolid():
      return Hit(currentBlock, previousBlock, face, distance)
    previousBlock = currentBlock
    
  return Miss
```

Properties: exact traversal of all voxels the ray passes through, O(distance) complexity, handles edge cases (ray parallel to axis, exact corner hits).

---

## 10. Lighting Algorithm Details

### BFS Flood-Fill Propagation

```
function propagateBlockLight(world, sourcePos, lightLevel):
  queue = [(sourcePos, lightLevel)]
  while queue not empty:
    (pos, level) = queue.dequeue()
    if level <= world.getBlockLight(pos):
      continue  // Already brighter
    world.setBlockLight(pos, level)
    if level <= 1:
      continue  // No more propagation
    for each neighbor in 6 directions:
      neighborBlock = world.getBlock(neighbor)
      if neighborBlock.isOpaque():
        continue
      attenuation = max(1, neighborBlock.lightFilter)
      newLevel = level - attenuation
      if newLevel > world.getBlockLight(neighbor):
        queue.enqueue((neighbor, newLevel))
```

### Light Removal (on block place)

When a block is placed that blocks light:
```
function removeLight(world, pos):
  oldLevel = world.getBlockLight(pos)
  world.setBlockLight(pos, 0)
  removalQueue = [(pos, oldLevel)]
  repropagate = []
  
  while removalQueue not empty:
    (p, level) = removalQueue.dequeue()
    for each neighbor:
      neighborLevel = world.getBlockLight(neighbor)
      if neighborLevel > 0 and neighborLevel < level:
        world.setBlockLight(neighbor, 0)
        removalQueue.enqueue((neighbor, neighborLevel))
      elif neighborLevel >= level:
        repropagate.add(neighbor)  // Lit by another source
  
  // Re-propagate from remaining sources
  for source in repropagate:
    propagateBlockLight(world, source, world.getBlockLight(source))
```

This two-phase approach (remove then re-propagate) correctly handles multiple overlapping light sources.

---

## 11. Scripting Integration Patterns

### sol2 Binding Examples

```cpp
// Expose a C++ class to Lua
lua.new_usertype<glm::ivec3>("Vec3i",
    sol::constructors<glm::ivec3(int,int,int)>(),
    "x", &glm::ivec3::x,
    "y", &glm::ivec3::y,
    "z", &glm::ivec3::z
);

// Expose engine API
lua["voxel"] = lua.create_table();
lua["voxel"]["register_block"] = [&registry](sol::table def) -> sol::optional<uint16_t> {
    auto id = def.get<std::string>("id");
    if (!id || id->empty()) return sol::nullopt;
    BlockDefinition block;
    block.stringId = *id;
    block.isSolid = def.get_or("solid", true);
    block.hardness = def.get_or("hardness", 1.0f);
    block.lightEmission = def.get_or("light_emission", 0);
    // ... more properties
    return registry.registerBlock(std::move(block));
};
```

### Sandboxing

```cpp
sol::state lua;
lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
// Remove dangerous functions
lua["os"] = sol::nil;
lua["io"] = sol::nil;
lua["debug"] = sol::nil;
lua["loadfile"] = sol::nil;
lua["dofile"] = sol::nil;
lua["load"] = sol::nil;
lua["rawset"] = sol::nil;  // Prevent metatable abuse
lua["rawget"] = sol::nil;
```

### Hot-Reload Pattern

```cpp
void ScriptEngine::hotReload() {
    // 1. Save current world state (blocks already in chunks, not affected)
    // 2. Clear all mod-registered content
    m_blockRegistry.clearModBlocks();  // Keep base blocks
    m_itemRegistry.clearModItems();
    m_eventCallbacks.clear();
    
    // 3. Destroy and recreate Lua state
    m_lua = sol::state{};
    initSandbox();
    bindEngineAPI();
    
    // 4. Re-execute all mods
    loadBaseMod();
    loadUserMods();
    
    // 5. Rebuild texture array if new blocks added textures
    m_renderer.rebuildTextureArray();
    
    VX_LOG_INFO("Hot-reload complete: {} blocks, {} items registered",
                m_blockRegistry.blockCount(), m_itemRegistry.itemCount());
}
```

---

## 12. Network-Ready Architecture Patterns

### Gabriel Gambetta's Client-Server Model

Four-part series (gabrielgambetta.com) covers:
1. **Client-server architecture**: authoritative server, dumb clients
2. **Client-side prediction**: client applies inputs immediately, reconciles with server
3. **Server reconciliation**: server sends authoritative state, client replays unacknowledged inputs
4. **Entity interpolation**: other players rendered between two known server states

For VoxelForge V1: implement the command queue and tick-based simulation. The networking layer plugs in later by sending commands over the wire instead of in-memory queue.

### Glenn Fiedler's Networking Model

Key articles (gafferongames.com):
- **Fix Your Timestep**: fixed dt simulation with render interpolation (exactly our game loop)
- **State Synchronization**: snapshot compression, delta encoding
- **Reliable UDP**: custom protocol for game state over UDP

### Quake-Style Internal Server

Even in singleplayer, the game conceptually runs as client + server in the same process:
```
Input → Client (prediction) → CommandQueue → Server (authoritative) → StateUpdate → Client (reconcile) → Render
```
In singleplayer: CommandQueue is an in-memory queue, StateUpdate is a direct callback.
In multiplayer: CommandQueue serializes over network, StateUpdate is a network packet.

Luanti uses exactly this pattern — validates that it works for Minecraft-like games.

---

## 13. Build System Best Practices

### CMake Modern Targets

```cmake
# Engine library — PUBLIC dependencies propagate to consumers
add_library(VoxelEngine STATIC)
target_include_directories(VoxelEngine
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VoxelEngine
    PUBLIC  glm::glm spdlog::spdlog EnTT::EnTT
    PRIVATE glfw Vulkan::Vulkan volk::volk GPUOpen::VulkanMemoryAllocator)
```

### Precompiled Headers Impact

Measured compilation time reduction with PCH (typical game engine codebase):
- STL headers (`<vector>`, `<string>`, `<unordered_map>`): 30-40% faster incremental builds
- GLM: significant (heavy template library)
- spdlog/fmt: moderate (fmt header parsing is slow)
- EnTT: moderate (heavy template library)

### vcpkg Manifest Mode

`vcpkg.json` at project root, `CMAKE_TOOLCHAIN_FILE` set to vcpkg toolchain in CMakePresets.json:
```json
{
  "version": 3,
  "configurePresets": [{
    "name": "default",
    "cacheVariables": {
      "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    }
  }]
}
```

Advantages over Conan for a solo project: simpler CMake integration, better Windows support, manifest mode just works.
