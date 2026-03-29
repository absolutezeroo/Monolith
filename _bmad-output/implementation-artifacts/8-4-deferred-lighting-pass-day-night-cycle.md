# Story 8.4: Deferred Lighting Pass + Day/Night Cycle

Status: review

## Story

As a developer,
I want the deferred lighting pass to use per-vertex light values and a day/night cycle,
so that the rendered world has proper voxel illumination that changes over time.

## Acceptance Criteria

1. **G-Buffer light storage**: A third G-Buffer render target (RT2) stores per-vertex `skyLight` and `blockLight` values written by `gbuffer.frag`, so the deferred lighting pass can read them.
2. **Block light contribution**: `lighting.frag` reads blockLight from RT2, maps it to a warm orange tint (`vec3(1.0, 0.85, 0.7) * blockLight`).
3. **Sky light contribution**: `lighting.frag` reads skyLight from RT2, multiplies by `dayNightFactor`, applies cool white/blue tint.
4. **Combined lighting**: Final light = `max(skyContribution, blockContribution)` + minimum ambient floor (configurable, default 0.02).
5. **Day/night factor**: Smooth sinusoidal cycle: 1.0 at noon, 0.1 at midnight. `dayNightFactor = 0.55 + 0.45 * sin(2π * timeOfDay - π/2)`.
6. **Configurable cycle duration**: Default 20 minutes real-time = full day. Adjustable via ImGui slider.
7. **Sun direction changes with time**: Sun rotates in a semicircle from east (sunrise) through zenith (noon) to west (sunset). Below horizon at night.
8. **Sky color varies with time**: Sky pixels in lighting.frag change from light blue (day) → warm orange (sunset/sunrise) → dark blue/black (night).
9. **Translucent pass consistency**: `translucent.frag` applies the same block/sky light formula using `dayNightFactor` from push constants.
10. **Debug overlay**: F3 overlay shows "Time: HH:MM (Day/Dusk/Night/Dawn)" with current cycle phase.
11. **Minimum ambient**: Pitch-black caves remain barely visible (ambient floor = 0.02, configurable).

## Tasks / Subtasks

- [x] Task 1: Add RT2 to G-Buffer (AC: 1)
  - [x] 1.1 Add `GBUFFER_RT2_FORMAT = VK_FORMAT_R8G8_UNORM` to `RendererConstants.h`
  - [x] 1.2 Add `m_lightImage`, `m_lightAllocation`, `m_lightView` members to `GBuffer.h`
  - [x] 1.3 Add `getLightImage()`, `getLightView()` accessors to `GBuffer.h`
  - [x] 1.4 In `GBuffer::create()`, allocate RT2 image alongside RT0/RT1
  - [x] 1.5 In `GBuffer::~GBuffer()`, destroy RT2 image and view

- [x] Task 2: Wire RT2 through Renderer (AC: 1)
  - [x] 2.1 In `Renderer::beginRenderPass()`, add RT2 as third color attachment to G-Buffer render pass (alongside RT0 albedo and RT1 normal)
  - [x] 2.2 In `Renderer::endFrame()`, transition RT2 to `SHADER_READ_ONLY_OPTIMAL` alongside RT0/RT1 before the lighting pass
  - [x] 2.3 In `Renderer::beginFrame()`, transition RT2 to `COLOR_ATTACHMENT_OPTIMAL` at start (alongside RT0/RT1)
  - [x] 2.4 Add binding 3 (combined image sampler, fragment stage) to lighting descriptor set layout in `createLightingPipeline()`
  - [x] 2.5 In `writeLightingDescriptors()`, add RT2 write to binding 3

- [x] Task 3: Update push constants (AC: 5, 6, 7)
  - [x] 3.1 In `ChunkPushConstants`, replace `pad[0]` with `dayNightFactor` (offset 72, same 96-byte total)
  - [x] 3.2 Extend `LightingPushConstants` to include `dayNightFactor` (float) and `timeOfDay` (float) — total 24 bytes
  - [x] 3.3 Update `static_assert` for `LightingPushConstants`
  - [x] 3.4 Update push constant range size in `createLightingPipeline()` to `sizeof(LightingPushConstants)` (already uses sizeof — auto-adjusts)

