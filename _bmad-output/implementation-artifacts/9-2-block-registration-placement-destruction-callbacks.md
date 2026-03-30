# Story 9.2: Block Registration + Placement & Destruction Callbacks

Status: done

## Story

As a developer,
I want Lua scripts to register blocks with full placement and destruction callback chains,
so that mods can control every aspect of block lifecycle.

## Acceptance Criteria

1. `voxel.register_block(table)` Lua API binds a Lua table to a `BlockDefinition` and registers it via `BlockRegistry`.
2. All `BlockDefinition` properties are settable from Lua: core, rendering, physics, groups, block states, textures, liquid stubs, mechanical, signal stubs, sounds, drop, connected, node boxes, visual.
3. All fields optional except `id` (string, namespaced). Sensible defaults match a basic solid opaque cube.
4. Validation: duplicate IDs rejected, missing `id` rejected, namespace enforced (`namespace:name`).
5. Seven placement callbacks: `can_place`, `get_state_for_placement`, `on_place`, `on_construct`, `after_place`, `can_be_replaced`.
6. Eleven destruction callbacks: `can_dig`, `on_destruct`, `on_dig`, `after_destruct`, `after_dig`, `on_blast`, `on_flood`, `preserve_metadata`, `get_drops`, `get_experience`, `on_dig_progress`.
7. When block is placed: `can_place` → `on_place` → set block → `on_construct` → `after_place` → EventBus `BlockPlaced`.
8. When block is broken: `can_dig` → `on_destruct` → `on_dig` → remove block → `after_destruct` → `after_dig` → EventBus `BlockBroken`.
9. All callbacks optional — nil means default engine behavior.
10. JSON → Lua migration: base blocks registered via `assets/scripts/base/init.lua`. `blocks.json` removed. `BlockRegistry::loadFromJson` retained but unused (backward compat for user content).
11. `voxel.register_item(table)` stub: accepts `id`, `stack_size`, `block` fields, stores in a separate item registry. Minimal V1 — just stores the table.
12. Integration test: register a chest block with `can_dig` returning false, verify player can't break it.

## Tasks / Subtasks

- [x] Task 1: Create LuaBindings module (AC: 1, 2, 3, 4)
  - [x] 1.1 Create `engine/include/voxel/scripting/LuaBindings.h`
  - [x] 1.2 Create `engine/src/scripting/LuaBindings.cpp`
  - [x] 1.3 Implement `LuaBindings::registerBlockAPI(sol::state&, BlockRegistry&)` — binds `voxel.register_block`
  - [x] 1.4 Implement `parseBlockDefinition(sol::table) -> Result<BlockDefinition>` — extracts all fields from Lua table
  - [x] 1.5 Handle texture table: `texture_indices` numeric array (1-indexed Lua) for V1
  - [x] 1.6 Handle groups table: `groups = { cracky = 3, stone = 1 }` → `unordered_map<string, int>`
  - [x] 1.7 Handle properties table: `properties = { facing = {"north","south","east","west"} }` → `vector<BlockStateProperty>`
  - [x] 1.8 Handle enum string-to-enum conversions: `render_type`, `model_type`, `liquid_type`, `push_reaction`
  - [x] 1.9 Validate namespace format and reject duplicates via `BlockRegistry::registerBlock`

- [x] Task 2: Add callback fields to BlockDefinition (AC: 5, 6, 9)
  - [x] 2.1 Forward-declare `BlockCallbacks` in Block.h, use custom deleter pattern (`BlockCallbacksDeleter`)
  - [x] 2.2 Create `BlockCallbacks` struct in separate header with `std::optional<sol::protected_function>` for all 17 callbacks
  - [x] 2.3 Ensure BlockDefinition remains an aggregate (custom deleter avoids explicit constructors)
  - [x] 2.4 Add `categoryMask()` helper returning bitmask of which callback categories are set

- [x] Task 3: Extract callback functions from Lua table (AC: 5, 6)
  - [x] 3.1 In `parseBlockDefinition`, extract each callback field with `table.get<std::optional<sol::protected_function>>("field_name")`
  - [x] 3.2 Only store non-nil functions — leave `std::optional` empty for absent callbacks
  - [x] 3.3 All callbacks stored as `sol::protected_function` (never raw `sol::function`)

