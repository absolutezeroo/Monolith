# Story 6.5: Texture Array Loading

Status: in-progress

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want block textures loaded as a Vulkan texture array,
so that all block textures are available in a single bind and the fragment shader samples real block textures instead of placeholder face-normal colors.

## Acceptance Criteria

1. Load PNG textures (16x16 RGBA) from `assets/textures/blocks/` via stb_image into a single `VkImage` with `VK_IMAGE_VIEW_TYPE_2D_ARRAY` — one layer per texture.
2. Generate mipmaps (5 levels: 16, 8, 4, 2, 1) via `vkCmdBlitImage` during init.
3. Sampler: `VK_FILTER_NEAREST` mag (pixel-art), `VK_FILTER_LINEAR` min + `VK_SAMPLER_MIPMAP_MODE_LINEAR` (smooth at distance), `VK_SAMPLER_ADDRESS_MODE_REPEAT` (tiling for greedy-meshed quads).
4. `TextureArray` class: `static create(VulkanContext&, const std::string& directory) -> Result<unique_ptr<TextureArray>>`, `getLayerIndex(textureName) -> uint16_t`, `getLayerCount()`.
5. Block registry `textureIndices[6]` values correspond to layer indices in the texture array.
6. Descriptor set extended: add `COMBINED_IMAGE_SAMPLER` at binding 4 with `VK_SHADER_STAGE_FRAGMENT_BIT`.
7. MeshBuilder packs per-face texture index (from `BlockDefinition::textureIndices[face]`) into the quad data field currently holding `blockStateId`.
8. `chunk.vert` unpacks texture layer and outputs it to the fragment shader.
9. `chunk.frag` samples `sampler2DArray` with `vec3(uv, float(textureLayer))`, applies AO, replaces face-normal placeholder coloring.
10. Placeholder 16x16 PNGs created for all block types in `blocks.json` (36 textures, indices 0–35).
11. Zero Vulkan validation errors. Blocks render with correct textures at correct positions.

## Tasks / Subtasks