- [x] Task 4: Update gbuffer.frag (AC: 1)
  - [x] 4.1 Add `layout(location = 2) out vec2 outLight` output (R = skyLight, G = blockLight)
  - [x] 4.2 Write `outLight = vec2(fragSkyLight, fragBlockLight)` in main()
  - [x] 4.3 Remove "Wired but unused" comments from fragSkyLight/fragBlockLight

- [x] Task 5: Update lighting.frag (AC: 2, 3, 4, 8, 11)
  - [x] 5.1 Add `layout(set = 0, binding = 3) uniform sampler2D gbufferLight` for RT2
  - [x] 5.2 Add `dayNightFactor` and `timeOfDay` to push constants struct
  - [x] 5.3 Read sky/block light from RT2: `vec2 light = texture(gbufferLight, fragUV).rg`
  - [x] 5.4 Compute sky contribution: `skyLight * dayNightFactor * vec3(0.95, 0.95, 1.0)` (cool white/blue)
  - [x] 5.5 Compute block contribution: `blockLight * vec3(1.0, 0.85, 0.7)` (warm orange)
  - [x] 5.6 Combined light: `max(skyContribution, blockContribution) + ambientFloor`
  - [x] 5.7 Apply to albedo: `color = (albedo * lightLevel + sunBonus) * ao`
  - [x] 5.8 For sky pixels (depth >= 1.0): compute sky color from `dayNightFactor` — day blue → sunset orange → night dark blue
  - [x] 5.9 Minimum ambient floor of 0.02 so caves are barely visible

- [x] Task 6: Update translucent.frag (AC: 9)
  - [x] 6.1 Add `dayNightFactor` field to push constants (replace `_pad0` at offset 72)
  - [x] 6.2 Compute block contribution: `fragBlockLight * vec3(1.0, 0.85, 0.7)`
  - [x] 6.3 Compute sky contribution: `fragSkyLight * pc.dayNightFactor * vec3(0.95, 0.95, 1.0)`
  - [x] 6.4 Combined: `max(skyContribution, blockContribution) + ambientFloor`
  - [x] 6.5 Replace current NdotL directional lighting with voxel light model
  - [x] 6.6 Remove "Wired but unused" comments

- [x] Task 7: Add TimeOfDay system to Renderer (AC: 5, 6, 7)
  - [x] 7.1 Add `m_timeOfDay` (float 0.0–1.0), `m_cycleDuration` (float seconds, default 1200.0f), `m_lastFrameTime` (double) members to Renderer
  - [x] 7.2 Add `setTimeOfDay(float t)`, `getTimeOfDay()`, `setCycleDuration(float seconds)` public methods
  - [x] 7.3 In `beginFrame()`, advance `m_timeOfDay` by `deltaTime / m_cycleDuration`, wrap at 1.0
  - [x] 7.4 Compute `dayNightFactor = 0.55f + 0.45f * sin(2π * m_timeOfDay - π/2)` each frame
  - [x] 7.5 Compute `sunDirection` from `m_timeOfDay`: rotate from east→zenith→west in a semicircle, below horizon at night
  - [x] 7.6 Pass computed values to all three push constants (opaque, lighting, translucent)

- [x] Task 8: Debug overlay (AC: 10)
  - [x] 8.1 Expose Renderer getters: `getTimeOfDay()`, `getDayNightFactor()`, `getCycleDuration()`
  - [x] 8.2 In GameApp's F3 overlay, add line: `Time: HH:MM (Phase)` where Phase ∈ {Day, Dusk, Night, Dawn}
  - [x] 8.3 Add ImGui slider for cycle duration (1–60 minutes) and time-of-day override