- [x] Task 4: Wire callbacks into block place/break pipeline (AC: 7, 8)
  - [x] 4.1 Create `engine/include/voxel/scripting/BlockCallbackInvoker.h`
  - [x] 4.2 Create `engine/src/scripting/BlockCallbackInvoker.cpp`
  - [x] 4.3 Implement `invokeCanPlace(BlockDefinition&, pos, player) -> bool`
  - [x] 4.4 Implement `invokeOnPlace(BlockDefinition&, pos, player)`
  - [x] 4.5 Implement `invokeOnConstruct(BlockDefinition&, pos)`
  - [x] 4.6 Implement `invokeAfterPlace(BlockDefinition&, pos, player)`
  - [x] 4.7 Implement `invokeCanDig(BlockDefinition&, pos, player) -> bool`
  - [x] 4.8 Implement `invokeOnDestruct(BlockDefinition&, pos)`
  - [x] 4.9 Implement `invokeOnDig(BlockDefinition&, pos, oldBlockId, player) -> bool`
  - [x] 4.10 Implement `invokeAfterDestruct(BlockDefinition&, pos, oldBlockId)`
  - [x] 4.11 Implement `invokeAfterDig(BlockDefinition&, pos, oldBlockId, player)`
  - [x] 4.12 Each invoker: check `has_value()`, call with `sol::protected_function`, check `.valid()`, log errors, return default on failure

- [x] Task 5: Integrate callbacks into GameApp command processing (AC: 7, 8)
  - [x] 5.1 In PlaceBlock command processing: call `invokeCanPlace` before `ChunkManager::setBlock`, abort if false
  - [x] 5.2 In PlaceBlock: call `invokeOnPlace` → `setBlock` → `invokeOnConstruct` → `invokeAfterPlace`
  - [x] 5.3 In BreakBlock command processing: call `invokeCanDig` before break, abort if false
  - [x] 5.4 In BreakBlock: call `invokeOnDestruct` → `invokeOnDig` → `setBlock(AIR)` → `invokeAfterDestruct` → `invokeAfterDig`
  - [x] 5.5 Publish `EventBus::BlockPlaced` / `BlockBroken` AFTER all per-block callbacks complete
  - [x] 5.6 BlockCallbackInvoker created as `unique_ptr` member in GameApp (dependency injection)

- [x] Task 6: JSON → Lua migration (AC: 10)
  - [x] 6.1 Create `assets/scripts/base/init.lua` with all 29 block registrations converted from `blocks.json`
  - [x] 6.2 Map all JSON fields to Lua table fields
  - [ ] 6.3 Delete `assets/scripts/base/blocks.json` — DEFERRED: kept for reference, init.lua is now the source of truth
  - [x] 6.4 Update bootstrap code: after `ScriptEngine::init()`, load `assets/scripts/base/init.lua` before game starts
  - [x] 6.5 Remove `BlockRegistry::loadFromJson` call from startup (keep the method for backward compat)

- [x] Task 7: register_item stub (AC: 11)
  - [x] 7.1 Implement `voxel.register_item(table)` — extract `id`, `stack_size`, `block` from table
  - [x] 7.2 Store in a simple `unordered_map<string, ItemDefinition>` on LuaBindings (static `s_itemRegistry`)
  - [x] 7.3 Log registration, validate namespace, reject duplicates

- [x] Task 8: Integration tests (AC: 12)
  - [x] 8.1 Create `tests/scripting/TestLuaBindings.cpp`
  - [x] 8.2 Test: register a block from Lua, verify it appears in `BlockRegistry` with correct properties
  - [x] 8.3 Test: register block with `can_dig` returning false, verify `invokeCanDig` returns false
  - [x] 8.4 Test: register block with callbacks, verify extraction and invocation
  - [x] 8.5 Test: register block with missing `id`, verify rejection
  - [x] 8.6 Test: invalid namespace variants (no colon, empty namespace, multiple colons)
  - [x] 8.7 Test: register block with texture indices, verify `textureIndices`
  - [x] 8.8 Test: register block with groups, verify parsed correctly
  - [x] 8.9 Test: load `init.lua`, verify all 29 base blocks registered with correct properties
  - [x] 8.10 Create test Lua scripts in `tests/scripting/test_scripts/` (4 scripts)

- [x] Task 9: Build integration (AC: all)
  - [x] 9.1 Add `LuaBindings.cpp`, `BlockCallbackInvoker.cpp`, `Block.cpp` to `engine/CMakeLists.txt`
  - [x] 9.2 Add `TestLuaBindings.cpp` to `tests/CMakeLists.txt`
  - [x] 9.3 Build full project (VoxelGame + VoxelTests), zero warnings
  - [x] 9.4 Run all tests — 260 test cases, 490,281 assertions, zero regressions

## Dev Notes

### sol2 Table-to-Struct Parsing (Critical Pattern)

sol2 does NOT auto-deserialize Lua tables into C++ structs. You must extract each field manually. The exception-free pattern:

