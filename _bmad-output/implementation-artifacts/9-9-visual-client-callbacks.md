# Story 9.9: Visual & Client Callbacks

Status: ready-for-dev

## Story

As a mod developer,
I want blocks to emit particles and customize their visual appearance via Lua,
so that mods can create fire particles, smoke, colored blocks, and animated blocks.

## Acceptance Criteria

1. **`on_animate_tick(pos, random)`** — Called every render frame for blocks within ~32 blocks of the player that have this callback. The `random` parameter is a callable function returning `[0, 1)`. Blocks are discovered by iterating loaded chunks near the player.
2. **`get_color(pos) -> int`** — Returns an `0xRRGGBB` integer override for block tinting. Cached per-block, invalidated when the block or a neighbor changes. Applied as a per-block tint multiplier on albedo in the fragment shader.
3. **`on_pick_block(pos) -> string`** — Called on middle-click when targeting a block. Returns the string ID of the item to give. Default (no callback): the block's own `stringId`.
4. **`voxel.add_particle({...})`** — Spawns a single particle with `pos`, `velocity`, `acceleration`, `lifetime`, `texture`, `size`, `collide` parameters. Particle rendered as a camera-facing billboard quad.
5. **`voxel.add_particle_spawner({...})`** — Spawns N particles over a duration within a region, with `amount`, `time`, `minpos/maxpos`, `minvel/maxvel`, `texture`, `size`.
6. **ParticleManager** — Manages up to 2000 active particles. Updates positions each frame (gravity via acceleration, lifetime countdown). Oldest killed when budget exceeded. Renders as forward-blended billboard quads after the deferred lighting pass.
7. **Particle rendering** — Billboard quads textured from the block `TextureArray`, alpha-blended, depth-tested (read-only) against the G-Buffer depth. New `particle.vert`/`particle.frag` shaders.
8. **categoryMask widening** — `BlockCallbacks::categoryMask()` return type widened from `uint8_t` to `uint16_t`. Visual callbacks use Bit 8 (0x100).
9. **Integration tests** — Register a fire block with `on_animate_tick` emitting particles, verify particle count increments. Register a block with `get_color`, verify override is returned. Verify `on_pick_block` returns custom item ID.

## Tasks / Subtasks

- [ ] Task 1: Extend BlockCallbacks + BlockCallbackInvoker (AC: 1, 2, 3, 8)
  - [ ] 1.1: Add 3 callback fields to `BlockCallbacks.h`: `onAnimateTick`, `getColor`, `onPickBlock` (all `std::optional<sol::protected_function>`)
  - [ ] 1.2: Widen `categoryMask()` return type from `uint8_t` to `uint16_t` — add Bit 8 (0x100) for visual category. Update all call sites.
  - [ ] 1.3: Add 3 invoke methods to `BlockCallbackInvoker.h/.cpp`:
    - `invokeOnAnimateTick(def, pos, randomValue)` — void return
    - `invokeGetColor(def, pos) -> std::optional<uint32_t>` — returns 0xRRGGBB or nullopt
    - `invokeOnPickBlock(def, pos) -> std::string` — returns item string ID, defaults to `def.stringId`

- [ ] Task 2: Extract callbacks in LuaBindings (AC: 1, 2, 3)
  - [ ] 2.1: In `parseBlockDefinition()`, add `checkAndStore("on_animate_tick", cbOnAnimateTick)`, same for `get_color` and `on_pick_block`
  - [ ] 2.2: Move stored callbacks into `BlockCallbacks` in the `hasAnyCallback` block (same pattern as all prior stories)

