# Story 3.3b: Expand BlockDefinition to Full Property Set

Status: done

## Story

As a developer,
I want BlockDefinition to contain all properties needed by future systems (rendering, physics, modding, audio, liquid, signals),
so that no structural changes to Block.h are needed when Epics 5–10 arrive.

## Acceptance Criteria

1. `BlockDefinition` expanded from 10 fields to 35+ fields per architecture spec, with enums `RenderType`, `ModelType`, `LiquidType`, `PushReaction`
2. All new fields have sensible defaults — existing code that creates `BlockDefinition` with only the original 10 fields still compiles and works identically
3. `blocks.json` updated: each block gains new fields where relevant (water gets liquid stubs, torch gets `isFloodable`, leaves get `waving`, glass gets `renderType: "cutout"`)
4. `BlockRegistry::loadFromJson()` parses all new fields with fallback to defaults for omitted fields
5. All existing tests pass unchanged; new tests cover enum parsing, groups map, stub fields, default values
6. `lightFilter` default changed from 15 to 0 (transparent by default — solid blocks override in JSON)

## Tasks / Subtasks

- [x] Task A: Expand Block.h with enums and fields (AC: 1, 2, 6)
  - [x] A.1 Add `RenderType` enum class: `Opaque, Cutout, Translucent`
  - [x] A.2 Add `ModelType` enum class: `FullCube, Slab, Stair, Cross, Torch, Connected, JsonModel, MeshModel, Custom`
  - [x] A.3 Add `LiquidType` enum class: `None, Source, Flowing`
  - [x] A.4 Add `PushReaction` enum class: `Normal, Destroy, Block`
  - [x] A.5 Add rendering fields: `renderType`, `modelType`, `tintIndex`, `waving`
  - [x] A.6 Add physics fields: `isClimbable`, `moveResistance`, `damagePerSecond`, `drowning`, `isBuildableTo`, `isFloodable`, `isReplaceable`
  - [x] A.7 Add groups: `std::unordered_map<std::string, int> groups`
  - [x] A.8 Add sound stubs: `soundFootstep`, `soundDig`, `soundPlace`
  - [x] A.9 Add liquid stubs: `liquidType`, `liquidViscosity`, `liquidRange`, `liquidRenewable`, `liquidAlternativeFlowing`, `liquidAlternativeSource`
  - [x] A.10 Add visual: `postEffectColor`
  - [x] A.11 Add mechanical: `pushReaction`, `isFallingBlock`
  - [x] A.12 Add signal stubs: `powerOutput`, `isPowerSource`, `isPowerConductor`
  - [x] A.13 Change `lightFilter` default from 15 to 0
  - [x] A.14 Verify: all existing code that constructs BlockDefinition still compiles (defaults cover all new fields)
- [x] Task B: Update JSON parser in BlockRegistry.cpp (AC: 4)
  - [x] B.1 Add enum string→value helpers: `parseRenderType(string)`, `parseModelType(string)`, `parseLiquidType(string)`, `parsePushReaction(string)`
  - [x] B.2 Parse all new fields from JSON with fallback to defaults for omitted fields
  - [x] B.3 Parse `groups` as JSON object `{"cracky": 3, "stone": 1}` → `std::unordered_map`
  - [x] B.4 Parse `sounds` as JSON object `{"footstep": "...", "dig": "...", "place": "..."}`
  - [x] B.5 Parse `liquid` as JSON object with all 6 sub-fields
  - [x] B.6 Verify: existing blocks.json still loads correctly with no changes (backward compatible)
- [x] Task C: Update blocks.json with new properties (AC: 3)
  - [x] C.1 Water: add `renderType: "translucent"`, `isReplaceable: true`, `liquidType: "source"`, `liquidViscosity: 1`, `drowning: 1`, `postEffectColor: "0x80000044"`, `moveResistance: 3`
  - [x] C.2 Glass: add `renderType: "cutout"`
  - [x] C.3 Oak leaves: add `renderType: "cutout"`, `waving: 1`, `isFloodable: true`, `groups: {"choppy": 3, "leafdecay": 3}`
  - [x] C.4 Torch: add `isFloodable: true`, `isBuildableTo: false`, `modelType: "torch"`, `groups: {"dig_immediate": 3}`
  - [x] C.5 Sand: add `isFallingBlock: true`, `groups: {"crumbly": 3, "falling_node": 1}`
  - [x] C.6 Stone: add `groups: {"cracky": 3, "stone": 1}`, `lightFilter: 15`
  - [x] C.7 Dirt: add `groups: {"crumbly": 3}`, `lightFilter: 15`
  - [x] C.8 Grass block: add `groups: {"crumbly": 3}`, `lightFilter: 15`
  - [x] C.9 Oak log: add `groups: {"choppy": 2, "wood": 1}`, `lightFilter: 15`
  - [x] C.10 Glowstone: add `groups: {"cracky": 3}`, `lightFilter: 15`