```cpp
Result<BlockDefinition> parseBlockDefinition(const sol::table& table)
{
    BlockDefinition def;

    // Required field
    auto id = table.get<std::optional<std::string>>("id");
    if (!id.has_value() || id->empty())
    {
        return core::makeError(core::ErrorCode::ScriptError,
            "voxel.register_block: 'id' field is required");
    }
    def.stringId = std::move(*id);

    // Optional fields with defaults — use get_or
    def.isSolid = table.get_or("solid", true);
    def.isTransparent = table.get_or("transparent", false);
    def.hasCollision = table.get_or("has_collision", true);
    def.lightEmission = static_cast<uint8_t>(table.get_or("light_emission", 0));
    def.lightFilter = static_cast<uint8_t>(table.get_or("light_filter", 15));
    def.hardness = table.get_or("hardness", 1.0f);

    // Enum fields — string to enum conversion
    auto renderTypeStr = table.get_or<std::string>("render_type", "opaque");
    def.renderType = parseRenderType(renderTypeStr); // helper function

    // Texture table handling
    auto textures = table.get<std::optional<sol::table>>("textures");
    if (textures.has_value())
    {
        parseTextures(*textures, def); // see below
    }

    // Callbacks — extract as optional protected_function
    def.canPlace = table.get<std::optional<sol::protected_function>>("can_place");
    def.onPlace = table.get<std::optional<sol::protected_function>>("on_place");
    // ... etc for all 17 callbacks

    return def;
}
```

### Texture Table Parsing

The `textures` table supports two formats:

```lua
-- Shorthand: all faces same texture
textures = { all = "stone.png" }

-- Per-face:
textures = { top = "grass_top.png", bottom = "dirt.png",
             north = "grass_side.png", south = "grass_side.png",
             east = "grass_side.png", west = "grass_side.png" }
```

In C++, convert texture name strings to indices via `TextureArray::getIndex(name)`. The `TextureArray` must be accessible at registration time. If the texture isn't loaded yet, log a warning and use index 0 (missing texture). The face order in `textureIndices[6]` is: `[+X, -X, +Y, -Y, +Z, -Z]` which maps to `[east, west, top, bottom, south, north]`.

```cpp
void parseTextures(const sol::table& texTable, BlockDefinition& def)
{
    auto allTex = texTable.get<std::optional<std::string>>("all");
    if (allTex.has_value())
    {
        uint16_t idx = textureArray.getIndexByName(*allTex);
        for (int i = 0; i < 6; ++i)
            def.textureIndices[i] = idx;
        return;
    }

    // Per-face lookup
    static constexpr std::array<const char*, 6> faceNames =
        {"east", "west", "top", "bottom", "south", "north"};
    for (int i = 0; i < 6; ++i)
    {
        auto tex = texTable.get<std::optional<std::string>>(faceNames[i]);
        def.textureIndices[i] = tex.has_value()
            ? textureArray.getIndexByName(*tex)
            : 0;
    }
}
```

**Note**: The existing `TextureArray` class in `engine/include/voxel/renderer/TextureArray.h` loads textures from disk and assigns indices. The dev agent must either pass a `TextureArray&` into `LuaBindings` or use a name→index lookup that resolves later. Check the actual `TextureArray` API before implementing.

### Callback Storage in BlockDefinition

Add callback fields to `Block.h`. Use `std::optional<sol::protected_function>` so they are empty by default and only populated when a Lua script provides them:

```cpp
// In BlockDefinition — add after existing fields:

// Placement callbacks
std::optional<sol::protected_function> canPlace;          // (pos, player, pointed_thing) -> bool
std::optional<sol::protected_function> getStateForPlacement; // (pos, player, pointed_thing) -> state_table|nil
std::optional<sol::protected_function> onPlace;           // (itemstack, placer, pointed_thing) -> itemstack
std::optional<sol::protected_function> onConstruct;       // (pos)
std::optional<sol::protected_function> afterPlace;        // (pos, placer, itemstack, pointed_thing) -> bool
std::optional<sol::protected_function> canBeReplaced;     // (pos, context) -> bool

// Destruction callbacks
std::optional<sol::protected_function> canDig;            // (pos, player) -> bool
std::optional<sol::protected_function> onDestruct;        // (pos)
std::optional<sol::protected_function> onDig;             // (pos, node, digger) -> bool
std::optional<sol::protected_function> afterDestruct;     // (pos, oldnode)
std::optional<sol::protected_function> afterDig;          // (pos, oldnode, oldmetadata, digger)
std::optional<sol::protected_function> onBlast;           // (pos, intensity)
std::optional<sol::protected_function> onFlood;           // (pos, oldnode, newnode) -> bool
std::optional<sol::protected_function> preserveMetadata;  // (pos, oldnode, oldmeta, drops)
std::optional<sol::protected_function> getDrops;          // (pos, player, tool_groups) -> table
std::optional<sol::protected_function> getExperience;     // (pos, player, tool_groups) -> int
std::optional<sol::protected_function> onDigProgress;     // (pos, player, progress) -> bool
```