- [ ] Task 3: Create ParticleManager (AC: 4, 5, 6)
  - [ ] 3.1: Create `engine/include/voxel/renderer/ParticleManager.h` with:
    - `Particle` struct: pos, velocity, acceleration, lifetime, maxLifetime, textureLayer (uint16_t), size (float), collide (bool)
    - `ParticleVertex` struct: pos (vec3), uv (vec2), texLayer (float), size (float), alpha (float) — 32 bytes per vertex
    - `static constexpr uint32_t MAX_PARTICLES = 2000`
    - `void init(VmaAllocator allocator)` — create HOST_VISIBLE vertex buffer (MAX_PARTICLES * 4 * sizeof(ParticleVertex) ≈ 256 KB), persistently mapped
    - `void shutdown(VmaAllocator allocator)` — destroy buffer
    - `void addParticle(const Particle& p)` — push to pool, evict oldest if full
    - `uint32_t addParticleSpawner(...)` — V1: immediately spawns `amount` particles spread across the region, returns 0 (no persistent spawner handle)
    - `void update(float dt)` — advance positions (`pos += vel * dt`, `vel += accel * dt`), decrement lifetime, remove expired
    - `void uploadVertices(const glm::vec3& cameraRight, const glm::vec3& cameraUp)` — write billboard quads to mapped buffer
    - `[[nodiscard]] uint32_t getActiveCount() const`
    - `[[nodiscard]] VkBuffer getVertexBuffer() const`
  - [ ] 3.2: Create `engine/src/renderer/ParticleManager.cpp` — implement all methods
  - [ ] 3.3: Particle collision (V1 simple): if `collide == true`, check `ChunkManager::getBlock()` at new position — if solid, zero velocity and stop (no bounce)

- [ ] Task 4: Particle rendering pipeline (AC: 7)
  - [ ] 4.1: Create `assets/shaders/particle.vert` — billboard expansion from 4 vertices per particle (corners: (-0.5,-0.5), (0.5,-0.5), (0.5,0.5), (-0.5,0.5)). Push constants: VP matrix, cameraRight, cameraUp. Vertex input: center pos, UV, texLayer, size, alpha
  - [ ] 4.2: Create `assets/shaders/particle.frag` — sample `blockTextures` array using `vec3(uv, texLayer)`, output `vec4(color.rgb, color.a * alpha)`, discard if alpha < 0.01
  - [ ] 4.3: In `Renderer.h/.cpp`: add `m_particlePipeline`, `m_particlePipelineLayout`, create pipeline in `init()` with alpha blend (srcAlpha, oneMinusSrcAlpha), depth test read-only (depthWrite=false), dynamic rendering (same swapchain format + G-Buffer depth)
  - [ ] 4.4: Add `void Renderer::renderParticles(ParticleManager& pm, const glm::mat4& vp, const glm::vec3& camRight, const glm::vec3& camUp)` — bind pipeline, bind particle vertex buffer, bind QuadIndexBuffer, push constants, `vkCmdDrawIndexed(activeCount * 6, 1, 0, 0, 0)`
  - [ ] 4.5: Compile particle shaders to SPIR-V (add to shader compilation script or CMake custom command)

- [ ] Task 5: Register particle Lua API (AC: 4, 5)
  - [ ] 5.1: Add `static void registerParticleAPI(sol::state& lua, ParticleManager& pm, TextureArray& texArray)` to `LuaBindings.h`
  - [ ] 5.2: Implement in `LuaBindings.cpp`:
    - `voxel.add_particle({pos, velocity, acceleration, lifetime, texture, size, collide})` — parse table, resolve `texture` string to layer index via `texArray.getLayerIndex()`, call `pm.addParticle()`
    - `voxel.add_particle_spawner({amount, time, minpos, maxpos, minvel, maxvel, texture, size})` — parse table, spawn particles immediately with random positions/velocities in range

- [ ] Task 6: Wire `on_animate_tick` dispatch in GameApp render loop (AC: 1)
  - [ ] 6.1: In `GameApp::render()`, after `renderChunksIndirect()` and before `drawPostEffectTint()`:
    - Get player block position from camera
    - Iterate blocks within 32-block radius of the player (scan loaded chunks via ChunkManager)
    - For each block with `onAnimateTick` callback: invoke `invokeOnAnimateTick(def, pos, randomFloat)`
    - The `random` value passed to Lua is a simple `static_cast<float>(rand()) / RAND_MAX` (V1: not per-call random function, just a float — epic says `random` is a callable but V1 simplification: pass a random float; or create a small lambda)
  - [ ] 6.2: Performance guard: cap at N blocks per frame (e.g., 256) to prevent Lua callback spam. Skip blocks if budget exceeded. Log warning if budget hit.
  - [ ] 6.3: The `on_animate_tick` check iterates in a spiral pattern from the player outward, prioritizing nearest blocks