- [x] Task D: Add tests (AC: 5)
  - [x] D.1 Test: default-constructed BlockDefinition has `renderType == Opaque`, `modelType == FullCube`, `liquidType == None`, `pushReaction == Normal`
  - [x] D.2 Test: `lightFilter` default is 0 (not 15)
  - [x] D.3 Test: loadFromJson parses `renderType: "cutout"` → `RenderType::Cutout`
  - [x] D.4 Test: loadFromJson parses `groups: {"cracky": 3}` → map contains key "cracky" with value 3
  - [x] D.5 Test: loadFromJson with omitted fields uses defaults (backward compatibility)
  - [x] D.6 Test: water block from JSON has `liquidType == Source`, `drowning == 1`, `moveResistance == 3`
  - [x] D.7 Test: existing TestBlockRegistry tests still pass unchanged

## Dev Notes

### Current State of Block.h

```cpp
struct BlockDefinition
{
    std::string stringId;
    uint16_t numericId = 0;
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    uint8_t lightEmission = 0;
    uint8_t lightFilter = 15;     // ← BUG: should default to 0
    float hardness = 1.0f;
    uint16_t textureIndices[6] = {};
    std::string dropItem;
};
```

10 fields. Needs to become 35+. All new fields have defaults so **zero existing code breaks**.

### Target Block.h Structure

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

constexpr uint16_t BLOCK_AIR = 0;

enum class RenderType : uint8_t { Opaque, Cutout, Translucent };
enum class ModelType : uint8_t { FullCube, Slab, Stair, Cross, Torch, Connected, JsonModel, MeshModel, Custom };
enum class LiquidType : uint8_t { None, Source, Flowing };
enum class PushReaction : uint8_t { Normal, Destroy, Block };

struct BlockDefinition
{
    // --- Identity ---
    std::string stringId;
    uint16_t numericId = 0;

    // --- Core properties ---
    bool isSolid = true;
    bool isTransparent = false;
    bool hasCollision = true;
    float hardness = 1.0f;
    uint8_t lightEmission = 0;
    uint8_t lightFilter = 0;           // 0 = transparent to light. Solid blocks set 15 in JSON.

    // --- Rendering ---
    RenderType renderType = RenderType::Opaque;
    ModelType modelType = ModelType::FullCube;
    uint16_t textureIndices[6] = {};
    uint8_t tintIndex = 0;             // 0=none, 1=grass, 2=foliage, 3=water
    uint8_t waving = 0;                // 0=none, 1=leaves, 2=plants, 3=liquid surface

    // --- Physics / interaction ---
    bool isClimbable = false;
    uint8_t moveResistance = 0;        // 0–7
    uint32_t damagePerSecond = 0;
    uint8_t drowning = 0;
    bool isBuildableTo = false;
    bool isFloodable = false;
    bool isReplaceable = false;

    // --- Tool / mining groups ---
    std::unordered_map<std::string, int> groups;

    // --- Drop ---
    std::string dropItem;

    // --- Sound stubs ---
    std::string soundFootstep;
    std::string soundDig;
    std::string soundPlace;

    // --- Liquid stubs ---
    LiquidType liquidType = LiquidType::None;
    uint8_t liquidViscosity = 0;
    uint8_t liquidRange = 8;
    bool liquidRenewable = true;
    std::string liquidAlternativeFlowing;
    std::string liquidAlternativeSource;

    // --- Visual effects ---
    uint32_t postEffectColor = 0;      // ARGB overlay when camera is inside

    // --- Mechanical behavior ---
    PushReaction pushReaction = PushReaction::Normal;
    bool isFallingBlock = false;