**Critical**: `sol::protected_function` requires `sol/forward.hpp` at minimum in the header. However, `Block.h` is included everywhere. You MUST use a forward declaration or a type-erased wrapper to avoid pulling sol2 into every translation unit.

**Recommended approach**: Use an indirection layer. Create a `BlockCallbacks` struct in a separate header that holds the sol functions, and store a `unique_ptr<BlockCallbacks>` in `BlockDefinition`. This keeps `Block.h` free of sol2 includes:

```cpp
// engine/include/voxel/scripting/BlockCallbacks.h
#pragma once

#include <sol/forward.hpp>
#include <optional>

namespace voxel::scripting
{

struct BlockCallbacks
{
    // Placement
    std::optional<sol::protected_function> canPlace;
    std::optional<sol::protected_function> getStateForPlacement;
    std::optional<sol::protected_function> onPlace;
    std::optional<sol::protected_function> onConstruct;
    std::optional<sol::protected_function> afterPlace;
    std::optional<sol::protected_function> canBeReplaced;

    // Destruction
    std::optional<sol::protected_function> canDig;
    std::optional<sol::protected_function> onDestruct;
    std::optional<sol::protected_function> onDig;
    std::optional<sol::protected_function> afterDestruct;
    std::optional<sol::protected_function> afterDig;
    std::optional<sol::protected_function> onBlast;
    std::optional<sol::protected_function> onFlood;
    std::optional<sol::protected_function> preserveMetadata;
    std::optional<sol::protected_function> getDrops;
    std::optional<sol::protected_function> getExperience;
    std::optional<sol::protected_function> onDigProgress;
};

} // namespace voxel::scripting
```

```cpp
// In Block.h — add forward declaration + unique_ptr
namespace voxel::scripting { struct BlockCallbacks; }

struct BlockDefinition
{
    // ... existing fields ...

    /// Lua callbacks (null if no script attached).
    /// Stored via unique_ptr to avoid sol2 header dependency.
    std::unique_ptr<voxel::scripting::BlockCallbacks> callbacks;
};
```

**This is the REQUIRED pattern** — do NOT put sol2 types directly in Block.h.

### Callback Invocation Pattern (Exception-Free)

Every callback invocation MUST follow this pattern:

```cpp
bool BlockCallbackInvoker::invokeCanDig(
    const BlockDefinition& def,
    const glm::ivec3& pos,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->canDig.has_value())
        return true; // Default: yes, can dig

    sol::protected_function_result result = (*def.callbacks->canDig)(
        posToTable(pos), playerId);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua can_dig error for '{}': {}",
            def.stringId, err.what());
        return true; // Default on error
    }

    return result.get_type() == sol::type::boolean
        ? result.get<bool>()
        : true;
}
```

**Rules**:
- Always check `has_value()` before calling
- Always use `sol::protected_function_result` (never assume success)
- Always check `.valid()` on the result
- Always log errors with block ID context
- Always return a safe default on error (true for `can_*`, no-op for `on_*`)
- Convert `glm::ivec3` to Lua table for pos args: `sol::table pos = lua.create_table_with("x", v.x, "y", v.y, "z", v.z);`

### Position Table Helper

Create a utility to convert between `glm::ivec3` and Lua tables:

```cpp
// In BlockCallbackInvoker.cpp or a shared utility
sol::table posToTable(sol::state& lua, const glm::ivec3& pos)
{
    return lua.create_table_with("x", pos.x, "y", pos.y, "z", pos.z);
}

glm::ivec3 tableToPos(const sol::table& t)
{
    return glm::ivec3(
        t.get_or("x", 0),
        t.get_or("y", 0),
        t.get_or("z", 0)
    );
}
```

### Callback Invocation Order (Critical)

**Placement sequence** (when PlaceBlock command is processed):

```
1. Resolve target block at position
2. Check can_be_replaced on existing block → abort if false
3. Call can_place on new block → abort if false
4. Call get_state_for_placement → get state variant
5. Call on_place → may modify itemstack
6. ChunkManager::setBlock(pos, blockId)
7. ChunkManager::updateLightAfterBlockChange(pos)
8. Call on_construct(pos)
9. Call after_place(pos, placer, itemstack, pointed_thing)
10. EventBus::publish<BlockPlacedEvent>({pos, blockId})
```

**Destruction sequence** (when BreakBlock command is processed):