- [ ] Task 7: Wire `get_color` callback with caching (AC: 2)
  - [ ] 7.1: Create a color cache: `std::unordered_map<glm::ivec3, uint32_t, IVec3Hash>` in GameApp (or a small `BlockColorCache` helper)
  - [ ] 7.2: On block change (setBlock event), invalidate cache for that position and 6 neighbors
  - [ ] 7.3: When building/updating tint data for rendering, check if block has `getColor` callback → query cache (or invoke if uncached) → use returned 0xRRGGBB color
  - [ ] 7.4: V1 approach for applying the color: For blocks with a `get_color` override, the color is applied as a per-block tint. Since the current `TintPalette` only has 8 global slots, `get_color` overrides will be stored in a separate per-block color SSBO or pushed through the existing tint system by assigning dedicated tint indices. **Simplest V1**: if `get_color` is defined, store the returned color in a new `dynamicTintColor` field on `BlockDefinition` (runtime-only, not serialized), and feed it into the tint palette at the block's `tintIndex` slot. This works for V1 where few blocks use dynamic color.

- [ ] Task 8: Wire `on_pick_block` for middle-click (AC: 3)
  - [ ] 8.1: In `GameApp::handleBlockInteraction()`, add middle-click handling after existing RMB block:
    - Check `wasMouseButtonPressed(GLFW_MOUSE_BUTTON_MIDDLE)` + cursor captured + raycast hit
    - Look up block at raycast position → if has `onPickBlock` callback, invoke it → else use `def.stringId`
    - V1: log the picked item ID. Future: add to hotbar/inventory

- [ ] Task 9: Wire ParticleManager into GameApp lifecycle (AC: 6, 7)
  - [ ] 9.1: Add `std::unique_ptr<ParticleManager> m_particleManager` to `GameApp.h` — declare AFTER renderer members for correct destruction order
  - [ ] 9.2: In `GameApp::init()`: create and init ParticleManager after renderer init. Call `registerParticleAPI()` after ScriptEngine init
  - [ ] 9.3: In `GameApp::render()`: call `m_particleManager->update(frameTime)`, `m_particleManager->uploadVertices(camRight, camUp)`, `m_renderer.renderParticles(*m_particleManager, vp, camRight, camUp)`
  - [ ] 9.4: In `GameApp::shutdown()` (or destructor): `m_particleManager->shutdown()` BEFORE renderer shutdown
  - [ ] 9.5: Add particle count to ImGui debug overlay

- [ ] Task 10: Integration tests (AC: 9)
  - [ ] 10.1: Create `tests/scripting/TestVisualCallbacks.cpp` with `[scripting][visual]` tags
  - [ ] 10.2: Test: register block with `on_animate_tick`, invoke callback, verify Lua `random` parameter is a number in [0, 1)
  - [ ] 10.3: Test: register block with `get_color` returning 0x7CFC00, invoke `invokeGetColor`, verify returns 0x7CFC00
  - [ ] 10.4: Test: register block with `on_pick_block` returning "base:fire_charge", invoke `invokeOnPickBlock`, verify returns "base:fire_charge"
  - [ ] 10.5: Test: block without `on_pick_block`, verify `invokeOnPickBlock` returns `def.stringId` (default)
  - [ ] 10.6: Test: `categoryMask()` includes Bit 8 (0x100) when visual callbacks are present
  - [ ] 10.7: Test: ParticleManager `addParticle` + `update` — add particle with 1.0s lifetime, update by 0.5s, verify active count = 1; update by 0.6s, verify active count = 0 (expired)
  - [ ] 10.8: Test: ParticleManager budget — add 2001 particles, verify active count = 2000 (oldest evicted)
  - [ ] 10.9: Create test scripts: `animate_tick.lua`, `get_color.lua`, `pick_block.lua`
  - [ ] 10.10: Add `TestVisualCallbacks.cpp` to `tests/CMakeLists.txt`