    // --- Redstone/signal stubs ---
    uint8_t powerOutput = 0;
    bool isPowerSource = false;
    bool isPowerConductor = true;
};

} // namespace voxel::world
```

### JSON Parsing — New Fields Pattern

All new fields are parsed with the same pattern as existing ones — check existence + type, fallback to default:

```cpp
// In BlockRegistry::loadFromJson(), for each block object `obj`:

// Enum fields — use string→enum helpers
if (obj.contains("renderType") && obj["renderType"].is_string())
    def.renderType = parseRenderType(obj["renderType"].get<std::string>());

// Integer fields
if (obj.contains("moveResistance") && obj["moveResistance"].is_number_integer())
    def.moveResistance = obj["moveResistance"].get<uint8_t>();

// Groups — parse as object of string→int
if (obj.contains("groups") && obj["groups"].is_object()) {
    for (auto& [key, val] : obj["groups"].items()) {
        if (val.is_number_integer())
            def.groups[key] = val.get<int>();
    }
}

// Sounds — parse as nested object
if (obj.contains("sounds") && obj["sounds"].is_object()) {
    auto& s = obj["sounds"];
    if (s.contains("footstep") && s["footstep"].is_string())
        def.soundFootstep = s["footstep"].get<std::string>();
    if (s.contains("dig") && s["dig"].is_string())
        def.soundDig = s["dig"].get<std::string>();
    if (s.contains("place") && s["place"].is_string())
        def.soundPlace = s["place"].get<std::string>();
}

// Liquid — parse as nested object
if (obj.contains("liquid") && obj["liquid"].is_object()) {
    auto& l = obj["liquid"];
    if (l.contains("type") && l["type"].is_string())
        def.liquidType = parseLiquidType(l["type"].get<std::string>());
    // ... viscosity, range, renewable, alternatives
}

// PostEffectColor — parse as hex string "0x80000044" or integer
if (obj.contains("postEffectColor")) {
    if (obj["postEffectColor"].is_string()) {
        def.postEffectColor = static_cast<uint32_t>(
            std::stoul(obj["postEffectColor"].get<std::string>(), nullptr, 16));
    } else if (obj["postEffectColor"].is_number_unsigned()) {
        def.postEffectColor = obj["postEffectColor"].get<uint32_t>();
    }
}
```

### Enum Parse Helpers

Keep these as private static methods in BlockRegistry.cpp (not in Block.h — Block.h has no cpp file):

```cpp
static RenderType parseRenderType(const std::string& s) {
    if (s == "cutout") return RenderType::Cutout;
    if (s == "translucent") return RenderType::Translucent;
    return RenderType::Opaque;
}

static ModelType parseModelType(const std::string& s) {
    if (s == "slab") return ModelType::Slab;
    if (s == "stair") return ModelType::Stair;
    if (s == "cross") return ModelType::Cross;
    if (s == "torch") return ModelType::Torch;
    if (s == "connected") return ModelType::Connected;
    if (s == "json_model") return ModelType::JsonModel;
    if (s == "mesh_model") return ModelType::MeshModel;
    if (s == "custom") return ModelType::Custom;
    return ModelType::FullCube;
}

static LiquidType parseLiquidType(const std::string& s) {
    if (s == "source") return LiquidType::Source;
    if (s == "flowing") return LiquidType::Flowing;
    return LiquidType::None;
}