- [x] Task 9: Compile shaders + build (AC: all)
  - [x] 9.1 Recompile all modified shaders to SPIR-V (gbuffer.frag, lighting.frag, translucent.frag)
  - [x] 9.2 Build and verify zero warnings
  - [x] 9.3 Visual test: torch in dark cave = warm orange glow, daytime surface = bright, nighttime = dim with sky light reduced

## Dev Notes

### Critical Architectural Challenge: Per-Vertex Light in Deferred Pipeline

The G-Buffer currently stores only albedo+AO (RT0) and normals (RT1). Per-vertex sky/block light values are interpolated in the geometry pass but **not written** to any G-Buffer attachment. The deferred lighting pass therefore cannot access them.

**Solution: Add G-Buffer RT2 for light data.**

| Attachment | Format | Content |
|-----------|--------|---------|
| RT0 | R8G8B8A8_SRGB | Albedo.rgb + AO.a (existing) |
| RT1 | R16G16_SFLOAT | Normal.xy octahedral (existing) |
| RT2 | R8G8_UNORM | SkyLight.r + BlockLight.g (**NEW**) |
| Depth | D32_SFLOAT | Hardware depth (existing) |

`R8G8_UNORM` is 2 bytes/pixel and more than sufficient — we only have 16 discrete light levels (0–15) mapped to 0.0–1.0. Bandwidth cost is negligible (adds ~4MB for 1080p).

### Current State of Lighting Pipeline (What Exists)

Everything below was wired in Story 8.0 and works:

| Component | Status | File |
|-----------|--------|------|
| LightMap (per-section storage) | DONE | `engine/include/voxel/world/LightMap.h` |
| MeshBuilder reads light + packs into quadLightData | DONE | `engine/src/renderer/MeshBuilder.cpp` |
| chunk.vert extracts per-corner light | DONE | `assets/shaders/chunk.vert:251-283` |
| fragSkyLight / fragBlockLight interpolated to fragment | DONE | `assets/shaders/gbuffer.frag:6-7, translucent.frag:6-7` |
| G-Buffer (RT0 albedo+AO, RT1 normal) | DONE | `engine/include/voxel/renderer/GBuffer.h` |
| Deferred lighting pass (fullscreen triangle) | DONE | `assets/shaders/lighting.vert`, `lighting.frag` |
| Translucent forward pass | DONE | `assets/shaders/translucent.frag` |
| Push constants (ChunkPushConstants, LightingPushConstants) | DONE | `engine/include/voxel/renderer/Renderer.h:40-56` |
| Sun direction + ambient strength | HARDCODED | `Renderer.cpp:1358-1359, 1456-1457, 1532-1533` |

**What's NOT done (this story):**
- gbuffer.frag does NOT write skyLight/blockLight to any output — they're input-only
- lighting.frag does NOT read or use light values — only directional + ambient
- translucent.frag does NOT use fragSkyLight/fragBlockLight — only directional + ambient
- No time-of-day system exists
- sunDirection is hardcoded to `normalize(vec3(0.3, 1.0, 0.5))`
- ambientStrength is hardcoded to `0.3f`

### Lighting Formula

Replace the current directional + ambient model with voxel-aware lighting:

```glsl
// In lighting.frag:
vec2 lightValues = texture(gbufferLight, fragUV).rg;  // R=sky, G=block
float skyLight = lightValues.r;
float blockLight = lightValues.g;

// Sky contribution: modulated by day/night
vec3 skyColor = skyLight * pc.dayNightFactor * vec3(0.95, 0.95, 1.0);

// Block contribution: warm orange, always full strength
vec3 blockColor = blockLight * vec3(1.0, 0.85, 0.7);

// Take max (not add) — matches Minecraft behavior
vec3 lightLevel = max(skyColor, blockColor);

// Minimum ambient so caves aren't pitch black
lightLevel = max(lightLevel, vec3(pc.ambientStrength));

// Add directional sun as bonus (subtle outdoors, zero at night)
float NdotL = max(dot(normal, normalize(pc.sunDirection)), 0.0);
vec3 sunBonus = albedo * NdotL * 0.15 * pc.dayNightFactor;

vec3 color = (albedo * lightLevel + sunBonus) * ao;
```