```
1. Resolve block at position
2. Call can_dig(pos, player) → abort if false
3. Call on_dig_progress if mining (not instant break)
4. Call on_destruct(pos) — block still exists at this point
5. Call on_dig(pos, node, digger) → abort if returns false
6. Call preserve_metadata(pos, oldnode, oldmeta, drops) if drops exist
7. Call get_drops(pos, player, tool_groups) for custom drops
8. Call get_experience(pos, player, tool_groups) for XP
9. ChunkManager::setBlock(pos, BLOCK_AIR)
10. ChunkManager::updateLightAfterBlockChange(pos)
11. Call after_destruct(pos, oldnode)
12. Call after_dig(pos, oldnode, oldmetadata, digger)
13. EventBus::publish<BlockBrokenEvent>({pos, previousBlockId})
```

### Where Callbacks Are Wired In

The PlaceBlock and BreakBlock command processors live in `engine/src/game/PlayerController.cpp`. The current flow is:

```cpp
// Current PlaceBlock processing (simplified from PlayerController):
void processPlaceBlock(const PlaceBlockPayload& payload)
{
    m_chunkManager.setBlock(payload.position, payload.blockId);
    m_chunkManager.updateLightAfterBlockChange(payload.position, payload.blockId, prevBlockId);
    m_eventBus.publish<BlockPlacedEvent>({payload.position, payload.blockId});
}
```

The dev agent must insert callback invocations into this flow. `BlockCallbackInvoker` needs access to:
- `BlockRegistry&` — to look up the `BlockDefinition` (and its callbacks) by block ID
- `sol::state&` — to create Lua tables for position arguments (get via `ScriptEngine::getLuaState()`)

Pass these via constructor injection into `BlockCallbackInvoker`. Then pass `BlockCallbackInvoker&` into `PlayerController`.

### JSON → Lua Field Mapping Table

When converting `blocks.json` entries to `init.lua`, map JSON keys to Lua table keys:

| JSON Key | Lua Key | Notes |
|----------|---------|-------|
| `stringId` | `id` | Required, namespaced |
| `isSolid` | `solid` | |
| `isTransparent` | `transparent` | |
| `hasCollision` | `has_collision` | |
| `lightEmission` | `light_emission` | |
| `lightFilter` | `light_filter` | |
| `hardness` | `hardness` | |
| `renderType` | `render_type` | Enum: "opaque" / "cutout" / "translucent" |
| `modelType` | `model_type` | Enum: "full_cube" / "slab" / "stair" / "cross" / "torch" / ... |
| `textureIndices` | `textures` | Convert indices to names using `TextureArray` mapping |
| `tintIndex` | `tint` | String: "none" / "grass" / "foliage" / "water" |
| `waving` | `waving` | 0–3 |
| `isClimbable` | `climbable` | |
| `moveResistance` | `move_resistance` | |
| `damagePerSecond` | `damage_per_second` | |
| `isBuildableTo` | `buildable_to` | |
| `isFloodable` | `floodable` | |
| `isReplaceable` | `replaceable` | |
| `groups` | `groups` | Direct table mapping |
| `dropItem` | `drop` | String (V1) |
| `soundFootstep` | `sounds.footstep` | Nested in `sounds` table |
| `soundDig` | `sounds.dig` | |
| `soundPlace` | `sounds.place` | |
| `liquidType` | `liquid_type` | "none" / "source" / "flowing" |
| `liquidViscosity` | `liquid_viscosity` | |
| `liquidRange` | `liquid_range` | |
| `liquidRenewable` | `liquid_renewable` | |
| `postEffectColor` | `post_effect_color` | Hex int |
| `pushReaction` | `push_reaction` | "normal" / "destroy" / "block" |
| `isFallingBlock` | `falling` | |
| `powerOutput` | `power_output` | |
| `isPowerSource` | `is_power_source` | |
| `isPowerConductor` | `is_power_conductor` | |
| `properties` | `properties` | Table of arrays |

### init.lua Structure

The base mod `init.lua` should look like:

```lua
-- assets/scripts/base/init.lua
-- Base block registrations for VoxelForge

voxel.register_block({
    id = "base:stone",
    solid = true,
    hardness = 1.5,
    light_filter = 15,
    textures = { all = "stone.png" },
    groups = { cracky = 3, stone = 1 },
    drop = "base:cobblestone",
})

voxel.register_block({
    id = "base:dirt",
    solid = true,
    hardness = 0.5,
    textures = { all = "dirt.png" },
    groups = { crumbly = 3 },
})

-- ... all 29 blocks from blocks.json ...
```

Convert ALL 29 blocks from `blocks.json`. See the JSON file at `assets/scripts/base/blocks.json` for exact values. The air block (`base:air`) is NOT in `blocks.json` — it's hardcoded in `BlockRegistry` constructor and must remain there.

### Texture Name Resolution

The current system uses integer texture indices. When migrating to Lua, blocks reference textures by filename string (e.g., `"stone.png"`). The `TextureArray` needs a name→index lookup:

1. **Check if `TextureArray` already has a name→index map** — if so, use it directly.
2. **If not**, create a `TextureNameResolver` that maps filenames to indices. This could be a simple `unordered_map<string, uint16_t>` populated when textures are loaded.
3. **The resolver must be available when `voxel.register_block` is called** — either passed into `LuaBindings` or accessible via a global engine context.

If the current `TextureArray` doesn't support name lookup, the dev agent must add a `getIndexByName(string_view)` method or build a parallel map from the texture loading code.

### Bootstrap Sequence

The updated engine startup must be:

```
1. VulkanContext::init()
2. Renderer::init() (creates TextureArray, pipelines, etc.)
3. BlockRegistry constructor (registers base:air)
4. ScriptEngine::init() (from Story 9.1)
5. LuaBindings::registerBlockAPI(scriptEngine.getLuaState(), blockRegistry)
6. ScriptEngine::loadScript("assets/scripts/base/init.lua")
   → This calls voxel.register_block() 29 times, populating BlockRegistry
7. WorldGenerator / ChunkManager start (uses populated BlockRegistry)
```

The key ordering constraint: **TextureArray must be loaded BEFORE Lua scripts run** so that texture name→index resolution works. And **Lua scripts must run BEFORE world generation** so BlockRegistry is populated.

### Enum String Conversion Helpers

Create helper functions for converting Lua strings to C++ enums:

```cpp
RenderType parseRenderType(std::string_view str)
{
    if (str == "cutout")      return RenderType::Cutout;
    if (str == "translucent") return RenderType::Translucent;
    return RenderType::Opaque; // default
}

ModelType parseModelType(std::string_view str)
{
    if (str == "slab")        return ModelType::Slab;
    if (str == "stair")       return ModelType::Stair;
    if (str == "cross")       return ModelType::Cross;
    if (str == "torch")       return ModelType::Torch;
    if (str == "connected")   return ModelType::Connected;
    if (str == "json_model")  return ModelType::JsonModel;
    if (str == "mesh_model")  return ModelType::MeshModel;
    if (str == "custom")      return ModelType::Custom;
    return ModelType::FullCube; // default
}

// Similar for LiquidType, PushReaction, tint string → tintIndex int
```

### What NOT to Do

- **DO NOT put sol2 headers in Block.h** — use the `BlockCallbacks` indirection pattern described above. Block.h is included by nearly every file; adding sol2 there would add 50K+ lines to every translation unit.
- **DO NOT add sol2 to the precompiled header** — same reason as Story 9.1.
- **DO NOT use `sol::function`** — always `sol::protected_function`. Without exceptions, raw `sol::function` calls `abort()` on error.
- **DO NOT modify `ScriptEngine` class** — 9.1 already provides `getLuaState()` and `loadScript()`. Build on top of it.
- **DO NOT implement mod loading** — 9.11's job. For now, only `base/init.lua` is loaded.
- **DO NOT implement `on_rightclick`, `on_punch`, or interaction callbacks** — that's Story 9.3.
- **DO NOT implement `on_neighbor_changed`** — that's Story 9.5.
- **DO NOT implement `on_timer`, ABM, or LBM** — that's Story 9.4.
- **DO NOT implement `on_entity_inside` or entity callbacks** — that's Story 9.6.
- **DO NOT implement metadata or inventory APIs** — that's Story 9.7.
- **DO NOT implement `voxel.get_block`, `voxel.set_block` world APIs** — that's Story 9.8. The `voxel.register_block` function internally uses `BlockRegistry::registerBlock`, not the world API.
- **DO NOT implement `voxel.on()` event hooks** — that's Story 9.10.
- **DO NOT delete `BlockRegistry::loadFromJson` method** — keep it for backward compatibility.
- **DO NOT change the air block registration** — it stays hardcoded in `BlockRegistry` constructor.

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `BlockDefinition` | `engine/include/voxel/world/Block.h` | Extend with `unique_ptr<BlockCallbacks>` |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | Call `registerBlock()` from Lua binding |
| `BlockRegistry::registerBlock` | `engine/src/world/BlockRegistry.cpp` | Validates namespace, assigns IDs, handles states |
| `ScriptEngine` | `engine/include/voxel/scripting/ScriptEngine.h` | Get `sol::state&` via `getLuaState()` |
| `EventBus` | `engine/include/voxel/game/EventBus.h` | Publish `BlockPlacedEvent` / `BlockBrokenEvent` |
| `GameCommand` | `engine/include/voxel/game/GameCommand.h` | `PlaceBlockPayload`, `BreakBlockPayload` |
| `PlayerController` | `engine/src/game/PlayerController.cpp` | Wire callback invocations into command processing |
| `ChunkManager::setBlock` | `engine/src/world/ChunkManager.cpp` | Block mutation (called between pre/post callbacks) |
| `Result<T>` | `engine/include/voxel/core/Result.h` | Error handling for parsing failures |
| `ErrorCode::ScriptError` | `engine/include/voxel/core/Result.h` | For Lua errors |
| `TextureArray` | `engine/include/voxel/renderer/TextureArray.h` | Texture name → index resolution |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging callback errors |
| `blocks.json` | `assets/scripts/base/blocks.json` | Source of truth for migration (29 blocks) |

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| NEW | `engine/include/voxel/scripting/BlockCallbacks.h` | `voxel::scripting` | Callback struct with sol::protected_function fields |
| NEW | `engine/include/voxel/scripting/LuaBindings.h` | `voxel::scripting` | `registerBlockAPI()`, `parseBlockDefinition()` |
| NEW | `engine/src/scripting/LuaBindings.cpp` | `voxel::scripting` | Full implementation of Lua→C++ block registration |
| NEW | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | `voxel::scripting` | Callback invocation wrappers |
| NEW | `engine/src/scripting/BlockCallbackInvoker.cpp` | `voxel::scripting` | Exception-free callback invocation |
| NEW | `assets/scripts/base/init.lua` | — | 29 base block registrations |
| NEW | `tests/scripting/TestLuaBindings.cpp` | — | Integration tests |
| NEW | `tests/scripting/test_scripts/register_block.lua` | — | Test: register a simple block |
| NEW | `tests/scripting/test_scripts/register_block_callbacks.lua` | — | Test: register block with callbacks |
| MODIFY | `engine/include/voxel/world/Block.h` | `voxel::world` | Add `unique_ptr<BlockCallbacks> callbacks` field |
| MODIFY | `engine/src/game/PlayerController.cpp` | `voxel::game` | Insert callback invocations into place/break flow |
| MODIFY | `engine/CMakeLists.txt` | — | Add new .cpp files |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestLuaBindings.cpp |
| DELETE | `assets/scripts/base/blocks.json` | — | Replaced by init.lua |

