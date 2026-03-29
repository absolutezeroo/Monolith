# Story 6.8: Block Tinting Shader Support

Status: ready-for-dev

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want the fragment shaders to apply per-vertex biome tinting from a GPU-uploaded palette,
so that grass, leaves, and water change color based on biome without per-block texture variants.

## Acceptance Criteria

1. `gbuffer.frag` reads `TintPalette` from SSBO (binding 5, set 0) containing 8 entries (vec4, .rgb used), multiplies `albedo.rgb *= tintPalette[fragTintIndex].rgb` before writing to G-Buffer RT0.
2. `translucent.frag` applies the same tint multiplication before lighting calculation.
3. `chunk.frag` (forward fallback) applies the same tint multiplication.
4. Index 0 = white `(1,1,1)` = no tint = no visual change. All non-tinted blocks remain pixel-identical.
5. Tint palette uploaded as HOST_VISIBLE SSBO at init, 128 bytes (8 x `vec4`). Persistently mapped for fast runtime updates.
6. Descriptor set layout extended with binding 5 (`STORAGE_BUFFER`, `FRAGMENT_BIT`). Both `m_chunkDescriptorSet` and `m_transDescriptorSet` updated.
7. `blocks.json` updated: `grass_block` tintIndex=1, all leaves tintIndex=2, `tall_grass` tintIndex=1, `water` tintIndex=3.
8. Default palette initialized from `TintPalette::buildForBiome(BiomeType::Plains)` at renderer init.
9. `Renderer::updateTintPalette(const TintPalette&)` method for runtime biome-driven updates (V1: called once at init; future: per-chunk or on biome change).
10. Zero Vulkan validation errors. Grass blocks visibly green-tinted, leaves foliage-tinted, water blue-tinted in Plains biome.

## Tasks / Subtasks