The translucent pass uses the same formula but reads fragSkyLight/fragBlockLight directly from vertex interpolation (no G-Buffer read needed in forward pass).

### Day/Night Cycle Parameters

```
timeOfDay: float in [0.0, 1.0]
  0.00 = midnight
  0.25 = sunrise (06:00)
  0.50 = noon (12:00)
  0.75 = sunset (18:00)

dayNightFactor: float in [0.1, 1.0]
  = 0.55 + 0.45 * sin(2π * timeOfDay - π/2)
  Peaks at 1.0 at noon (0.50), bottoms at 0.1 at midnight (0.00)

cycleDuration: float in seconds (default 1200 = 20 minutes)
  Advance per frame: deltaTime / cycleDuration
  Wrap: timeOfDay = fmod(timeOfDay, 1.0)

Display time:
  hours = (int)(timeOfDay * 24) % 24
  minutes = (int)(timeOfDay * 24 * 60) % 60
  phase: [0.20-0.30] = Dawn, [0.30-0.70] = Day, [0.70-0.80] = Dusk, else = Night
```

### Sun Direction Calculation

```cpp
// Sun travels in a semicircle: east (sunrise) → top (noon) → west (sunset)
// Below horizon at night (dayNightFactor handles dimming)
float sunAngle = (m_timeOfDay - 0.25f) * glm::pi<float>(); // 0 at sunrise, π at sunset
glm::vec3 sunDir;
sunDir.x = -cos(sunAngle);   // East(-1) → West(+1)
sunDir.y = sin(sunAngle);    // Up at noon
sunDir.z = 0.3f;             // Slight Z offset for visual interest
sunDir = glm::normalize(sunDir);

// Clamp Y to 0 at night (sun below horizon → no directional light)
if (sunDir.y < 0.0f) sunDir.y = 0.0f;
sunDir = glm::normalize(sunDir);
```

### Sky Color Gradient

```glsl
// In lighting.frag, for sky pixels (depth >= 1.0):
vec3 daySky = vec3(0.4, 0.6, 0.9);       // Light blue
vec3 nightSky = vec3(0.01, 0.01, 0.03);   // Near-black
vec3 sunsetSky = vec3(0.9, 0.5, 0.2);     // Warm orange

// Blend based on dayNightFactor
float sunsetFactor = smoothstep(0.3, 0.5, pc.dayNightFactor) * smoothstep(0.8, 0.6, pc.dayNightFactor);
vec3 skyColor = mix(nightSky, daySky, pc.dayNightFactor);
skyColor = mix(skyColor, sunsetSky, sunsetFactor * 0.5);

outColor = vec4(skyColor, 1.0);
```

### Push Constant Layout Changes

**ChunkPushConstants (96 bytes — UNCHANGED SIZE):**
```cpp
struct ChunkPushConstants
{
    glm::mat4 viewProjection; // 64 bytes, offset 0
    float time;               // 4 bytes,  offset 64
    float ambientStrength;    // 4 bytes,  offset 68
    float dayNightFactor;     // 4 bytes,  offset 72  ← was pad[0]
    float pad;                // 4 bytes,  offset 76  ← was pad[1]
    glm::vec4 sunDirection;   // 16 bytes, offset 80
};
static_assert(sizeof(ChunkPushConstants) == 96);
```

**LightingPushConstants (24 bytes — was 16):**
```cpp
struct LightingPushConstants
{
    glm::vec3 sunDirection;   // 12 bytes
    float ambientStrength;    // 4 bytes
    float dayNightFactor;     // 4 bytes  ← NEW
    float timeOfDay;          // 4 bytes  ← NEW (for sky color computation)
};
static_assert(sizeof(LightingPushConstants) == 24);
```

Update the push constant range size in `createLightingPipeline()` from `sizeof(LightingPushConstants)` — it auto-adjusts since it uses `sizeof`.

### G-Buffer Render Pass Changes

In `Renderer::renderChunksIndirect()`, the G-Buffer render pass currently has 2 color attachments + depth. Add a third:

```cpp
// Existing:
VkRenderingAttachmentInfo colorAttachments[2] = { albedoAttachment, normalAttachment };
// Change to:
VkRenderingAttachmentInfo colorAttachments[3] = { albedoAttachment, normalAttachment, lightAttachment };

// Light attachment:
VkRenderingAttachmentInfo lightAttachment{};
lightAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
lightAttachment.imageView = m_gbuffer->getLightView();
lightAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
lightAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
lightAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
lightAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

renderingInfo.colorAttachmentCount = 3;  // was 2
```

### Image Transitions

RT2 needs the same transitions as RT0/RT1:
1. Start of frame: `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL` (before G-Buffer pass)
2. After G-Buffer pass: `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` (before lighting pass)

Use the existing image transition helper. RT2 transitions can be batched with RT0/RT1 in the same pipeline barrier.

### Lighting Descriptor Set Changes

Add binding 3 for RT2:

```cpp
// In createLightingPipeline():
DescriptorLayoutBuilder lightBuilder;
auto lightLayoutResult =
    lightBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // RT0
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // RT1
        .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // Depth
        .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // RT2 Light
        .build(device);
```

```cpp
// In writeLightingDescriptors(), add 4th write:
VkDescriptorImageInfo lightInfo{};
lightInfo.sampler = m_gbuffer->getSampler();
lightInfo.imageView = m_gbuffer->getLightView();
lightInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
writes[3].dstSet = m_lightingDescriptorSet;
writes[3].dstBinding = 3;
writes[3].descriptorCount = 1;
writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
writes[3].pImageInfo = &lightInfo;
```

### Time Tracking in Renderer

Add to Renderer.h private members:
```cpp
float m_timeOfDay = 0.5f;          // Start at noon
float m_cycleDuration = 1200.0f;   // 20 minutes = 1200 seconds
float m_dayNightFactor = 1.0f;     // Computed each frame
glm::vec3 m_sunDirection{0.0f, 1.0f, 0.3f};  // Computed each frame
double m_lastFrameTime = 0.0;      // For delta time calculation
```

In `beginFrame()`, use `glfwGetTime()` for high-resolution time delta. This is already imported (GLFW is used for window).

### Hardcoded Values to Replace

Three locations in `Renderer.cpp` currently hardcode sun/ambient:

1. **Line 1358-1359** (opaque pass): `ambientStrength = 0.3f; sunDirection = normalize(0.3, 1.0, 0.5)`
2. **Line 1456-1457** (lighting pass): same hardcoded values
3. **Line 1532-1533** (translucent pass): same hardcoded values

All three must read from `m_dayNightFactor`, `m_sunDirection`, and use a configurable ambient floor (0.02 default).

### What NOT to Do

- DO NOT modify LightMap.h — complete from Story 8.0
- DO NOT modify MeshBuilder.cpp — light-to-vertex pipeline already wired
- DO NOT modify chunk.vert — per-corner light extraction already works
- DO NOT modify cull.comp or cull_translucent.comp — unrelated to lighting
- DO NOT modify light propagation (Stories 8.1-8.3) — this story only consumes light values
- DO NOT create a separate TimeOfDay class — keep it as Renderer members (< 20 lines of state)
- DO NOT add UBO for lighting params — push constants are sufficient (24 bytes is well within 128-byte minimum)
- DO NOT change the light averaging in MeshBuilder — vertex light values are correct as-is
- DO NOT implement shadows, fog, or post-processing — out of scope for this story
- DO NOT add G-Buffer RT2 to the translucent pass — forward pass reads per-vertex light directly

### File Structure