- [x] **Task 1: Create stb_image implementation translation unit** (AC: #1)
  - [x] Create `engine/src/renderer/StbImageImpl.cpp`:
    ```cpp
    #define STB_IMAGE_IMPLEMENTATION
    #include <stb_image.h>
    ```
  - [x] Follow existing pattern from `engine/src/renderer/StbImageWriteImpl.cpp`
  - [x] Verify `stb` is already in `vcpkg.json` (it is — provides both stb_image.h and stb_image_write.h)

- [x] **Task 2: Create placeholder textures** (AC: #10)
  - [x] Create directory `assets/textures/blocks/`
  - [x] Create `assets/textures/blocks/textures.json` manifest listing all textures in index order:
    ```json
    [
      "fallback",
      "stone",
      "dirt",
      "grass_top",
      "grass_side",
      "sand",
      "water_still",
      "oak_log_top",
      "oak_log_side",
      "oak_leaves",
      "glass",
      "glowstone",
      "torch",
      "bedrock",
      "sandstone",
      "snow",
      "birch_log_top",
      "birch_log_side",
      "birch_leaves",
      "spruce_log_top",
      "spruce_log_side",
      "spruce_leaves",
      "jungle_log_top",
      "jungle_log_side",
      "jungle_leaves",
      "cactus_top",
      "cactus_side",
      "tall_grass",
      "flower_red",
      "flower_yellow",
      "dead_bush",
      "snow_layer",
      "coal_ore",
      "iron_ore",
      "gold_ore",
      "diamond_ore"
    ]
    ```
  - [x] Generate 16x16 RGBA placeholder PNGs programmatically (small helper using stb_image_write, or committed as binary). Each texture should be a distinct solid color so blocks are visually distinguishable. Index 0 = magenta/black checkerboard (missing-texture marker).
  - [x] Texture filenames match manifest entries with `.png` extension (e.g., `stone.png`, `dirt.png`)

- [x] **Task 3: Add constants to RendererConstants.h** (AC: #1, #2)
  - [x] `BLOCK_TEXTURE_SIZE = 16` — pixel dimensions per texture
  - [x] `BLOCK_TEXTURE_MIP_LEVELS = 5` — log2(16) + 1 = 5
  - [x] `MAX_BLOCK_TEXTURES = 256` — max layers in texture array

- [x] **Task 4: Create TextureArray class** (AC: #1, #2, #3, #4)
  - [x] New files: `engine/include/voxel/renderer/TextureArray.h` / `engine/src/renderer/TextureArray.cpp`
  - [x] Follow RAII factory pattern from `QuadIndexBuffer` / `Gigabuffer`: private ctor, `static create()`, deleted copy/move
  - [x] Factory signature: `static Result<unique_ptr<TextureArray>> create(VulkanContext& ctx, const std::string& textureDir)`
  - [x] Members:
    - `VkImage m_image = VK_NULL_HANDLE`
    - `VmaAllocation m_allocation = VK_NULL_HANDLE`
    - `VkImageView m_imageView = VK_NULL_HANDLE`
    - `VkSampler m_sampler = VK_NULL_HANDLE`
    - `VmaAllocator m_allocator` (for RAII cleanup)
    - `VkDevice m_device` (for RAII cleanup)
    - `uint32_t m_layerCount = 0`
    - `std::unordered_map<std::string, uint16_t> m_nameToLayer`
  - [x] `create()` implementation:
    1. Load `textures.json` manifest from `textureDir` to get ordered list of texture names
    2. For each name, load `textureDir/<name>.png` via `stbi_load()` (force 4 channels = RGBA)
    3. Validate all textures are `BLOCK_TEXTURE_SIZE x BLOCK_TEXTURE_SIZE` — error if not
    4. If a PNG is missing, generate a magenta/black checkerboard fallback in memory
    5. Create staging buffer (temporary, one-time): `VMA_MEMORY_USAGE_CPU_ONLY`, size = `layerCount * 16 * 16 * 4`
    6. Copy all pixel data into staging buffer sequentially (layer 0 first, then layer 1, etc.)
    7. Create `VkImage`:
       - `imageType = VK_IMAGE_TYPE_2D`
       - `format = VK_FORMAT_R8G8B8A8_SRGB`
       - `extent = {16, 16, 1}`
       - `mipLevels = BLOCK_TEXTURE_MIP_LEVELS` (5)
       - `arrayLayers = layerCount`
       - `usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`
       - VMA: `VMA_MEMORY_USAGE_AUTO`, `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`
    8. Record one-time command buffer on graphics queue (need blit support):
       a. Transition mip 0, all layers: `UNDEFINED -> TRANSFER_DST_OPTIMAL`
       b. `vkCmdCopyBufferToImage`: staging -> image mip 0, all layers. Use one `VkBufferImageCopy` per layer (offset = `layerIndex * 16 * 16 * 4`)
       c. Generate mipmaps: for each mip level 1-4:
          - Transition mip N-1: `TRANSFER_DST -> TRANSFER_SRC`
          - `vkCmdBlitImage` from mip N-1 to mip N (all layers), FILTER_LINEAR
          - Resulting layout of mip N: `TRANSFER_DST_OPTIMAL`
       d. Transition all mip levels to `SHADER_READ_ONLY_OPTIMAL`:
          - Mip 0 through mip 3: already `TRANSFER_SRC` -> `SHADER_READ_ONLY`
          - Mip 4 (last): `TRANSFER_DST` -> `SHADER_READ_ONLY`
    9. Submit command buffer, wait for fence, destroy temporary staging + command buffer
    10. Create `VkImageView`:
        - `viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY`
        - `format = VK_FORMAT_R8G8B8A8_SRGB`
        - `subresourceRange.levelCount = BLOCK_TEXTURE_MIP_LEVELS`
        - `subresourceRange.layerCount = layerCount`
    11. Create `VkSampler`:
        - `magFilter = VK_FILTER_NEAREST` (pixel-art up close)
        - `minFilter = VK_FILTER_LINEAR` (smooth at distance)
        - `mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR`
        - `addressModeU/V/W = VK_SAMPLER_ADDRESS_MODE_REPEAT` (tiling for greedy-meshed quads)
        - `maxLod = float(BLOCK_TEXTURE_MIP_LEVELS - 1)`
        - `anisotropyEnable = VK_FALSE` (can enable later)
    12. Populate `m_nameToLayer` map from manifest order
  - [x] Accessors: `getImageView()`, `getSampler()`, `getLayerIndex(name) -> uint16_t`, `getLayerCount()`
  - [x] RAII destructor: destroy sampler, image view, image (via vmaDestroyImage)

- [x] **Task 5: Update descriptor layout and write texture binding** (AC: #6)
  - [x] In `Renderer::init()`, extend the `DescriptorLayoutBuilder` chain to add:
    ```cpp
    .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
    ```
  - [x] After texture array creation, write descriptor binding 4:
    ```cpp
    VkDescriptorImageInfo textureInfo{};
    textureInfo.sampler = m_textureArray->getSampler();
    textureInfo.imageView = m_textureArray->getImageView();
    textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = m_chunkDescriptorSet;
    textureWrite.dstBinding = 4;
    textureWrite.descriptorCount = 1;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = &textureInfo;
    ```
  - [x] DescriptorAllocator pool already includes `COMBINED_IMAGE_SAMPLER` — no changes needed (verified in `DescriptorAllocator.cpp:166`)

- [x] **Task 6: Wire TextureArray into Renderer** (AC: #6, #11)
  - [x] Add member: `std::unique_ptr<TextureArray> m_textureArray`
  - [x] In `init()`, create TextureArray after Gigabuffer, before descriptor layout build
  - [x] Pass texture directory path (derive from shaderDir or add as parameter)
  - [x] Add accessor: `const TextureArray* getTextureArray() const`
  - [x] In `shutdown()`, destroy TextureArray in correct order: ImGuiBackend -> StagingBuffer -> TextureArray -> QuadIndexBuffer -> Gigabuffer -> DescriptorAllocator -> pipelines

- [x] **Task 7: Update MeshBuilder to pack per-face texture index** (AC: #7)
  - [x] Find where the mesher packs the 16-bit `blockStateId` field into quad data
  - [x] The current packing: bits 30-31 of lo word + bits 0-13 of hi word = 16-bit blockStateId
  - [x] Change to pack `blockDef.textureIndices[faceIndex]` instead of the raw block numeric ID
  - [x] `faceIndex` is 0-5 corresponding to +X,-X,+Y,-Y,+Z,-Z (same as `BlockFace` enum order)
  - [x] Ensure the mesher has access to BlockRegistry to look up BlockDefinition for each block
  - [x] Greedy meshing merge check: adjacent quads should only merge if they share the same texture index for that face (this should naturally work since same-type blocks have same per-face texture)

- [x] **Task 8: Update chunk.vert** (AC: #8)
  - [x] Rename output: `fragBlockStateId` -> `fragTextureLayer` (location 4)
  - [x] The unpacking logic stays the same — `blockStateId = bsLow | (bsHigh << 2u)` — but now represents a texture layer index
  - [x] Rename internal variable: `blockStateId` -> `textureLayer`
  - [x] Output: `fragTextureLayer = textureLayer;`

- [x] **Task 9: Update chunk.frag** (AC: #9, #11)
  - [x] Add texture array binding:
    ```glsl
    layout(set = 0, binding = 4) uniform sampler2DArray blockTextures;
    ```
  - [x] Rename input: `fragBlockStateId` -> `fragTextureLayer` (location 4)
  - [x] Replace face-normal placeholder coloring with texture sampling:
    ```glsl
    void main()
    {
        // Sample block texture from array
        vec4 texColor = texture(blockTextures, vec3(fragUV, float(fragTextureLayer)));

        // Apply ambient occlusion
        vec3 color = texColor.rgb * fragAO;

        outColor = vec4(color, texColor.a);
    }
    ```
  - [x] Remove the face-normal coloring block and its `// textures come in Story 6.5` comment

- [x] **Task 10: Compile shaders and validate** (AC: #11)
  - [x] Recompile `chunk.vert` -> `chunk.vert.spv`
  - [x] Recompile `chunk.frag` -> `chunk.frag.spv`
  - [x] Build with `/W4 /WX` — zero warnings
  - [x] Run with Vulkan validation layers — zero errors
  - [x] Visual validation: blocks render with correct textures, grass has green top / dirt bottom / textured sides, logs have bark sides / cut ends

## Dev Notes

### Architecture Compliance

- **RAII pattern**: TextureArray follows QuadIndexBuffer/Gigabuffer exactly — private ctor, `static create()` factory returning `Result<unique_ptr<T>>`, deleted copy/move. [Source: architecture.md#Memory & Ownership]
- **Error handling**: `Result<T>` for factory. Failed texture loads produce a fallback checkerboard, not a fatal error. Missing manifest = fatal. [Source: project-context.md#Error Handling]
- **Naming**: PascalCase class `TextureArray`, camelCase methods `getLayerIndex()`, `m_` prefix members. [Source: CLAUDE.md#Naming Conventions]
- **One class per file**: TextureArray.h / TextureArray.cpp. [Source: CLAUDE.md#Critical Rules]
- **File location**: `engine/include/voxel/renderer/TextureArray.h` + `engine/src/renderer/TextureArray.cpp` (mirror pattern). [Source: architecture.md#Project Tree]

### Sampler REPEAT Mode — Corrects Epic Spec

The epic specifies `CLAMP_TO_EDGE` but this is **wrong** for greedy-meshed quads. A merged 3-block-wide quad has UVs `(0,0)` to `(3,height)`. With CLAMP_TO_EDGE, the texture stretches at the edges. With REPEAT, it tiles 3 times — the correct behavior for block textures. Use `VK_SAMPLER_ADDRESS_MODE_REPEAT`.

### Texture Array Format — sRGB

Use `VK_FORMAT_R8G8B8A8_SRGB` — block textures are authored in sRGB color space. The GPU automatically converts to linear during sampling, which is required for correct lighting math. Do NOT use `VK_FORMAT_R8G8B8A8_UNORM` (would skip gamma correction, making textures look washed out).

### One-Time Upload Pattern

Texture loading is a one-time init operation. Do NOT use the existing `StagingBuffer` (it's designed for per-frame buffer-to-buffer transfers). Instead:

1. Allocate a temporary staging VkBuffer via VMA (`VMA_MEMORY_USAGE_CPU_ONLY`)
2. `memcpy` all texture data into it
3. Allocate a one-time `VkCommandBuffer` from a transient command pool (or reuse an existing pool)
4. Record transitions + copies + blit chain
5. Submit to **graphics queue** (blits require graphics capability — transfer queue won't work)
6. Wait on fence
7. Destroy staging buffer and command buffer

This avoids modifying the StagingBuffer class. Reference pattern: the screenshot capture in `Renderer.cpp` does similar one-time image operations.

### Mipmap Generation via Blit Chain

For each mip level N from 1 to 4:
1. Transition mip N-1 from TRANSFER_DST to TRANSFER_SRC (all layers)
2. `vkCmdBlitImage` from mip N-1 to mip N with `VK_FILTER_LINEAR` (all layers in one blit — set `layerCount = totalLayers` in `VkImageSubresourceLayers`)
3. After all blits, transition all mip levels to `SHADER_READ_ONLY_OPTIMAL`

```
Mip 0 (16x16): buffer copy → TRANSFER_DST → TRANSFER_SRC → ... → SHADER_READ_ONLY
Mip 1 (8x8):   blit from 0 → TRANSFER_DST → TRANSFER_SRC → ... → SHADER_READ_ONLY
Mip 2 (4x4):   blit from 1 → TRANSFER_DST → TRANSFER_SRC → ... → SHADER_READ_ONLY
Mip 3 (2x2):   blit from 2 → TRANSFER_DST → TRANSFER_SRC → ... → SHADER_READ_ONLY
Mip 4 (1x1):   blit from 3 → TRANSFER_DST → SHADER_READ_ONLY
```

Use `VkImageMemoryBarrier2` with synchronization2 (Vulkan 1.3 core, already used in codebase).

### Quad Data Bit Layout — Actual vs Architecture Doc

The architecture doc says bits 30-37 (8 bits) for "block type / texture index", but the **actual implementation** uses 16 bits spanning both uint32 words:

```
lo word bits [30:31] = bsLow (2 bits)
hi word bits [0:13]  = bsHigh (14 bits)
Combined: blockStateId = bsLow | (bsHigh << 2) → 16 bits (0–65535)
```

The mesher currently packs the block's numeric ID here. This story changes it to pack `BlockDefinition::textureIndices[face]` instead. The 16-bit field is more than enough for texture indices (blocks.json max = 35).

### Greedy Mesher Merge Compatibility

Adjacent quads merge only if they have the same value in this field. Previously: same block type → merge. Now: same texture index for this face → merge. This is actually MORE correct — two different block types with the same face texture would merge, reducing quad count.

Ensure the merge key check in the binary greedy mesher compares the per-face texture index, not the raw block ID.

### Descriptor Binding Map (After This Story)

| Binding | Type | Stage | Buffer/Image | Purpose |
|---------|------|-------|-------------|---------|
| 0 | STORAGE_BUFFER | VERTEX | Gigabuffer | Quad data (vertex pulling) |
| 1 | STORAGE_BUFFER | VERTEX \| COMPUTE | ChunkRenderInfoBuffer | Per-chunk metadata |
| 2 | STORAGE_BUFFER | COMPUTE | IndirectDrawBuffer (commands) | Draw commands |
| 3 | STORAGE_BUFFER | COMPUTE | IndirectDrawBuffer (count) | Atomic draw count |
| 4 | COMBINED_IMAGE_SAMPLER | FRAGMENT | TextureArray | Block textures |

**Dependency**: Bindings 1–3 are added by Story 6.3. This story MUST be implemented after 6.3.

### Texture Index Mapping (From blocks.json)

| Index | Texture Name | Used By |
|-------|-------------|---------|
| 0 | fallback | Missing texture marker (magenta/black checkerboard) |
| 1 | stone | base:stone (all faces) |
| 2 | dirt | base:dirt (all), base:grass_block (bottom) |
| 3 | grass_top | base:grass_block (+Y) |
| 4 | grass_side | base:grass_block (sides) |
| 5 | sand | base:sand (all) |
| 6 | water_still | base:water (all) |
| 7 | oak_log_top | base:oak_log (+Y,-Y) |
| 8 | oak_log_side | base:oak_log (sides) |
| 9 | oak_leaves | base:oak_leaves (all) |
| 10 | glass | base:glass (all) |
| 11 | glowstone | base:glowstone (all) |
| 12 | torch | base:torch (all) |
| 13 | bedrock | base:bedrock (all) |
| 14 | sandstone | base:sandstone (all) |
| 15 | snow | base:snow_block (all) |
| 16 | birch_log_top | base:birch_log (+Y,-Y) |
| 17 | birch_log_side | base:birch_log (sides) |
| 18 | birch_leaves | base:birch_leaves (all) |
| 19 | spruce_log_top | base:spruce_log (+Y,-Y) |
| 20 | spruce_log_side | base:spruce_log (sides) |
| 21 | spruce_leaves | base:spruce_leaves (all) |
| 22 | jungle_log_top | base:jungle_log (+Y,-Y) |
| 23 | jungle_log_side | base:jungle_log (sides) |
| 24 | jungle_leaves | base:jungle_leaves (all) |
| 25 | cactus_top | base:cactus (+Y,-Y) |
| 26 | cactus_side | base:cactus (sides) |
| 27 | tall_grass | base:tall_grass (all) |
| 28 | flower_red | base:flower_red (all) |
| 29 | flower_yellow | base:flower_yellow (all) |
| 30 | dead_bush | base:dead_bush (all) |
| 31 | snow_layer | base:snow_layer (all) |
| 32 | coal_ore | base:coal_ore (all) |
| 33 | iron_ore | base:iron_ore (all) |
| 34 | gold_ore | base:gold_ore (all) |
| 35 | diamond_ore | base:diamond_ore (all) |

### What This Story Does NOT Do

- Does NOT implement deferred G-Buffer rendering (Story 6.6)
- Does NOT add alpha test/discard for cutout blocks (Story 6.7)
- Does NOT add translucent rendering pass (Story 6.7)
- Does NOT add biome-based tint palette (Story 6.8)
- Does NOT modify the compute culling shader (Story 6.4)
- Does NOT change the indirect draw path — texture array is orthogonal to draw submission

### Project Structure Notes

```
engine/include/voxel/renderer/
  TextureArray.h                    (NEW)
  RendererConstants.h               (MODIFY — add texture constants)
  Renderer.h                        (MODIFY — add TextureArray member + accessor)
engine/src/renderer/
  TextureArray.cpp                  (NEW)
  StbImageImpl.cpp                  (NEW — stb_image implementation unit)
  Renderer.cpp                      (MODIFY — create TextureArray, write descriptor, shutdown order)
  MeshBuilder.cpp                   (MODIFY — pack per-face texture index instead of block ID)
assets/shaders/
  chunk.vert                        (MODIFY — rename blockStateId → textureLayer)
  chunk.frag                        (MODIFY — add sampler2DArray, sample texture, remove placeholder)
assets/textures/blocks/
  textures.json                     (NEW — manifest)
  *.png                             (NEW — 36 placeholder textures)
```

### Previous Story Intelligence

**From Story 6.4 (Compute Culling — ready-for-dev):**
- `chunk.vert` will transition from push constant `chunkWorldPos` to SSBO read via `gl_InstanceIndex`. If 6.4 is implemented first, the push constants struct will change. The texture changes in this story are orthogonal (different shader outputs/bindings).
- Descriptor set is shared between compute and graphics pipelines. Adding binding 4 (FRAGMENT only) doesn't affect compute — compute pipeline only accesses bindings 0-3.

**From Story 6.3 (IndirectDrawBuffer — ready-for-dev):**
- Descriptor layout extended to bindings 0-3. This story adds binding 4.
- Descriptor write pattern established: batch `VkWriteDescriptorSet` array. Add texture image write to the batch.
- `ChunkRenderInfoBuffer` is HOST_VISIBLE, updated by `ChunkUploadManager`. Texture array is separate (DEVICE_LOCAL, loaded once).

**From Story 6.2 (Vertex Pulling — in progress):**
- Push constants: `ChunkPushConstants` is 80 bytes. Unchanged by this story.
- `fragBlockStateId` is output at location 4 but unused in fragment shader. This story repurposes it as `fragTextureLayer`.
- UVs: `vec2 fragUV` (location 2) already output with greedy mesh dimensions (0 to width/height). Ready for texture array tiling.

**From Story 6.0 (Descriptor Infrastructure):**
- `DescriptorLayoutBuilder` supports chained `.addBinding()` including `COMBINED_IMAGE_SAMPLER`.
- Pool creation already includes `COMBINED_IMAGE_SAMPLER` pool size (DescriptorAllocator.cpp:166). No changes needed.

### Git Intelligence

Recent commits follow `feat(renderer):` pattern:
```
5c50868 feat(renderer): add IndirectDrawBuffer and ChunkRenderInfoBuffer for GPU-driven rendering
140113a chore: finalize Story 6.0 and update shaders/render states
bd3e207 feat(renderer): implement GPU-driven chunk rendering via vertex pulling
4ff70e3 feat(renderer): add QuadIndexBuffer for shared GPU index data
```

Suggested commit: `feat(renderer): add texture array loading for block textures`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-06-gpu-driven-rendering.md — Story 6.5 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — Texture Arrays section, ADR-009 Gigabuffer, Shader Architecture]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Memory & Ownership]
- [Source: engine/include/voxel/renderer/Renderer.h — Current descriptor layout, member ownership, shutdown order]
- [Source: engine/include/voxel/renderer/RendererConstants.h — Existing constants to extend]
- [Source: engine/src/renderer/DescriptorAllocator.cpp:161-168 — Pool sizes already include COMBINED_IMAGE_SAMPLER]
- [Source: engine/src/renderer/StbImageWriteImpl.cpp — stb implementation file pattern]
- [Source: engine/include/voxel/world/Block.h:74 — textureIndices[6] field]
- [Source: assets/scripts/base/blocks.json — All texture index assignments (0-35)]
- [Source: assets/shaders/chunk.vert:49-70 — Current quad unpacking, 16-bit blockStateId extraction]
- [Source: assets/shaders/chunk.frag:14-36 — Current face-normal placeholder coloring to replace]
- [Source: _bmad-output/implementation-artifacts/6-3-indirect-draw-buffer-chunkrenderinfo-ssbo.md — Descriptor layout bindings 0-3]
- [Source: _bmad-output/implementation-artifacts/6-4-compute-culling-shader.md — Shared descriptor set, compute/graphics pipeline]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### Change Log