static PushReaction parsePushReaction(const std::string& s) {
    if (s == "destroy") return PushReaction::Destroy;
    if (s == "block") return PushReaction::Block;
    return PushReaction::Normal;
}
```

### lightFilter Default Change

Current default is 15 (fully opaque to light). This is wrong — the default should be 0 (transparent to light), and solid blocks should explicitly set `lightFilter: 15` in JSON. This matches Luanti where `light_propagates = true` is the default.

**Impact:** All 10 existing blocks in blocks.json that are solid need `"lightFilter": 15` added. Water already has `"lightFilter": 2` (correct). Torch has `"lightFilter": 0` (correct by new default).

### Updated blocks.json — Water Example

```json
{
    "stringId": "base:water",
    "isSolid": false,
    "isTransparent": true,
    "hasCollision": false,
    "lightEmission": 0,
    "lightFilter": 2,
    "hardness": 100.0,
    "textureIndices": [6, 6, 6, 6, 6, 6],
    "dropItem": "",
    "renderType": "translucent",
    "isReplaceable": true,
    "moveResistance": 3,
    "drowning": 1,
    "postEffectColor": "0x80000044",
    "liquid": {
        "type": "source",
        "viscosity": 1,
        "range": 8,
        "renewable": true
    }
}
```

### Block States — NOT in This Story

The `properties`, `baseStateId`, `stateCount` fields from the architecture spec are part of **Story 3.4 (Block State System)** — do NOT add them here. Block.h will be extended again in 3.4.

### Lua Callbacks — NOT in This Story

The `sol::function` callback fields are part of **Epic 9** — do NOT add them or any sol2 includes here.

### What NOT to Do

- Do NOT add `#include <sol/sol.hpp>` or any Lua types — that's Epic 9
- Do NOT add `BlockStateProperty` or state fields — that's Story 3.4
- Do NOT change the BlockRegistry API (registerBlock, getBlock, getIdByName, blockCount) — only the struct and JSON parser change
- Do NOT change `BLOCK_AIR` constant or the air registration logic
- Do NOT add `std::unordered_map` to Block.h without `#include <unordered_map>` — add the include
- Do NOT add `std::vector` to Block.h without `#include <vector>` — but we don't need vector yet (block states are Story 3.4)
- Do NOT parse unknown JSON fields as errors — silently ignore them (forward compatibility for mods)

### Project Structure Notes

**Modified files:**
```
engine/include/voxel/world/Block.h          (expand struct + add enums)
engine/src/world/BlockRegistry.cpp          (expand JSON parser + add enum helpers)
assets/scripts/base/blocks.json             (add new fields to existing blocks)
tests/world/TestBlockRegistry.cpp           (add tests for new fields)
```

**No new files created.**

### Architecture Compliance

- **ADR-008 (No exceptions):** No change — BlockRegistry already uses Result<T>.
- **Naming conventions:** `RenderType` (PascalCase enum class), `isClimbable` (camelCase bool), `m_nonAirCount` pattern respected, `BLOCK_AIR` (SCREAMING_SNAKE constant).
- **Stub fields pattern:** Fields for unimplemented systems (sound, liquid, signal) are stored but never read. Zero runtime cost (just struct padding). They exist so Block.h doesn't change when those systems arrive.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-03-voxel-world-core.md — Story 3.3 full BlockDefinition spec]
- [Source: _bmad-output/planning-artifacts/luanti-gap-analysis.md — ContentFeatures field mapping]
- [Source: engine/include/voxel/world/Block.h — Current 10-field version]
- [Source: engine/src/world/BlockRegistry.cpp — Current JSON parser]
- [Source: assets/scripts/base/blocks.json — Current 10 blocks]
- [Source: tests/world/TestBlockRegistry.cpp — Current 18 test sections]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

None — clean implementation, no errors encountered.

### Completion Notes List

- Expanded BlockDefinition from 10 to 35+ fields with 4 new enum classes (RenderType, ModelType, LiquidType, PushReaction)
- All new fields have sensible defaults — zero breaking changes to existing code
- Changed lightFilter default from 15 to 0 (transparent by default); solid blocks now specify lightFilter: 15 in JSON
- Added 4 static enum parse helpers in BlockRegistry.cpp (parseRenderType, parseModelType, parseLiquidType, parsePushReaction)
- JSON parser extended to handle: enum fields, groups map, sounds object, liquid object, postEffectColor (hex string or uint), and all scalar new fields
- Updated all 10 blocks in blocks.json with relevant new properties (groups, renderType, liquid, waving, isFallingBlock, etc.)
- Added 11 new test sections covering: default values, enum parsing, groups parsing, backward compatibility, water/leaves/torch/sand/stone field verification
- All existing test sections preserved unchanged

### Change Log

- 2026-03-26: Story 3.3b implementation complete — expanded BlockDefinition to 35+ fields with full JSON parsing and test coverage
- 2026-03-26: Code review fix — replaced `std::stoul` with `std::from_chars` in postEffectColor hex parsing (crash risk in no-exceptions build)

### File List

- engine/include/voxel/world/Block.h (modified — expanded struct, added 4 enums, added #include <unordered_map>)
- engine/src/world/BlockRegistry.cpp (modified — added 4 enum parse helpers, expanded JSON parser for all new fields, changed lightFilter default from 15 to 0)
- assets/scripts/base/blocks.json (modified — added new fields to all 10 blocks)
- tests/world/TestBlockRegistry.cpp (modified — added 11 new test sections)