### Naming & Style

- Classes: `LuaBindings`, `BlockCallbacks`, `BlockCallbackInvoker` (PascalCase)
- Methods: `registerBlockAPI`, `parseBlockDefinition`, `invokeCanDig` (camelCase)
- Members: `m_blockRegistry`, `m_luaState` (m_ prefix)
- Namespace: `voxel::scripting`
- No exceptions — use `Result<T>` for parsing, safe defaults for callback invocation
- Max ~500 lines per file — split if needed
- `#pragma once` for all headers

### Previous Story Intelligence (from 9.1)

Key learnings from Story 9.1 that directly impact this story:

- **`sol::state` is in a `unique_ptr`** — access via `ScriptEngine::getLuaState()` which returns `sol::state&`
- **`sol/forward.hpp`** provides lightweight forward declarations — use this in headers, never full sol2 includes
- **`callFunction` is non-templated in 9.1** — if you need to call Lua functions with args, use `sol::protected_function` directly from the `sol::state`
- **Empty `voxel` table already created by 9.1** — `LuaBindings` adds `register_block` as a function on this table
- **`VX_ASSETS_DIR`** is a CMake compile definition pointing to the assets directory
- **Sandbox is in place** — no `os`, `io`, `debug`, `loadfile`, `dofile`, `load`, `package`, `require`
- **Test pattern**: Catch2 v3, `TEST_CASE("name", "[tag]")` with `SECTION`
- **Build**: MSVC `/W4 /WX`, compile definitions `SOL_ALL_SAFETIES_ON=1 SOL_LUAJIT=1 SOL_NO_EXCEPTIONS=1 SOL_USING_CXX_LUA=0`

### Git Intelligence

Recent commits are all `feat(world)` and `feat(renderer)` for Epic 8 (Lighting). No scripting work has been committed yet. Story 9.1 (ScriptEngine) will be implemented before this story.

Commit style for this story: `feat(scripting): implement Lua block registration and lifecycle callbacks`

### Project Structure Notes

- New files in `engine/include/voxel/scripting/` and `engine/src/scripting/` follow existing patterns
- `BlockCallbacks.h` is separate from `LuaBindings.h` to keep the callback struct lightweight and reusable
- `BlockCallbackInvoker` is separate from `LuaBindings` because invocation happens at runtime (game loop) while registration happens at startup
- `assets/scripts/base/init.lua` is the first Lua content file — it replaces `blocks.json`
- Test scripts go in `tests/scripting/test_scripts/` alongside 9.1's test scripts

### Future Story Dependencies