- [ ] Task 11: Build integration (AC: all)
  - [ ] 11.1: Add `ParticleManager.cpp` to `engine/CMakeLists.txt`
  - [ ] 11.2: Build full project, verify zero warnings under `/W4 /WX`
  - [ ] 11.3: Run all tests (existing + new), verify zero regressions

## Dev Notes

### Architecture Compliance

- **Chunks NOT in ECS** (ADR-004): All block lookups for `on_animate_tick` radius scanning go through `ChunkManager`, never ECS queries.
- **No exceptions** (project rule): All Lua-facing invoke methods use `sol::protected_function_result` + `.valid()` + safe defaults. ParticleManager never throws.
- **Command Pattern** (ADR-010): `on_pick_block` is a query (no state mutation), so no command needed. If future "give item to inventory" is added, that MUST go through a command.
- **Game State != Render State**: `on_animate_tick` runs in the render loop and MUST NOT modify game state. It can only call `voxel.add_particle()` (visual-only side effect). Any state-modifying call from `on_animate_tick` should be blocked or deferred.
- **Threading**: `on_animate_tick` and particle update run on the main thread in the render path. No threading concerns for V1.

### Callback Invocation Pattern (Same as 9.2–9.8)

All 3 new invoke methods follow the identical pattern established in prior stories:

```cpp
// Guard
if (!def.callbacks || !def.callbacks->onAnimateTick.has_value()) return;
// Call
sol::protected_function_result result = (*def.callbacks->onAnimateTick)(posTable, randomVal);
// Error handling
if (!result.valid()) {
    sol::error err = result;
    VX_LOG_WARN("Lua on_animate_tick error for '{}': {}", def.stringId, err.what());
    return;
}
```

**Default return values:**

| Callback | Default (nil/missing) | Default (error) |
|----------|----------------------|-----------------|
| `on_animate_tick` | no-op | no-op (log warning) |
| `get_color` | `std::nullopt` (use normal tint) | `std::nullopt` |
| `on_pick_block` | `def.stringId` | `def.stringId` |

### categoryMask() Widening

`categoryMask()` currently returns `uint8_t` — all 8 bits are used (0=placement, 1=destruction, 2=interaction, 3=timer, 4=neighbor, 5=shape, 6=signal, 7=entity). Visual callbacks need Bit 8 (0x100).

**Change**: Return type → `uint16_t`. This is a low-risk change — the mask is used for quick "has any callback in category" checks, never as an array index. Search for `categoryMask()` usage and update all call sites.

### Existing C++ APIs to Reuse (DO NOT REIMPLEMENT)

| Need | Existing API | File |
|------|-------------|------|
| Block lookup | `ChunkManager::getBlock(ivec3)` | `ChunkManager.h` |
| Block definition | `BlockRegistry::getBlockType(uint16_t)` | `BlockRegistry.h` |
| Texture layer lookup | `TextureArray::getLayerIndex(string)` | `TextureArray.h` |
| Callback extraction | `checkAndStore()` lambda in `LuaBindings.cpp` | `LuaBindings.cpp` |
| Callback invocation | `posToTable(lua, pos)` | `BlockCallbackInvoker.cpp` |
| Middle-click input | `InputManager::wasMouseButtonPressed(2)` | `InputManager.h` |
| Raycast result | `m_raycastResult.hit`, `.blockPos` | `GameApp.cpp` |
| QuadIndexBuffer | `QuadIndexBuffer` (reuse for particle quads) | `QuadIndexBuffer.h` |
| VMA buffer creation | VMA patterns in `Gigabuffer.cpp`, `StagingBuffer.cpp` | |
| Tint palette | `TintPalette`, `Renderer::updateTintPalette()` | `TintPalette.h`, `Renderer.h` |
| Camera vectors | `Camera::getPosition()`, `getRight()`, `getUp()` | `Camera.h` |

### Particle Rendering Pipeline Details