| Action | File | Notes |
|--------|------|-------|
| MODIFY | `engine/include/voxel/renderer/RendererConstants.h` | Add `GBUFFER_RT2_FORMAT` |
| MODIFY | `engine/include/voxel/renderer/GBuffer.h` | Add RT2 image/view/allocation + accessors |
| MODIFY | `engine/src/renderer/GBuffer.cpp` | Create/destroy RT2 image |
| MODIFY | `engine/include/voxel/renderer/Renderer.h` | Update push constant structs, add time members + methods |
| MODIFY | `engine/src/renderer/Renderer.cpp` | Wire RT2, update transitions/descriptors, time system, push constants |
| MODIFY | `assets/shaders/gbuffer.frag` | Write skyLight/blockLight to RT2 output |
| MODIFY | `assets/shaders/lighting.frag` | Read RT2, apply block/sky/day-night lighting, sky color |
| MODIFY | `assets/shaders/translucent.frag` | Apply block/sky light with dayNightFactor |
| MODIFY | `game/src/GameApp.cpp` | Debug overlay time display, ImGui slider for cycle duration |

**No new files created.** This story modifies only existing files.

### Naming & Style

- Members: `m_timeOfDay`, `m_cycleDuration`, `m_dayNightFactor`, `m_sunDirection`
- Methods: `setTimeOfDay()`, `getTimeOfDay()`, `setCycleDuration()`
- Constants: `GBUFFER_RT2_FORMAT`
- Push constant fields: `dayNightFactor`, `timeOfDay` (camelCase for struct members, no `m_` prefix)
- Namespace: `voxel::renderer`

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| GBuffer class | `engine/include/voxel/renderer/GBuffer.h` | Extend with RT2 (follow RT0/RT1 pattern) |
| DescriptorLayoutBuilder | `engine/src/renderer/Renderer.cpp:724` | Add binding 3 for RT2 |
| Image transition helpers | `Renderer.cpp` inline barriers | Add RT2 to existing barrier batches |
| ChunkPushConstants | `Renderer.h:40-47` | Replace pad[0] with dayNightFactor |
| LightingPushConstants | `Renderer.h:51-55` | Extend with dayNightFactor + timeOfDay |
| glfwGetTime() | Already used | For delta time in time-of-day advancement |
| ImGui overlay | `GameApp.cpp:570-630` | Add time display line |

### Git Intelligence

Recent commits show the project's lighting evolution:
- `23dd811` — Story 8.0: light averaging tests + GPU upload handling (parallel light buffer)
- `0952f4f` — Story 8.0: LightMap creation, meshing pipeline integration
- Both establish the pattern: parallel data vectors, light packed into gigabuffer after quad data
- Push constants already carry `sunDirection` and `ambientStrength` — this story makes them dynamic

### Project Structure Notes

- All modifications are within existing files — no new files needed
- Changes span two modules: `renderer` (GBuffer, Renderer, shaders) and `game` (GameApp overlay)
- Shader changes require SPIR-V recompilation. The build system handles this via `glslangValidator` during CMake build
- No CMakeLists.txt changes needed (no new source files)

### Performance Considerations