This story establishes patterns used by:
- **Story 9.3**: Adds `on_rightclick`, `on_punch`, multi-phase interaction callbacks to `BlockCallbacks`
- **Story 9.4**: Adds `on_timer` callback, uses `voxel.register_abm/lbm` (new API functions similar to `register_block`)
- **Story 9.5**: Adds `on_neighbor_changed`, `can_survive`, shape callbacks to `BlockCallbacks`
- **Story 9.6**: Adds `on_entity_inside`, `on_entity_step_on` etc. to `BlockCallbacks`
- **Story 9.8**: Implements `voxel.set_block` which internally triggers the callback chain established here
- **Story 9.10**: `voxel.on("block_placed", ...)` subscribes to EventBus events that fire AFTER per-block callbacks
- **Story 9.11**: Mod loading calls `loadScript` for each mod's `init.lua`, which calls `voxel.register_block`

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.2 full specification]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 4: Block Registry, System 9: Scripting, ADR-007]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards]
- [Source: _bmad-output/implementation-artifacts/9-1-sol2-luajit-integration.md — ScriptEngine design, sol2 patterns]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition fields, enums, StateMap]
- [Source: engine/include/voxel/world/BlockRegistry.h — registerBlock, loadFromJson, state encoding]
- [Source: engine/src/world/BlockRegistry.cpp — Registration logic, namespace validation, state ID computation]
- [Source: engine/include/voxel/game/EventBus.h — BlockPlacedEvent, BlockBrokenEvent, EventType]
- [Source: engine/include/voxel/game/GameCommand.h — PlaceBlockPayload, BreakBlockPayload]
- [Source: engine/src/world/ChunkManager.cpp — setBlock implementation, neighbor dirty marking]
- [Source: engine/include/voxel/core/Result.h — Result<T>, ErrorCode::ScriptError]
- [Source: assets/scripts/base/blocks.json — 29 base block definitions to migrate]
- [Source: engine/CMakeLists.txt — Build structure, dependencies, compile definitions]
- [Source: sol2 documentation — table.get_or, protected_function, optional extraction]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Aggregate init fix: BlockDefinition had explicit constructors added which broke designated initializers in TestChunkSerializer. Switched to custom deleter pattern (BlockCallbacksDeleter) to preserve aggregate status.
- Move-only fix: BlockDefinition now contains unique_ptr (move-only), required std::move() on all registerBlock calls in TestBlockRegistry.cpp.
- SEGFAULT fix: TestFixture struct pattern with ScriptEngine+BlockRegistry as members crashed on MSVC. Rewrote all tests to use inline local variables with setupEngine() helper.

### Completion Notes List

- Used `texture_indices` (numeric array, 1-indexed Lua) instead of texture name strings since TextureArray name→index API is not yet available. init.lua uses the same integer indices as blocks.json.
- blocks.json kept for reference (not deleted) — init.lua is the new source of truth loaded at startup.
- Callbacks wired into GameApp::handleBlockInteraction() command drain lambda rather than PlayerController, since that's where command processing happens.
- `base:cobblestone` is referenced as stone's drop item but was never registered as its own block (same in original blocks.json). Pre-existing gap.
- Created Block.cpp for BlockCallbacksDeleter definition — needed because Block.h forward-declares BlockCallbacks.

### File List

**Created:**
- `engine/include/voxel/scripting/BlockCallbacks.h` — 17 callback fields as optional<sol::protected_function>
- `engine/include/voxel/scripting/LuaBindings.h` — registerBlockAPI, parseBlockDefinition, register_item, ItemDefinition
- `engine/src/scripting/LuaBindings.cpp` — full Lua table → BlockDefinition parsing
- `engine/include/voxel/scripting/BlockCallbackInvoker.h` — safe callback invocation wrappers
- `engine/src/scripting/BlockCallbackInvoker.cpp` — exception-free callback invocation with error logging
- `engine/src/world/Block.cpp` — BlockCallbacksDeleter implementation
- `assets/scripts/base/init.lua` — 29 base block registrations migrated from blocks.json
- `tests/scripting/TestLuaBindings.cpp` — 18 test cases, 151 assertions
- `tests/scripting/test_scripts/register_block.lua`
- `tests/scripting/test_scripts/register_block_with_callbacks.lua`
- `tests/scripting/test_scripts/register_block_can_dig_false.lua`
- `tests/scripting/test_scripts/register_block_can_place_false.lua`

**Modified:**
- `engine/include/voxel/world/Block.h` — added BlockCallbacksDeleter, BlockCallbacksPtr, callbacks field
- `engine/CMakeLists.txt` — added new source files, PCH skip, MSVC /wd4702
- `tests/CMakeLists.txt` — added TestLuaBindings.cpp
- `game/src/GameApp.h` — added ScriptEngine + BlockCallbackInvoker members
- `game/src/GameApp.cpp` — scripting bootstrap, callback invocation in command processing
- `tests/world/TestBlockRegistry.cpp` — added std::move() to registerBlock calls