**Placement in render loop**: After deferred lighting pass, before ImGui. Forward-rendered into the swapchain image with G-Buffer depth for depth testing (read-only).

**Blend mode**: `srcAlpha, oneMinusSrcAlpha` (standard alpha blend). Depth write disabled, depth test enabled (read-only).

**Billboard construction**: Each particle produces 4 vertices forming a camera-facing quad:
```
center + (-size * camRight - size * camUp) * 0.5  // bottom-left
center + ( size * camRight - size * camUp) * 0.5  // bottom-right
center + ( size * camRight + size * camUp) * 0.5  // top-right
center + (-size * camRight + size * camUp) * 0.5  // top-left
```

**Vertex format** (per-vertex, not per-quad packed like chunks):
```cpp
struct ParticleVertex {
    glm::vec3 pos;      // 12 bytes — world-space corner position
    glm::vec2 uv;       // 8 bytes — (0,0), (1,0), (1,1), (0,1)
    float texLayer;      // 4 bytes — texture array layer
    float alpha;         // 4 bytes — lifetime-based fade
    float pad;           // 4 bytes — alignment
}; // 32 bytes per vertex, 128 bytes per particle quad
```

**Buffer**: HOST_VISIBLE, persistently mapped, ~256 KB (2000 * 4 * 32). Double-buffered per frame index is optional for V1 (single buffer + host-visible coherent is sufficient at this particle count).

**Push constants for particle.vert**:
```glsl
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;  // 64 bytes
};
```

Camera right/up are pre-baked into vertex positions on CPU — no need to pass to shader.

### on_animate_tick Dispatch Strategy

In `GameApp::render()`, after `renderChunksIndirect()`:

1. Get player block position: `glm::ivec3 center = glm::ivec3(glm::floor(glm::vec3(m_camera.getPosition())))`
2. Define scan radius: 32 blocks = 2 chunks in each direction
3. Iterate chunk columns within radius (via `ChunkManager::getChunkColumn(cx, cz)`)
4. For each section in range: iterate blocks (only sections with potential `onAnimateTick` blocks)
5. **Optimization**: maintain a set of block type IDs that have `onAnimateTick` callbacks (built at registration time). Skip blocks whose type is not in the set — avoids looking up `BlockDefinition` for every block.
6. **Budget**: cap at 256 callback invocations per frame. Beyond that, skip remaining blocks.
7. Pass a random float `[0, 1)` to each callback invocation (or a callable random function — see V1 note below).

**V1 random parameter**: The epic specifies `random` as a callable function. Implement as a Lua closure that calls C++ `std::mt19937`:

```cpp
// Create once, reuse:
static std::mt19937 rng(std::random_device{}());
static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
sol::function randomFn = lua["math"]["random"]; // Or create a lightweight wrapper
```

Simplest approach: pass `lua["math"]["random"]` as the random parameter — Lua's built-in `math.random()` already returns [0,1).

### get_color Integration (V1 Approach)

The current `TintPalette` has 8 global slots. `get_color` returns a per-block position color override, which is fundamentally different from the global tint system.

**V1 Strategy**: `get_color` is invoked and cached, but the result is stored per-block-type in a runtime color map. At render time, if a block type has a `get_color` callback, its `tintIndex` in the mesh already points to a palette slot. Override that palette slot with the `get_color` result before rendering.

**Limitation**: Multiple blocks sharing a `tintIndex` will see the last `get_color` result. This is acceptable for V1 since `get_color` is mainly for custom mod blocks that define their own unique `tintIndex`.

**Cache invalidation**: Use the same dirty-tracking mechanism as `ShapeCache` (Story 9.5) — when `ChunkManager::setBlock()` fires, mark positions as dirty in the color cache. Query is lazy: invoke `getColor` only when the block is about to be rendered and the cache entry is missing or dirty.

### on_pick_block Wiring

Add to `GameApp::handleBlockInteraction()` after the existing RMB section:

```cpp
// Middle-click: pick block
if (m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_MIDDLE)
    && m_input->isCursorCaptured()
    && m_raycastResult.hit)
{
    uint16_t blockId = m_chunkManager.getBlock(m_raycastResult.blockPos);
    if (blockId != voxel::world::BLOCK_AIR)
    {
        const auto& def = m_blockRegistry.getBlockType(blockId);
        std::string itemId = m_callbackInvoker->invokeOnPickBlock(def, m_raycastResult.blockPos);
        VX_LOG_INFO("Pick block: '{}'", itemId);
        // Future: set hotbar slot to itemId
    }
}
```

No `InputManager` changes needed — `GLFW_MOUSE_BUTTON_MIDDLE` (2) is already supported.

### File Structure

**New files (5):**
- `engine/include/voxel/renderer/ParticleManager.h`
- `engine/src/renderer/ParticleManager.cpp`
- `assets/shaders/particle.vert`
- `assets/shaders/particle.frag`
- `tests/scripting/TestVisualCallbacks.cpp`

**New test scripts (3):**
- `tests/scripting/test_scripts/animate_tick.lua`
- `tests/scripting/test_scripts/get_color.lua`
- `tests/scripting/test_scripts/pick_block.lua`

**Modified files (10):**
- `engine/include/voxel/scripting/BlockCallbacks.h` — 3 visual callback fields + widen categoryMask to uint16_t + Bit 8
- `engine/include/voxel/scripting/BlockCallbackInvoker.h` — 3 invoke method declarations
- `engine/src/scripting/BlockCallbackInvoker.cpp` — 3 invoke implementations
- `engine/include/voxel/scripting/LuaBindings.h` — `registerParticleAPI` declaration
- `engine/src/scripting/LuaBindings.cpp` — 3 callback extraction + `registerParticleAPI` implementation
- `engine/include/voxel/renderer/Renderer.h` — particle pipeline, `renderParticles()` method
- `engine/src/renderer/Renderer.cpp` — particle pipeline creation, `renderParticles()` implementation
- `game/src/GameApp.h` — `m_particleManager` member
- `game/src/GameApp.cpp` — particle init/shutdown, `on_animate_tick` dispatch, `on_pick_block` handling, particle render call, debug overlay
- `engine/CMakeLists.txt` — add `ParticleManager.cpp`
- `tests/CMakeLists.txt` — add `TestVisualCallbacks.cpp`

### Naming Conventions (from project-context.md)

- Classes: `ParticleManager`, `Particle`, `ParticleVertex` (PascalCase)
- Methods: `addParticle`, `uploadVertices`, `renderParticles`, `invokeOnAnimateTick`, `invokeGetColor`, `invokeOnPickBlock` (camelCase)
- Members: `m_particles`, `m_vertexBuffer`, `m_particlePipeline`, `m_particleManager` (m_ prefix)
- Constants: `MAX_PARTICLES`, `ANIMATE_TICK_RADIUS`, `ANIMATE_TICK_BUDGET` (SCREAMING_SNAKE)
- Namespace: `voxel::renderer` for ParticleManager, `voxel::scripting` for callback additions
- Files: `ParticleManager.h` / `ParticleManager.cpp` (PascalCase)

### What NOT To Do

- **DO NOT add sol2 headers to ParticleManager, Renderer, or Camera** — keep sol2 isolated to scripting/
- **DO NOT implement persistent particle spawners** — V1 `add_particle_spawner` immediately spawns all particles, no spawner handle/object
- **DO NOT implement particle physics with full collision resolution** — V1: simple check "is block at new position solid? stop." No bouncing, no sliding.
- **DO NOT implement `on_tesselate`** (custom mesh generation from Lua) — deferred to post-V1 per epic spec
- **DO NOT modify ScriptEngine** — all new bindings go through LuaBindings and new API registration methods
- **DO NOT implement formspec/UI** — not in scope
- **DO NOT implement a full color-per-block SSBO** — V1 uses the existing 8-entry TintPalette with `get_color` overriding the block's tint slot
- **DO NOT create ECS components for particles** — particles are managed by `ParticleManager`, not the ECS
- **DO NOT use `std::shared_ptr` for ParticleManager** — owned by GameApp via `unique_ptr`
- **DO NOT store Vulkan objects (VkBuffer etc) in a class without RAII shutdown** — ParticleManager::shutdown() must be called before VMA/device destruction
- **DO NOT call state-modifying Lua APIs from `on_animate_tick`** — it runs in the render path. V1: not enforced, but documented. Future: sandbox `on_animate_tick` environment to only allow `add_particle`