- [ ] **Task 1: Update blocks.json with tintIndex values** (AC: #7)
  - [ ] Add `"tintIndex": 1` to `base:grass_block` (TINT_GRASS — biome-dependent green on all faces)
  - [ ] Add `"tintIndex": 2` to `base:oak_leaves` (TINT_FOLIAGE)
  - [ ] Add `"tintIndex": 2` to `base:birch_leaves` (TINT_FOLIAGE)
  - [ ] Add `"tintIndex": 2` to `base:spruce_leaves` (TINT_FOLIAGE)
  - [ ] Add `"tintIndex": 2` to `base:jungle_leaves` (TINT_FOLIAGE)
  - [ ] Add `"tintIndex": 1` to `base:tall_grass` (TINT_GRASS — follows grass block coloring)
  - [ ] Add `"tintIndex": 3` to `base:water` (TINT_WATER)
  - [ ] Do NOT add tintIndex to flowers (they have natural color in textures), dead_bush, cactus, ores, stone, etc.

- [ ] **Task 2: Create tint palette GPU buffer** (AC: #5)
  - [ ] Add to `Renderer.h` private members:
    ```cpp
    VkBuffer m_tintPaletteBuffer = VK_NULL_HANDLE;
    VmaAllocation m_tintPaletteAllocation = VK_NULL_HANDLE;
    glm::vec4* m_tintPaletteMapped = nullptr;
    ```
  - [ ] In `Renderer::init()`, after descriptor allocator setup and before descriptor writes:
    ```cpp
    // Tint palette SSBO: 8 x vec4 = 128 bytes, HOST_VISIBLE + persistently mapped
    VkBufferCreateInfo tintBufInfo{};
    tintBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tintBufInfo.size = 8 * sizeof(glm::vec4);  // 128 bytes
    tintBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo tintAllocInfo{};
    tintAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    tintAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo tintMappedInfo{};
    vmaCreateBuffer(allocator, &tintBufInfo, &tintAllocInfo,
                    &m_tintPaletteBuffer, &m_tintPaletteAllocation, &tintMappedInfo);
    m_tintPaletteMapped = static_cast<glm::vec4*>(tintMappedInfo.pMappedData);
    ```
  - [ ] In `Renderer::shutdown()`, destroy before descriptor allocator:
    ```cpp
    if (m_tintPaletteBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, m_tintPaletteBuffer, m_tintPaletteAllocation);
        m_tintPaletteBuffer = VK_NULL_HANDLE;
        m_tintPaletteMapped = nullptr;
    }
    ```

- [ ] **Task 3: Extend descriptor set layout with binding 5** (AC: #6)
  - [ ] In `Renderer.cpp` descriptor layout builder (currently lines ~101-109), add binding 5:
    ```cpp
    .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
    ```
  - [ ] Update the comment block above the builder to document binding 5:
    ```
    //   binding 5 = SSBO (tint palette, fragment stage)
    ```
  - [ ] No changes needed to pipeline layout — it already uses `m_chunkDescriptorSetLayout` which will include binding 5 automatically.

- [ ] **Task 4: Write tint palette to both descriptor sets** (AC: #6)
  - [ ] In the opaque descriptor writes section, expand from `std::array<VkWriteDescriptorSet, 5>` to `std::array<VkWriteDescriptorSet, 6>`:
    ```cpp
    VkDescriptorBufferInfo tintPaletteInfo{};
    tintPaletteInfo.buffer = m_tintPaletteBuffer;
    tintPaletteInfo.offset = 0;
    tintPaletteInfo.range = VK_WHOLE_SIZE;

    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = m_chunkDescriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[5].pBufferInfo = &tintPaletteInfo;
    ```
  - [ ] Repeat for the translucent descriptor writes section (`m_transDescriptorSet`):
    - Same expansion from 5 to 6 entries
    - `descriptorWrites[5].dstSet = m_transDescriptorSet;`

- [ ] **Task 5: Implement updateTintPalette method** (AC: #8, #9)
  - [ ] Add public method to `Renderer.h`:
    ```cpp
    /// Upload a TintPalette to the GPU. Converts vec3 → vec4 (w=1) for std430 alignment.
    void updateTintPalette(const TintPalette& palette);
    ```
  - [ ] Implement in `Renderer.cpp`:
    ```cpp
    void Renderer::updateTintPalette(const TintPalette& palette)
    {
        if (m_tintPaletteMapped == nullptr) return;
        for (uint8_t i = 0; i < TintPalette::MAX_ENTRIES; ++i)
        {
            glm::vec3 color = palette.getColor(i);
            m_tintPaletteMapped[i] = glm::vec4(color, 1.0f);
        }
    }
    ```
  - [ ] Call at end of `init()` with default Plains palette:
    ```cpp
    TintPalette defaultPalette = TintPalette::buildForBiome(world::BiomeType::Plains);
    updateTintPalette(defaultPalette);
    ```
  - [ ] Add `#include "voxel/renderer/TintPalette.h"` to Renderer.cpp

- [ ] **Task 6: Update gbuffer.frag** (AC: #1, #4)
  - [ ] Add SSBO declaration after existing bindings:
    ```glsl
    layout(std430, set = 0, binding = 5) readonly buffer TintPaletteSSBO {
        vec4 colors[8];
    } tintPalette;
    ```
  - [ ] After texture sampling and before G-Buffer write, apply tint:
    ```glsl
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));
    if (texColor.a < 0.5)
        discard;

    // Apply biome tint (index 0 = white = no change)
    vec3 tint = tintPalette.colors[fragTintIndex].rgb;

    outAlbedoAO = vec4(texColor.rgb * tint, fragAO);
    ```

- [ ] **Task 7: Update translucent.frag** (AC: #2)
  - [ ] Add same SSBO declaration as gbuffer.frag (binding 5)
  - [ ] Apply tint before lighting calculation:
    ```glsl
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));
    if (texColor.a < 0.01)
        discard;

    vec3 tint = tintPalette.colors[fragTintIndex].rgb;
    vec3 tintedColor = texColor.rgb * tint;

    // ... lighting calculation uses tintedColor instead of texColor.rgb ...
    outColor = vec4(tintedColor * lighting, texColor.a);
    ```

- [ ] **Task 8: Update chunk.frag** (AC: #3)
  - [ ] Add same SSBO declaration (binding 5)
  - [ ] Apply tint:
    ```glsl
    vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));
    vec3 tint = tintPalette.colors[fragTintIndex].rgb;
    float ao = mix(0.4, 1.0, fragAO);
    vec3 color = texColor.rgb * tint * ao;
    outColor = vec4(color, texColor.a);
    ```

- [ ] **Task 9: Recompile shaders and validate** (AC: #10)
  - [ ] Recompile `gbuffer.frag` → `gbuffer.frag.spv`
  - [ ] Recompile `translucent.frag` → `translucent.frag.spv`
  - [ ] Recompile `chunk.frag` → `chunk.frag.spv`
  - [ ] Build with `/W4 /WX` — zero warnings
  - [ ] Run with Vulkan validation layers — zero errors
  - [ ] Visual validation: grass blocks show biome-dependent green tint, leaves show foliage tint, water shows blue tint; stone/dirt/glass remain unchanged

## Dev Notes

### What Already Exists — DO NOT Recreate

The CPU-side tint infrastructure is **100% complete** from Story 5.5. Do NOT create new classes or modify existing ones:

- **`TintPalette`** class (`engine/include/voxel/renderer/TintPalette.h`, `engine/src/renderer/TintPalette.cpp`): 8-entry RGB LUT, `buildForBiome()` factory for all 8 biome types. Constants: `TINT_NONE=0`, `TINT_GRASS=1`, `TINT_FOLIAGE=2`, `TINT_WATER=3`.
- **`BlockDefinition::tintIndex`** (`engine/include/voxel/world/Block.h:75`): `uint8_t tintIndex = 0` — already parsed from JSON by `BlockRegistry::loadFromJson()`.
- **Quad bit packing** (`engine/include/voxel/renderer/ChunkMesh.h`): `packQuad()` stores tintIndex at bits [59:61]. `unpackTintIndex()` extracts it.
- **MeshBuilder** (`engine/src/renderer/MeshBuilder.cpp`): Both `buildNaive()` and `buildGreedy()` already pass `blockDef.tintIndex` to `packQuad()`.
- **chunk.vert** (`assets/shaders/chunk.vert:80`): Already unpacks tintIndex via `bitfieldExtract(hi, 27, 3)` and outputs as `flat uint fragTintIndex` at location 5.
- **All fragment shaders**: Already declare `layout(location = 5) flat in uint fragTintIndex` but currently **ignore it**. This story wires it up.
- **BiomeSystem** (`engine/include/voxel/world/BiomeSystem.h`): Whittaker classification for 8 biome types.
- **Tests** (`tests/renderer/TestTintWaving.cpp`): Comprehensive quad packing, meshing, and palette tests already pass.

### Architecture Compliance

- **SSBO over UBO**: Use `STORAGE_BUFFER` (std430 layout) rather than `UNIFORM_BUFFER` (std140). std430 avoids the vec3→vec4 padding ambiguity. We pad to vec4 explicitly on the C++ upload side (`glm::vec4(color, 1.0f)`). [Source: architecture.md#Vulkan Renderer]
- **RAII**: No new RAII classes. Buffer is a raw Vulkan resource owned by Renderer, destroyed in `shutdown()`. [Source: project-context.md#Memory & Ownership]
- **Error handling**: Buffer creation uses VMA — check return code, log fatal on failure. `updateTintPalette()` is a pure memory copy, no failure mode. [Source: project-context.md#Error Handling]
- **Naming**: `m_tintPaletteBuffer`, `m_tintPaletteMapped`, `updateTintPalette()` follow conventions. [Source: CLAUDE.md#Naming Conventions]

### Descriptor Binding Map (After This Story)

| Binding | Type | Stage | Resource |
|---------|------|-------|----------|
| 0 | STORAGE_BUFFER | VERTEX | Gigabuffer (quad data) |
| 1 | STORAGE_BUFFER | VERTEX + COMPUTE | ChunkRenderInfo SSBO |
| 2 | STORAGE_BUFFER | COMPUTE | Indirect command buffer |
| 3 | STORAGE_BUFFER | COMPUTE | Draw count buffer |
| 4 | COMBINED_IMAGE_SAMPLER | FRAGMENT | Block texture array |
| **5** | **STORAGE_BUFFER** | **FRAGMENT** | **Tint palette (8 x vec4 = 128 bytes)** |

Both `m_chunkDescriptorSet` (opaque) and `m_transDescriptorSet` (translucent) share the same layout and both get binding 5 pointing to the same `m_tintPaletteBuffer`.

### std430 GLSL Layout

In the SSBO declaration, use `vec4 colors[8]` (not `vec3`). In std430, a `vec3` has alignment of 16 bytes but size of 12 bytes, creating subtle padding issues. Using `vec4` eliminates ambiguity and matches the C++ upload side (`glm::vec4(color, 1.0f)`). The `.rgb` swizzle in the shader extracts the 3-component color.

### GPU Buffer Sizing

`8 * sizeof(glm::vec4) = 8 * 16 = 128 bytes` — trivially small. HOST_VISIBLE + HOST_COHERENT with persistent mapping means zero overhead for runtime updates. No staging buffer needed.

### V1 Limitations (Acceptable)

- **Per-block tinting, not per-face**: `grass_block` bottom face (dirt texture) gets grass tint too. Minor visual artifact — Minecraft uses per-face tint via block model JSON. Per-face tint requires a mesher change (different tintIndex per face) which is out of scope.
- **Global palette, not per-chunk**: All chunks use the same tint palette. At biome boundaries, chunks snap to the dominant biome's colors. Smooth blending requires per-chunk palette upload (Future: per-chunk tint via ChunkRenderInfo extension).
- **Static at init**: `updateTintPalette()` is called once. Dynamic biome-driven updates (as player moves across biomes) require integration with ChunkManager's biome tracking, planned for future.

### Shutdown Order

Insert tint palette buffer destruction into existing sequence in `Renderer::shutdown()`:
```
1. vkDeviceWaitIdle
2. ImGui
3. StagingBuffer, TextureArray
4. Translucent indirect buffers
5. Tint palette buffer ← NEW (before descriptor allocator)
6. IndirectDrawBuffer, ChunkRenderInfoBuffer
7. QuadIndexBuffer, Gigabuffer
8. Swapchain resources
9. Pipelines (translucent, wireframe, main, compute)
10. Descriptor allocator, layouts, pipeline layouts
11. Semaphores, command pools
```

### What This Story Does NOT Do

- Does NOT add per-face tinting (all faces of a block get the same tint)
- Does NOT add per-chunk palettes (global palette only, V1)
- Does NOT add dynamic biome tracking (palette set once at init)
- Does NOT modify chunk.vert (tintIndex already extracted and output)
- Does NOT modify cull.comp or cull_translucent.comp (no tint in culling)
- Does NOT modify MeshBuilder (tintIndex already packed into quads)
- Does NOT modify TintPalette class (already complete from Story 5.5)
- Does NOT add sky/block light integration (Epic 8)

### Previous Story Intelligence

**From Story 6.7 (Translucent Rendering — review):**
- `translucent.frag` was created in 6.7 with `fragTintIndex` declared but unused. This story adds the SSBO and tint multiplication.
- The translucent pipeline reuses `m_pipelineLayout` — adding binding 5 to the descriptor set layout automatically makes it available in the translucent pass.
- Both descriptor sets (opaque + translucent) share the same layout, so binding 5 must be written to BOTH sets.

**From Story 6.6 (G-Buffer — done):**
- `gbuffer.frag` was created in 6.6 with `fragTintIndex` declared but unused. Tint must be applied to `outAlbedoAO.rgb` BEFORE the G-Buffer write — the deferred lighting pass reads the already-tinted albedo, so no lighting shader changes needed.
- Key: tinting happens in the geometry pass, not the lighting pass. The lighting pass reads pre-tinted albedo from RT0.

**From Story 5.5 (Tint & Waving — done):**
- All CPU infrastructure created. Tests in `TestTintWaving.cpp` validate packing/unpacking, meshing integration, and palette colors.
- TintPalette biome colors are hardcoded: Plains grass=(0.55, 0.76, 0.38), Forest=(0.45, 0.68, 0.30), Desert=(0.75, 0.72, 0.42), etc.

### Git Intelligence

Recent commits follow `feat(renderer):` pattern:
```
c19d55e feat(renderer): GPU-driven translucent rendering pipeline and culling
3456090 feat(renderer): implement G-Buffer setup and deferred lighting pipeline
7e87f06 feat(renderer): add TextureArray support for block textures and integrate into Renderer
```

Suggested commit: `feat(renderer): wire biome tint palette to fragment shaders via SSBO`

### Project Structure Notes

```
engine/include/voxel/renderer/
  Renderer.h                      (MODIFY — add m_tintPaletteBuffer, m_tintPaletteMapped, updateTintPalette)
engine/src/renderer/
  Renderer.cpp                    (MODIFY — create buffer, extend descriptor layout + writes, init palette)
assets/shaders/
  gbuffer.frag                    (MODIFY — add SSBO binding 5, tint multiplication)
  translucent.frag                (MODIFY — add SSBO binding 5, tint multiplication)
  chunk.frag                      (MODIFY — add SSBO binding 5, tint multiplication)
assets/scripts/base/
  blocks.json                     (MODIFY — add tintIndex to grass, leaves, tall_grass, water)
```

No new files created. No test modifications needed (existing TestTintWaving.cpp already validates CPU-side).

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.8 acceptance criteria]
- [Source: engine/include/voxel/renderer/TintPalette.h — TintPalette class, MAX_ENTRIES=8, TINT_NONE/GRASS/FOLIAGE/WATER constants]
- [Source: engine/src/renderer/TintPalette.cpp — buildForBiome() with hardcoded biome colors for all 8 types]
- [Source: engine/include/voxel/world/Block.h:75 — BlockDefinition::tintIndex (uint8_t, default 0)]
- [Source: engine/include/voxel/renderer/ChunkMesh.h:59-61 — Quad bits [59:61] = tintIndex, packQuad/unpackTintIndex]
- [Source: engine/src/renderer/MeshBuilder.cpp — buildNaive/buildGreedy pass blockDef.tintIndex to packQuad]
- [Source: assets/shaders/chunk.vert:80 — bitfieldExtract(hi, 27, 3) for tintIndex, output at location 5]
- [Source: assets/shaders/gbuffer.frag:9 — fragTintIndex declared but unused]
- [Source: assets/shaders/translucent.frag:9 — fragTintIndex declared but unused]
- [Source: assets/shaders/chunk.frag:9 — fragTintIndex declared but unused]
- [Source: engine/src/renderer/Renderer.cpp:95-114 — Descriptor layout builder, bindings 0-4]
- [Source: engine/src/renderer/Renderer.cpp:299-367 — Descriptor writes pattern (array of VkWriteDescriptorSet)]
- [Source: engine/src/renderer/Renderer.cpp:369-430 — Translucent descriptor writes (m_transDescriptorSet)]
- [Source: engine/include/voxel/renderer/Renderer.h:39-45 — ChunkPushConstants (80 bytes, at capacity)]
- [Source: engine/include/voxel/world/BiomeTypes.h — BiomeType enum: Desert, Savanna, Plains, Forest, Jungle, Taiga, Tundra, IcePlains]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Memory & Ownership]
- [Source: _bmad-output/implementation-artifacts/6-7-transparent-translucent-rendering.md — Translucent pipeline reuses m_pipelineLayout]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