- RT2 at R8G8_UNORM = 2 bytes/pixel. At 1080p: 2 × 1920 × 1080 = ~4MB. Negligible VRAM impact
- One additional texture read in lighting.frag per pixel — minimal since the G-Buffer textures share cache lines
- dayNightFactor computation is one `sin()` per frame — effectively free
- No additional draw calls or passes — RT2 is written in the existing geometry pass and read in the existing lighting pass

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-08-lighting.md — Story 8.4 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 5: Vulkan Renderer, Deferred Rendering, G-Buffer layout, lighting.frag description]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 8: Lighting, dual light system, renderer integration]
- [Source: _bmad-output/planning-artifacts/ux-spec.md — Section 8: Sky color changes with day/night, debug overlay shows Time/Biome]
- [Source: _bmad-output/project-context.md — Naming conventions, push constant patterns, shader compilation]
- [Source: engine/include/voxel/renderer/Renderer.h:40-56 — ChunkPushConstants, LightingPushConstants structs]
- [Source: engine/include/voxel/renderer/GBuffer.h — Current 2-RT G-Buffer structure]
- [Source: engine/include/voxel/renderer/RendererConstants.h — G-Buffer format constants]
- [Source: engine/src/renderer/Renderer.cpp:719-778 — createLightingPipeline, descriptor layout, pipeline layout]
- [Source: engine/src/renderer/Renderer.cpp:783-819 — writeLightingDescriptors (3 bindings)]
- [Source: engine/src/renderer/Renderer.cpp:1355-1462 — renderChunksIndirect + lighting pass push constants]
- [Source: engine/src/renderer/Renderer.cpp:1529-1536 — translucent pass push constants]
- [Source: assets/shaders/gbuffer.frag — fragSkyLight/fragBlockLight wired but unused]
- [Source: assets/shaders/lighting.frag — Current directional+ambient only, no voxel light]
- [Source: assets/shaders/translucent.frag — Current directional+ambient only, no voxel light]
- [Source: _bmad-output/implementation-artifacts/8-0-wire-light-data-into-meshing-pipeline.md — Light pipeline wiring]
- [Source: _bmad-output/implementation-artifacts/8-1-light-data-storage-bfs-block-light.md — Block light BFS reference]
- [Source: _bmad-output/implementation-artifacts/8-2-sky-light-propagation.md — Sky light propagation reference]
- [Source: _bmad-output/implementation-artifacts/8-3-dynamic-light-updates.md — Dynamic light update reference]

## Dev Agent Record

### Agent Model Used
Claude Opus 4.6

### Debug Log References
- Build initially failed due to missing `glm/ext/scalar_constants.hpp` include for `glm::pi<float>()`. Fixed by adding the include.
- All 490,085 assertions in 241 test cases pass — zero regressions.

### Completion Notes List
- Added G-Buffer RT2 (R8G8_UNORM) for per-vertex sky/block light storage in deferred pipeline
- gbuffer.frag now writes `outLight = vec2(fragSkyLight, fragBlockLight)` to RT2
- lighting.frag reads RT2, computes voxel-aware lighting: sky contribution modulated by dayNightFactor, block contribution as warm orange, combined via max(), with configurable ambient floor (0.02)
- Sky pixels render time-of-day color gradient: day blue → sunset orange → night dark blue
- translucent.frag updated with identical voxel light model using per-vertex light values and dayNightFactor from push constants
- TimeOfDay system added to Renderer: sinusoidal day/night factor, sun direction semicircle rotation, all driven from `beginFrame()` delta time
- Push constants updated: ChunkPushConstants.dayNightFactor replaces pad[0]; LightingPushConstants extended to 24 bytes with dayNightFactor + timeOfDay
- Debug overlay shows Time: HH:MM (Dawn/Day/Dusk/Night) + dayNightFactor; ImGui sliders for time-of-day override and cycle duration (1–60 min)
- All three hardcoded sunDirection/ambientStrength locations replaced with dynamic computed values

### File List
- engine/include/voxel/renderer/RendererConstants.h (MODIFIED — added GBUFFER_RT2_FORMAT)
- engine/include/voxel/renderer/GBuffer.h (MODIFIED — added RT2 members + accessors)
- engine/src/renderer/GBuffer.cpp (MODIFIED — RT2 create/destroy)
- engine/include/voxel/renderer/Renderer.h (MODIFIED — push constants, time-of-day members + methods)
- engine/src/renderer/Renderer.cpp (MODIFIED — RT2 wiring, transitions, descriptors, time-of-day system, dynamic push constants)
- assets/shaders/gbuffer.frag (MODIFIED — RT2 output for sky/block light)
- assets/shaders/lighting.frag (MODIFIED — voxel light model + sky color + day/night cycle)
- assets/shaders/translucent.frag (MODIFIED — voxel light model with dayNightFactor)
- game/src/GameApp.cpp (MODIFIED — debug overlay time display + ImGui sliders)

### Change Log
- 2026-03-30: Implemented Story 8.4 — Deferred Lighting Pass + Day/Night Cycle. Added G-Buffer RT2 for light data, voxel-aware lighting in deferred and forward passes, sinusoidal day/night cycle with sun direction rotation, sky color gradient, and debug overlay controls.