### Dependencies

- **Story 9.2** (done): Block registration, callback extraction pattern, `checkAndStore`, `BlockCallbacksPtr`
- **Story 9.3** (done): Interaction callbacks in GameApp pattern
- **Story 9.5** (done): `ShapeCache` dirty-tracking pattern (reuse for `get_color` cache invalidation)
- **Story 9.6** (review): Entity callbacks, categoryMask bits 0-7 fully used
- **Story 5.5** (done): Block tinting + `TintPalette`, `tintIndex` in vertex format
- **Story 6.5** (done): `TextureArray` loading, `getLayerIndex()`
- **Story 6.6** (done): G-Buffer setup, depth attachment for particle depth testing
- **Story 6.7** (done): Translucent rendering pass — particle pass follows similar blending approach
- **Epic 2** (done): Vulkan pipeline creation patterns, QuadIndexBuffer

### Previous Story Intelligence

**From 9.8 (World Query & Modification API):**
- `registerWorldAPI(lua, chunkManager, blockRegistry)` static pattern — same for `registerParticleAPI(lua, pm, texArray)`
- Rate limiting via `RateLimiter` struct — `add_particle` could be rate-limited (optional for V1, as the 2000 particle budget is self-limiting)
- ChunkManager iteration for area queries — reuse approach for `on_animate_tick` radius scan

**From 9.6 (Entity Callbacks):**
- Callback invocation pattern: `has_value()` → `protected_function_result` → `.valid()` → log + safe default
- `posToTable(lua, pos)` utility in `BlockCallbackInvoker` for converting `ivec3` → Lua table
- `categoryMask` bit 7 (0x80) = entity — next is bit 8 (0x100), requiring `uint16_t`
- EntityHandle usertype registration pattern — similar for any new usertypes if needed

**From 9.5 (Neighbor/Shape):**
- `ShapeCache`: caches dynamic values per-block-position, invalidated on block/neighbor change — same pattern needed for `get_color` cache
- `NeighborNotifier`: fires after `setBlock` — hook color cache invalidation into this

**From 5.5 (Block Tinting):**
- `TintPalette` has 8 slots (0=none, 1=grass, 2=foliage, 3=water, 4-7=available)
- `tintIndex` packed into quad bits 59-61 (3 bits, max 8 values)
- `TintPaletteSSBO` at binding 5 in `gbuffer.frag` / `chunk.frag`
- `Renderer::updateTintPalette()` writes to persistently-mapped buffer

**From 6.7 (Translucent Rendering):**
- Forward-blended pass after deferred lighting — particle pass follows identical pattern
- Alpha blending state: `srcAlpha, oneMinusSrcAlpha`, depth write off, depth test on
- Dynamic rendering with swapchain color + G-Buffer depth attachments

### Git Intelligence

Recent commits follow `feat(scripting): <description>` pattern. This story adds rendering infrastructure, so commit could be:
- `feat(scripting): add visual callbacks and particle system for Lua blocks`

### Testing Strategy

**Integration tests** (Catch2 v3, `[scripting][visual]` tags):
1. `on_animate_tick` callback invocation — register block, invoke, verify Lua receives pos and random
2. `get_color` returns correct value — register with `get_color` returning 0x7CFC00, verify invoker returns it
3. `on_pick_block` returns custom item — register with callback, verify custom string returned
4. `on_pick_block` default — register without callback, verify `def.stringId` returned
5. `categoryMask` bit 8 — register block with only visual callbacks, verify mask is 0x100
6. `categoryMask` combined — register with placement + visual, verify mask has both bits
7. ParticleManager lifetime — add particle, update past lifetime, verify removed
8. ParticleManager budget — add >MAX_PARTICLES, verify capped at MAX_PARTICLES

**Test Lua scripts** in `tests/scripting/test_scripts/`:

`animate_tick.lua`:
```lua
voxel.register_block({
    id = "test:fire",
    on_animate_tick = function(pos, random)
        test_animate_tick_called = true
        test_animate_tick_random = random
        test_animate_tick_pos_x = pos.x
    end,
})
```

`get_color.lua`:
```lua
voxel.register_block({
    id = "test:colored_block",
    get_color = function(pos)
        return 0x7CFC00 -- Lawn green
    end,
})
```

`pick_block.lua`:
```lua
voxel.register_block({
    id = "test:special_block",
    on_pick_block = function(pos)
        return "test:fire_charge"
    end,
})
```

### Particle Shader Reference

**particle.vert:**
```glsl
#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) in vec3 inPos;       // pre-computed billboard corner position
layout(location = 1) in vec2 inUV;
layout(location = 2) in float inTexLayer;
layout(location = 3) in float inAlpha;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out flat float fragTexLayer;
layout(location = 2) out float fragAlpha;

void main() {
    gl_Position = pc.viewProjection * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTexLayer = inTexLayer;
    fragAlpha = inAlpha;
}
```

**particle.frag:**
```glsl
#version 450

layout(set = 0, binding = 2) uniform sampler2DArray blockTextures;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in flat float fragTexLayer;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(blockTextures, vec3(fragUV, fragTexLayer));
    if (texColor.a * fragAlpha < 0.01) discard;
    outColor = vec4(texColor.rgb, texColor.a * fragAlpha);
}
```

### Destruction Order

ParticleManager holds VMA-allocated buffers. Destruction order in GameApp:
1. `m_particleManager->shutdown(m_vulkanContext.getAllocator())` — free VkBuffer
2. Renderer shutdown (frees pipeline, pipeline layout)
3. VulkanContext shutdown (frees VMA allocator, device)

Declare `m_particleManager` in `GameApp.h` such that it is destroyed before the renderer. Or call `shutdown()` explicitly in the GameApp destructor/shutdown method.

### Project Structure Notes

All new code follows existing structure:
- Public headers: `engine/include/voxel/renderer/` for ParticleManager
- Implementation: `engine/src/renderer/` for ParticleManager.cpp
- Scripting additions: `engine/include/voxel/scripting/` and `engine/src/scripting/`
- Shaders: `assets/shaders/`
- Tests: `tests/scripting/`
- Test scripts: `tests/scripting/test_scripts/`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.9]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 5: Vulkan Renderer, System 9: Scripting, ADR-004, ADR-007, ADR-010]
- [Source: _bmad-output/project-context.md — Critical Implementation Rules, Naming, Testing]
- [Source: engine/include/voxel/scripting/BlockCallbacks.h — Current 8 category bits]
- [Source: engine/include/voxel/scripting/BlockCallbackInvoker.h — Invocation pattern]
- [Source: engine/src/scripting/LuaBindings.cpp — checkAndStore pattern, parseBlockDefinition]
- [Source: engine/include/voxel/renderer/Renderer.h — Pipeline, renderChunksIndirect, tintPalette]
- [Source: engine/include/voxel/renderer/TextureArray.h — getLayerIndex for particle textures]
- [Source: engine/include/voxel/renderer/TintPalette.h — 8-entry tint palette system]
- [Source: engine/include/voxel/renderer/QuadIndexBuffer.h — Reuse for particle quads]
- [Source: engine/include/voxel/renderer/Camera.h — getPosition, getRight, getUp]
- [Source: engine/include/voxel/input/InputManager.h — wasMouseButtonPressed(MIDDLE)]
- [Source: game/src/GameApp.cpp — render() loop, handleBlockInteraction()]
- [Source: _bmad-output/implementation-artifacts/9-8-world-query-modification-api.md — registerXxxAPI pattern]
- [Source: _bmad-output/implementation-artifacts/9-6-entity-block-interaction-callbacks.md — categoryMask, entity invoke pattern]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
