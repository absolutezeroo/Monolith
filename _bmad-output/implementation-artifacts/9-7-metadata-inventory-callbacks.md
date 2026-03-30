# Story 9.7: Metadata & Inventory Callbacks

Status: ready-for-dev

## Story

As a developer,
I want blocks to store custom key-value data and manage per-block inventories,
so that mods can create chests, furnaces, signs, and other container/stateful blocks.

## Acceptance Criteria

1. `BlockMetadata` class provides a per-block key-value store with typed accessors: `set_string`/`get_string`, `set_int`/`get_int`, `set_float`/`get_float`. Keys are strings; values are `std::variant<std::string, int32_t, float>`.
2. `voxel.get_meta(pos)` Lua API returns a `MetaDataRef` sol2 usertype bound to the metadata at that position. Creates empty metadata on first access.
3. `BlockInventory` class provides named inventory lists of `ItemStack`s. `inv:set_size("main", 27)` creates/resizes a list. `inv:get_stack(list, index)`, `inv:set_stack(list, index, stack)`, `inv:get_size(list)`, `inv:is_empty(list)`.
4. `ItemStack` value type: `name` (string ID), `count` (uint16_t). Lua usertype with `get_name()`, `get_count()`, `set_count()`, `is_empty()`, `ItemStack("base:stone", 64)` constructor.
5. `voxel.get_inventory(pos)` Lua API returns an `InvRef` sol2 usertype bound to the inventory at that position. Creates empty inventory on first access.
6. 3 permission callbacks: `allow_inventory_put(pos, listname, index, stack, player) -> int`, `allow_inventory_take(pos, listname, index, stack, player) -> int`, `allow_inventory_move(pos, from_list, from_index, to_list, to_index, count, player) -> int`. Return 0 = deny, N = allow up to N items.
7. 3 notification callbacks: `on_inventory_put(pos, listname, index, stack, player)`, `on_inventory_take(pos, listname, index, stack, player)`, `on_inventory_move(pos, from_list, from_index, to_list, to_index, count, player)`. No return value.
8. Metadata and inventory are stored in `ChunkColumn` via sparse maps (only blocks with metadata/inventory allocate storage).
9. `ChunkSerializer` extended: metadata and inventory data serialized after section block data, deserialized on load. Existing region files without metadata load cleanly (backward compatible).
10. Integration test: register a chest block, call `voxel.get_meta(pos)` and `voxel.get_inventory(pos)` from `on_construct`, set metadata values, set inventory size, programmatically put/take items via Lua, verify `allow_inventory_put` is called and can deny, verify metadata persists across serialize/deserialize roundtrip.

## Tasks / Subtasks

- [ ] Task 1: Create ItemStack value type (AC: 4)
  - [ ] 1.1 Create `engine/include/voxel/world/ItemStack.h` — `struct ItemStack { std::string name; uint16_t count = 0; }` with `isEmpty()`, `getCount()`, `getName()`, `setCount()`, `clear()`
  - [ ] 1.2 Create `engine/src/world/ItemStack.cpp` — trivial implementations
  - [ ] 1.3 Register `ItemStack` as sol2 usertype in LuaBindings: constructor `ItemStack("base:stone", 64)`, methods `get_name`, `get_count`, `set_count`, `is_empty`
  - [ ] 1.4 Add to `engine/CMakeLists.txt`

- [ ] Task 2: Create BlockMetadata class (AC: 1, 8)
  - [ ] 2.1 Create `engine/include/voxel/world/BlockMetadata.h` — key-value store using `std::unordered_map<std::string, std::variant<std::string, int32_t, float>>`
  - [ ] 2.2 Methods: `setString(key, value)`, `getString(key, default="")`, `setInt(key, value)`, `getInt(key, default=0)`, `setFloat(key, value)`, `getFloat(key, default=0.0f)`, `contains(key)`, `erase(key)`, `clear()`, `empty()`, `size()`
  - [ ] 2.3 Create `engine/src/world/BlockMetadata.cpp` — implementations
  - [ ] 2.4 Serialization helpers: `serialize(BinaryWriter&)` and `static deserialize(BinaryReader&) -> Result<BlockMetadata>` — write count, then for each entry: key string + type tag (0=string, 1=int, 2=float) + value
  - [ ] 2.5 Add to `engine/CMakeLists.txt`

- [ ] Task 3: Create BlockInventory class (AC: 3, 8)
  - [ ] 3.1 Create `engine/include/voxel/world/BlockInventory.h` — named lists: `std::unordered_map<std::string, std::vector<ItemStack>>`
  - [ ] 3.2 Methods: `setSize(listname, size)`, `getSize(listname)`, `getStack(listname, index)`, `setStack(listname, index, stack)`, `isEmpty(listname)`, `isEmpty()` (all lists), `getListNames()`
  - [ ] 3.3 Create `engine/src/world/BlockInventory.cpp` — implementations with bounds checking (out-of-range returns empty ItemStack)
  - [ ] 3.4 Serialization helpers: `serialize(BinaryWriter&)` and `static deserialize(BinaryReader&) -> Result<BlockInventory>` — write list count, for each list: name + size + each ItemStack (name + count)
  - [ ] 3.5 Add to `engine/CMakeLists.txt`

- [ ] Task 4: Add metadata/inventory storage to ChunkColumn (AC: 8)
  - [ ] 4.1 Add `std::unordered_map<uint16_t, BlockMetadata> m_metadata` to `ChunkColumn` private members (key = packed local index: `x + z*16 + y*256`, fits uint16_t since max = 15 + 15*16 + 255*256 = 65535)
  - [ ] 4.2 Add `std::unordered_map<uint16_t, BlockInventory> m_inventories` to `ChunkColumn` private members
  - [ ] 4.3 Add public accessors: `BlockMetadata* getMetadata(int x, int y, int z)`, `BlockMetadata& getOrCreateMetadata(int x, int y, int z)`, `void removeMetadata(int x, int y, int z)`
  - [ ] 4.4 Add public accessors: `BlockInventory* getInventory(int x, int y, int z)`, `BlockInventory& getOrCreateInventory(int x, int y, int z)`, `void removeInventory(int x, int y, int z)`
  - [ ] 4.5 Add `bool hasBlockData(int x, int y, int z) const` — returns true if metadata or inventory exists at position
  - [ ] 4.6 Helper: `static uint16_t packLocalIndex(int x, int y, int z)` — `x + z * 16 + y * 256`
  - [ ] 4.7 In `setBlock()`: when a block is replaced with air (or different type), remove metadata and inventory at that position (cleanup stale data)

- [ ] Task 5: Extend ChunkSerializer for metadata/inventory (AC: 9)
  - [ ] 5.1 After writing all section data in `serializeColumn()`, write a metadata section: magic byte `0xMD` (0x4D44), then count of metadata entries, then for each: packed index (u16) + BlockMetadata serialization
  - [ ] 5.2 After metadata section, write inventory section: magic byte `0xIV` (0x4956), then count of inventory entries, then for each: packed index (u16) + BlockInventory serialization
  - [ ] 5.3 In `deserializeColumn()`: after reading all sections, check if more data remains. If so, read magic bytes and deserialize metadata/inventory. If no more data, return normally (backward compatible with old region files).
  - [ ] 5.4 Expose `BinaryWriter` and `BinaryReader` from anonymous namespace to a shared internal header `engine/src/world/BinaryIO.h` (or make them static members of ChunkSerializer) so BlockMetadata and BlockInventory serialization can use them
  - [ ] 5.5 Integration: roundtrip test — serialize column with metadata+inventory, deserialize, verify all data preserved

- [ ] Task 6: Add 6 inventory callbacks to BlockCallbacks (AC: 6, 7)
  - [ ] 6.1 Add to `BlockCallbacks` struct in `BlockCallbacks.h`:
    ```
    // --- Inventory callbacks ---
    std::optional<sol::protected_function> allowInventoryPut;   // (pos, listname, index, stack, player) -> int
    std::optional<sol::protected_function> allowInventoryTake;  // (pos, listname, index, stack, player) -> int
    std::optional<sol::protected_function> allowInventoryMove;  // (pos, from_list, from_idx, to_list, to_idx, count, player) -> int
    std::optional<sol::protected_function> onInventoryPut;      // (pos, listname, index, stack, player)
    std::optional<sol::protected_function> onInventoryTake;     // (pos, listname, index, stack, player)
    std::optional<sol::protected_function> onInventoryMove;     // (pos, from_list, from_idx, to_list, to_idx, count, player)
    ```
  - [ ] 6.2 Update `categoryMask()` — add Bit 4 (0x10) for inventory callback category
  - [ ] 6.3 Verify struct remains movable

- [ ] Task 7: Add inventory invoke methods to BlockCallbackInvoker (AC: 6, 7)
  - [ ] 7.1 Add to `BlockCallbackInvoker.h`:
    ```
    [[nodiscard]] int invokeAllowInventoryPut(def, pos, listname, index, stack, playerId);
    [[nodiscard]] int invokeAllowInventoryTake(def, pos, listname, index, stack, playerId);
    [[nodiscard]] int invokeAllowInventoryMove(def, pos, fromList, fromIdx, toList, toIdx, count, playerId);
    void invokeOnInventoryPut(def, pos, listname, index, stack, playerId);
    void invokeOnInventoryTake(def, pos, listname, index, stack, playerId);
    void invokeOnInventoryMove(def, pos, fromList, fromIdx, toList, toIdx, count, playerId);
    ```
  - [ ] 7.2 Implement in `BlockCallbackInvoker.cpp` following existing pattern: check `has_value()` → `protected_function_result` → `.valid()` → log error → return safe default
  - [ ] 7.3 Permission defaults: `allowInventoryPut` and `allowInventoryTake` return the requested count (allow all). `allowInventoryMove` returns the requested count.
  - [ ] 7.4 Pass `ItemStack` as sol2 usertype argument to Lua callbacks

- [ ] Task 8: Extract inventory callbacks in LuaBindings (AC: 6, 7)
  - [ ] 8.1 In `parseBlockDefinition()`, add 6 new temporary variables and `checkAndStore()` calls:
    ```
    checkAndStore("allow_inventory_put", cbAllowInventoryPut);
    checkAndStore("allow_inventory_take", cbAllowInventoryTake);
    checkAndStore("allow_inventory_move", cbAllowInventoryMove);
    checkAndStore("on_inventory_put", cbOnInventoryPut);
    checkAndStore("on_inventory_take", cbOnInventoryTake);
    checkAndStore("on_inventory_move", cbOnInventoryMove);
    ```
  - [ ] 8.2 In the `hasAnyCallback` block, move the 6 callbacks into `BlockCallbacksPtr`

- [ ] Task 9: Register Lua APIs and usertypes (AC: 2, 3, 4, 5)
  - [ ] 9.1 Register `ItemStack` usertype in `LuaBindings::registerBlockAPI()`:
    ```cpp
    lua.new_usertype<world::ItemStack>("ItemStack",
        sol::constructors<world::ItemStack(), world::ItemStack(std::string, uint16_t)>(),
        "get_name", &world::ItemStack::getName,
        "get_count", &world::ItemStack::getCount,
        "set_count", &world::ItemStack::setCount,
        "is_empty", &world::ItemStack::isEmpty
    );
    ```
  - [ ] 9.2 Register `BlockMetadata` as `MetaDataRef` usertype:
    ```cpp
    lua.new_usertype<world::BlockMetadata>("MetaDataRef",
        "set_string", &world::BlockMetadata::setString,
        "get_string", &world::BlockMetadata::getString,
        "set_int", &world::BlockMetadata::setInt,
        "get_int", &world::BlockMetadata::getInt,
        "set_float", &world::BlockMetadata::setFloat,
        "get_float", &world::BlockMetadata::getFloat,
        "contains", &world::BlockMetadata::contains,
        "erase", &world::BlockMetadata::erase
    );
    ```
  - [ ] 9.3 Register `BlockInventory` as `InvRef` usertype:
    ```cpp
    lua.new_usertype<world::BlockInventory>("InvRef",
        "set_size", &world::BlockInventory::setSize,
        "get_size", &world::BlockInventory::getSize,
        "get_stack", &world::BlockInventory::getStack,
        "set_stack", &world::BlockInventory::setStack,
        "is_empty", sol::overload(
            static_cast<bool(world::BlockInventory::*)(const std::string&) const>(&world::BlockInventory::isEmpty),
            static_cast<bool(world::BlockInventory::*)() const>(&world::BlockInventory::isEmpty)
        )
    );
    ```
  - [ ] 9.4 Register `voxel.get_meta(pos)` function — needs access to `ChunkManager` to resolve world pos → ChunkColumn → local pos → `getOrCreateMetadata()`. Returns reference to BlockMetadata.
  - [ ] 9.5 Register `voxel.get_inventory(pos)` function — same resolution path, returns reference to BlockInventory.
  - [ ] 9.6 Both APIs require ChunkManager access: add `ChunkManager*` parameter to `registerBlockAPI()` or create a separate `registerWorldAPI(lua, chunkManager)` method in LuaBindings. **Preferred: new `registerMetadataAPI(lua, chunkManager)` static method** to keep registration modular.

- [ ] Task 10: Integration tests (AC: 10)
  - [ ] 10.1 Create `tests/scripting/TestBlockMetadata.cpp`
  - [ ] 10.2 Test: `BlockMetadata` set/get string/int/float, missing key returns default, contains/erase/clear
  - [ ] 10.3 Test: `BlockMetadata` serialize/deserialize roundtrip preserves all types
  - [ ] 10.4 Test: `BlockInventory` setSize, getStack, setStack, isEmpty, bounds checking
  - [ ] 10.5 Test: `BlockInventory` serialize/deserialize roundtrip preserves all lists and items
  - [ ] 10.6 Test: `ItemStack` construction, getters, isEmpty
  - [ ] 10.7 Create `tests/scripting/TestMetadataInventoryCallbacks.cpp`
  - [ ] 10.8 Test: register block with `on_construct` that calls `voxel.get_meta(pos)` and sets values, verify metadata accessible from C++
  - [ ] 10.9 Test: register block with `allow_inventory_put` returning 0 (deny), invoke callback, verify returns 0
  - [ ] 10.10 Test: register block with `allow_inventory_put` returning stack count (allow), verify returns correct count
  - [ ] 10.11 Test: register block with `on_inventory_put` notification, invoke, verify Lua global flag set
  - [ ] 10.12 Test: ChunkSerializer roundtrip with metadata and inventory — serialize column, deserialize, verify data
  - [ ] 10.13 Test: ChunkSerializer backward compat — deserialize old-format column (no metadata section), verify loads cleanly
  - [ ] 10.14 Create Lua test scripts: `metadata_basic.lua`, `inventory_chest.lua`, `inventory_deny.lua`, `inventory_notify.lua`

- [ ] Task 11: Build integration (AC: all)
  - [ ] 11.1 Add `ItemStack.cpp`, `BlockMetadata.cpp`, `BlockInventory.cpp` to `engine/CMakeLists.txt`
  - [ ] 11.2 Add `TestBlockMetadata.cpp`, `TestMetadataInventoryCallbacks.cpp` to `tests/CMakeLists.txt`
  - [ ] 11.3 Build full project, verify zero warnings under `/W4 /WX`
  - [ ] 11.4 Run all tests (existing + new), verify zero regressions

## Dev Notes

### Architecture: Where Metadata/Inventory Live

Metadata and inventory are **per-block instance data** (not per-block-type). They live in `ChunkColumn` alongside the block ID array, using sparse maps keyed by packed local index.

```
ChunkColumn
├── m_sections[16]         — block type IDs (uint16_t per block)
├── m_lightMaps[16]        — lighting data
├── m_metadata             — sparse map: packed_index → BlockMetadata
└── m_inventories          — sparse map: packed_index → BlockInventory
```

**Packed index**: `x + z * 16 + y * 256` where x,z ∈ [0,15] and y ∈ [0,255]. Max value = 65535 → `uint16_t`.

**Opt-in allocation**: Only blocks whose `on_construct` (or other callback) calls `voxel.get_meta(pos)` or `voxel.get_inventory(pos)` will have storage allocated. Most blocks (stone, dirt, air) never allocate.

**Cleanup on block removal**: When `ChunkColumn::setBlock()` replaces a block with air or a different type, any metadata and inventory at that position MUST be removed to prevent stale data leaks.

### BlockMetadata Design

```cpp
// engine/include/voxel/world/BlockMetadata.h
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace voxel::world
{

class BlockMetadata
{
public:
    using Value = std::variant<std::string, int32_t, float>;

    void setString(const std::string& key, const std::string& value);
    [[nodiscard]] std::string getString(const std::string& key, const std::string& defaultVal = "") const;

    void setInt(const std::string& key, int32_t value);
    [[nodiscard]] int32_t getInt(const std::string& key, int32_t defaultVal = 0) const;

    void setFloat(const std::string& key, float value);
    [[nodiscard]] float getFloat(const std::string& key, float defaultVal = 0.0f) const;

    [[nodiscard]] bool contains(const std::string& key) const;
    void erase(const std::string& key);
    void clear();
    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    // Serialization
    void serialize(class BinaryWriter& writer) const;
    [[nodiscard]] static core::Result<BlockMetadata> deserialize(class BinaryReader& reader);

private:
    std::unordered_map<std::string, Value> m_data;
};

} // namespace voxel::world
```

**Serialization format**: `[u16 count] [for each: u16 key_len, key_bytes, u8 type_tag, value_data]`
- Type tags: 0 = string (u16 len + bytes), 1 = int32 (4 bytes LE), 2 = float (4 bytes LE, `memcpy`)

### BlockInventory Design

```cpp
// engine/include/voxel/world/BlockInventory.h
#pragma once

#include "voxel/world/ItemStack.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace voxel::world
{

class BlockInventory
{
public:
    void setSize(const std::string& listname, size_t size);
    [[nodiscard]] size_t getSize(const std::string& listname) const;

    [[nodiscard]] ItemStack getStack(const std::string& listname, size_t index) const;
    void setStack(const std::string& listname, size_t index, const ItemStack& stack);

    [[nodiscard]] bool isEmpty(const std::string& listname) const;
    [[nodiscard]] bool isEmpty() const; // All lists empty

    [[nodiscard]] std::vector<std::string> getListNames() const;

    // Serialization
    void serialize(class BinaryWriter& writer) const;
    [[nodiscard]] static core::Result<BlockInventory> deserialize(class BinaryReader& reader);

private:
    std::unordered_map<std::string, std::vector<ItemStack>> m_lists;
};

} // namespace voxel::world
```

**Serialization format**: `[u16 list_count] [for each list: u16 name_len, name_bytes, u16 slot_count, [for each slot: u16 name_len, name_bytes, u16 count]]`

### ItemStack Design

```cpp
// engine/include/voxel/world/ItemStack.h
#pragma once

#include <cstdint>
#include <string>

namespace voxel::world
{

struct ItemStack
{
    std::string name;
    uint16_t count = 0;

    ItemStack() = default;
    ItemStack(std::string itemName, uint16_t itemCount);

    [[nodiscard]] const std::string& getName() const { return name; }
    [[nodiscard]] uint16_t getCount() const { return count; }
    void setCount(uint16_t c) { count = c; }
    [[nodiscard]] bool isEmpty() const { return name.empty() || count == 0; }
    void clear() { name.clear(); count = 0; }
};

} // namespace voxel::world
```

### BinaryIO Shared Utility

`BinaryWriter` and `BinaryReader` currently live in an anonymous namespace in `ChunkSerializer.cpp`. They need to be shared with `BlockMetadata` and `BlockInventory` serialization.

**Solution**: Extract to `engine/src/world/BinaryIO.h` — internal header (in `src/`, not `include/`), not part of public API. Only used by serialization code.

```cpp
// engine/src/world/BinaryIO.h
#pragma once
#include "voxel/core/Result.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace voxel::world
{
class BinaryWriter { /* ... same as ChunkSerializer.cpp ... */ };
class BinaryReader { /* ... same as ChunkSerializer.cpp ... */ };
} // namespace voxel::world
```

Update `ChunkSerializer.cpp` to `#include "BinaryIO.h"` and remove the anonymous namespace versions.

### ChunkSerializer Extension — Backward Compatible

**Serialization format (extended)**:
```
[u16 section_bitmask]
[for each section: bitsPerEntry, palette, packed data]  ← existing
[u16 magic 0x4D44 "MD"]                                 ← NEW: metadata marker
[u16 metadata_count]
[for each: u16 packed_index, BlockMetadata blob]
[u16 magic 0x4956 "IV"]                                 ← NEW: inventory marker
[u16 inventory_count]
[for each: u16 packed_index, BlockInventory blob]
```

**Backward compatibility**: After reading all sections, check `reader.hasRemaining(2)`. If false → old format, return column as-is. If true → read magic, deserialize metadata/inventory. If magic doesn't match → skip (future extensibility).

### Callback Invocation Pattern (Same as 9.2/9.3)

Permission callbacks return `int` (not `bool`). Default on missing/error: return the requested count (allow all).

```cpp
int BlockCallbackInvoker::invokeAllowInventoryPut(
    const BlockDefinition& def,
    const glm::ivec3& pos,
    const std::string& listname,
    size_t index,
    const ItemStack& stack,
    uint32_t playerId)
{
    if (!def.callbacks || !def.callbacks->allowInventoryPut.has_value())
        return static_cast<int>(stack.count); // Default: allow all

    auto posTable = posToTable(m_lua, pos);
    sol::protected_function_result result =
        (*def.callbacks->allowInventoryPut)(posTable, listname, index, stack, playerId);

    if (!result.valid())
    {
        sol::error err = result;
        VX_LOG_WARN("Lua allow_inventory_put error for '{}': {}", def.stringId, err.what());
        return static_cast<int>(stack.count);
    }

    return result.get_type() == sol::type::number
        ? result.get<int>()
        : static_cast<int>(stack.count);
}
```

**Default return values per callback:**

| Callback | Default (nil/missing) | Default (error) |
|----------|----------------------|-----------------|
| `allow_inventory_put` | requested count (allow all) | requested count |
| `allow_inventory_take` | requested count (allow all) | requested count |
| `allow_inventory_move` | requested count (allow all) | requested count |
| `on_inventory_put` | no-op | no-op |
| `on_inventory_take` | no-op | no-op |
| `on_inventory_move` | no-op | no-op |

### Lua API: voxel.get_meta / voxel.get_inventory

These APIs need `ChunkManager` access to resolve world position → chunk column → local position. The current `LuaBindings::registerBlockAPI()` doesn't have ChunkManager.

**Solution**: Add a new static registration method:

```cpp
// In LuaBindings.h
static void registerMetadataAPI(sol::state& lua, world::ChunkManager& chunkManager);
```

Called from GameApp after `registerBlockAPI()`. Binds:

```cpp
void LuaBindings::registerMetadataAPI(sol::state& lua, world::ChunkManager& chunkManager)
{
    sol::table voxelTable = lua["voxel"];

    // Register usertypes first
    lua.new_usertype<world::ItemStack>("ItemStack", ...);
    lua.new_usertype<world::BlockMetadata>("MetaDataRef", ...);
    lua.new_usertype<world::BlockInventory>("InvRef", ...);

    voxelTable.set_function("get_meta", [&chunkManager](const sol::table& posTable) -> world::BlockMetadata& {
        int x = posTable.get<int>("x");
        int y = posTable.get<int>("y");
        int z = posTable.get<int>("z");
        // Resolve to chunk column + local position
        glm::ivec2 chunkCoord = {
            voxel::math::floorDiv(x, 16),
            voxel::math::floorDiv(z, 16)
        };
        auto* column = chunkManager.getColumn(chunkCoord);
        if (!column)
        {
            // Chunk not loaded — return a static dummy metadata (logged as warning)
            VX_LOG_WARN("voxel.get_meta: chunk not loaded at ({}, {})", chunkCoord.x, chunkCoord.y);
            static world::BlockMetadata s_dummyMeta;
            return s_dummyMeta;
        }
        int lx = voxel::math::euclideanMod(x, 16);
        int lz = voxel::math::euclideanMod(z, 16);
        return column->getOrCreateMetadata(lx, y, lz);
    });

    voxelTable.set_function("get_inventory", [&chunkManager](const sol::table& posTable) -> world::BlockInventory& {
        // Same resolution pattern as get_meta
        // ...
        return column->getOrCreateInventory(lx, y, lz);
    });
}
```

**Note**: Returning references to data inside ChunkColumn is safe within a single Lua callback execution (same tick, no chunk unloading during Lua calls). The `MetaDataRef`/`InvRef` should NOT be stored across ticks by Lua code — document this limitation.

### What NOT to Do

- **DO NOT create an ECS component for block metadata** — metadata lives in ChunkColumn, not the ECS. The architecture doc lists "Block entities — ChestContent, FurnaceState" as future ECS work, but V1 uses the simpler ChunkColumn-based approach. ECS block entities come in a future epic when the full entity system exists.
- **DO NOT add sol2 headers to ChunkColumn.h** — BlockMetadata and BlockInventory are pure C++ classes with no Lua dependency. Sol2 usertypes are registered separately in LuaBindings.
- **DO NOT modify ScriptEngine** — unchanged since Story 9.1.
- **DO NOT implement inventory UI (formspec)** — V1 is API-only. Players can't see inventories until a GUI system exists. Mods manipulate inventories programmatically.
- **DO NOT implement `voxel.set_block`/`voxel.get_block` world APIs** — that's Story 9.8.
- **DO NOT implement global events** — that's Story 9.10.
- **DO NOT implement item tools, enchantments, or tool groups** — `ItemStack` is minimal (name + count). Tool metadata comes in a future epic.
- **DO NOT implement the `drop` callback as a function** — Story 9.2 handles the `drop` field as a string. Function-based drops are deferred.
- **DO NOT modify BlockCallbackInvoker constructor signature** — it takes `(sol::state&, BlockRegistry&)`. The inventory invokers don't need ChunkManager because they receive the ItemStack directly.
- **DO NOT use `std::any` for metadata values** — use `std::variant<std::string, int32_t, float>` for type safety and serialization.
- **DO NOT store Lua objects in BlockMetadata** — only primitive types that can be serialized. If a mod needs to store complex Lua tables, they must serialize to a string themselves.

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `BlockCallbacks` | `engine/include/voxel/scripting/BlockCallbacks.h` | Add 6 inventory callback fields |
| `BlockCallbackInvoker` | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | Add 6 invoke methods |
| `BlockCallbackInvoker.cpp` | `engine/src/scripting/BlockCallbackInvoker.cpp` | `posToTable()` utility, invocation pattern |
| `LuaBindings` | `engine/src/scripting/LuaBindings.cpp` | Add callback extraction + usertype registration |
| `LuaBindings.h` | `engine/include/voxel/scripting/LuaBindings.h` | Add `registerMetadataAPI()` declaration |
| `ChunkColumn` | `engine/include/voxel/world/ChunkColumn.h` | Add metadata/inventory maps |
| `ChunkColumn.cpp` | `engine/src/world/ChunkColumn.cpp` | Implement accessors, cleanup in setBlock |
| `ChunkSerializer` | `engine/include/voxel/world/ChunkSerializer.h` | Extend serialize/deserialize |
| `ChunkSerializer.cpp` | `engine/src/world/ChunkSerializer.cpp` | BinaryWriter/BinaryReader extraction, metadata/inventory sections |
| `BlockRegistry` | `engine/include/voxel/world/BlockRegistry.h` | Look up BlockDefinition for callback access |
| `ChunkManager` | `engine/include/voxel/world/ChunkManager.h` | `getColumn()` for world-pos → chunk resolution |
| `Result<T>` | `engine/include/voxel/core/Result.h` | Error handling |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging |
| `CoordUtils` | `engine/include/voxel/math/CoordUtils.h` | `floorDiv()`, `euclideanMod()` |

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| NEW | `engine/include/voxel/world/ItemStack.h` | `voxel::world` | Simple value type (name + count) |
| NEW | `engine/src/world/ItemStack.cpp` | `voxel::world` | Constructor implementation |
| NEW | `engine/include/voxel/world/BlockMetadata.h` | `voxel::world` | Per-block key-value store |
| NEW | `engine/src/world/BlockMetadata.cpp` | `voxel::world` | Metadata implementation + serialization |
| NEW | `engine/include/voxel/world/BlockInventory.h` | `voxel::world` | Named inventory lists |
| NEW | `engine/src/world/BlockInventory.cpp` | `voxel::world` | Inventory implementation + serialization |
| NEW | `engine/src/world/BinaryIO.h` | `voxel::world` | Shared BinaryWriter/BinaryReader (internal header) |
| MODIFY | `engine/include/voxel/world/ChunkColumn.h` | `voxel::world` | Add metadata/inventory maps + accessors |
| MODIFY | `engine/src/world/ChunkColumn.cpp` | `voxel::world` | Implement accessors, cleanup in setBlock |
| MODIFY | `engine/src/world/ChunkSerializer.cpp` | `voxel::world` | Use BinaryIO.h, add metadata/inventory sections |
| MODIFY | `engine/include/voxel/scripting/BlockCallbacks.h` | `voxel::scripting` | Add 6 inventory callback fields + categoryMask bit 4 |
| MODIFY | `engine/include/voxel/scripting/BlockCallbackInvoker.h` | `voxel::scripting` | Add 6 invoke method declarations |
| MODIFY | `engine/src/scripting/BlockCallbackInvoker.cpp` | `voxel::scripting` | Implement 6 invoke methods |
| MODIFY | `engine/include/voxel/scripting/LuaBindings.h` | `voxel::scripting` | Add `registerMetadataAPI()` declaration |
| MODIFY | `engine/src/scripting/LuaBindings.cpp` | `voxel::scripting` | Extract 6 inventory callbacks + register 3 usertypes + voxel.get_meta/get_inventory |
| MODIFY | `engine/CMakeLists.txt` | — | Add ItemStack.cpp, BlockMetadata.cpp, BlockInventory.cpp |
| NEW | `tests/scripting/TestBlockMetadata.cpp` | — | Unit tests for metadata + inventory classes |
| NEW | `tests/scripting/TestMetadataInventoryCallbacks.cpp` | — | Integration tests for Lua callbacks |
| NEW | `tests/scripting/test_scripts/metadata_basic.lua` | — | Test: get_meta, set/get values |
| NEW | `tests/scripting/test_scripts/inventory_chest.lua` | — | Test: on_construct creates inventory |
| NEW | `tests/scripting/test_scripts/inventory_deny.lua` | — | Test: allow_inventory_put returns 0 |
| NEW | `tests/scripting/test_scripts/inventory_notify.lua` | — | Test: on_inventory_put notification |
| MODIFY | `tests/CMakeLists.txt` | — | Add test files |

### Naming & Style

- Classes: `BlockMetadata`, `BlockInventory`, `ItemStack`, `BinaryWriter`, `BinaryReader` (PascalCase)
- Methods: `setString`, `getString`, `setSize`, `getStack`, `setStack`, `isEmpty`, `invokeAllowInventoryPut` (camelCase)
- Members: `m_data`, `m_lists`, `m_metadata`, `m_inventories` (m_ prefix)
- Constants: `BLOCK_AIR` (SCREAMING_SNAKE)
- Namespace: `voxel::world` for data classes, `voxel::scripting` for callback/binding classes
- No exceptions — `Result<T>` for deserialization, safe defaults for callback invocation
- Max ~500 lines per file
- `#pragma once` for all headers
- `[[nodiscard]]` on all query methods

### Previous Story Intelligence

**From Story 9.6 (most recent story file):**
- EntityHandle pattern: lightweight usertype wrapping C++ objects for Lua access — BlockMetadata/BlockInventory follow same pattern
- Callback dispatch in GameApp (not PlayerController) — inventory callbacks similarly dispatched where inventory operations happen
- categoryMask currently uses bits 0–3 (placement, destruction, interaction, timer) — inventory = bit 4 (0x10)
- Invocation pattern: `has_value()` → `protected_function_result` → `.valid()` → log error → return default

**From Story 9.3:**
- `checkAndStore()` lambda pattern for extracting callbacks — extend with 6 new calls
- Callback temporary variables + move into BlockCallbacksPtr — same pattern for inventory callbacks

**From Story 9.2:**
- `LuaBindings::parseBlockDefinition()` extracts callbacks with `table.get<std::optional<sol::protected_function>>("field_name")`
- `BlockCallbacksPtr` = `unique_ptr<BlockCallbacks, BlockCallbacksDeleter>` — defined in Block.h
- `registerBlockAPI` structure — `registerMetadataAPI` follows same pattern as separate static method

**From ChunkSerializer (Story 3.7):**
- BinaryWriter/BinaryReader currently in anonymous namespace in ChunkSerializer.cpp
- Serialization uses little-endian throughout
- String format: u16 length prefix + raw bytes
- LZ4 compression wraps the entire column binary blob — metadata/inventory included automatically
- Region file format with chunk indexing — no changes needed at region level

### Git Intelligence

Recent scripting commits:
```
bd1dcce feat(scripting): implement entity-block interaction callbacks for fall, step, overlap, and collide
e98e6ec feat(scripting): implement block interaction callbacks and sustained interactions
24efe9d feat(scripting): add Lua-based block callbacks and block registration API
effe7b4 feat(scripting): implement neighbor/shape/physics callbacks in BlockCallbacks
8ce6142 feat(scripting): implement sol2/LuaJIT-based ScriptEngine with sandbox and integration tests
```

Commit message for this story: `feat(scripting): implement block metadata/inventory system with per-block storage and Lua callbacks`

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/world/BlockMetadata.h"
#include "voxel/world/BlockInventory.h"
#include "voxel/world/ItemStack.h"

using namespace voxel::world;

TEST_CASE("BlockMetadata typed storage", "[world][metadata]")
{
    BlockMetadata meta;

    SECTION("string values")
    {
        meta.setString("text", "Hello World");
        REQUIRE(meta.getString("text") == "Hello World");
        REQUIRE(meta.getString("missing", "default") == "default");
    }

    SECTION("int values")
    {
        meta.setInt("count", 42);
        REQUIRE(meta.getInt("count") == 42);
        REQUIRE(meta.getInt("missing", -1) == -1);
    }

    SECTION("float values")
    {
        meta.setFloat("temperature", 1200.5f);
        REQUIRE(meta.getFloat("temperature") == Catch::Approx(1200.5f));
    }

    SECTION("contains and erase")
    {
        meta.setString("key", "val");
        REQUIRE(meta.contains("key"));
        meta.erase("key");
        REQUIRE_FALSE(meta.contains("key"));
    }
}

TEST_CASE("BlockInventory named lists", "[world][inventory]")
{
    BlockInventory inv;

    SECTION("set_size creates list")
    {
        inv.setSize("main", 27);
        REQUIRE(inv.getSize("main") == 27);
        REQUIRE(inv.isEmpty("main"));
    }

    SECTION("set_stack and get_stack")
    {
        inv.setSize("main", 9);
        inv.setStack("main", 0, ItemStack{"base:stone", 64});
        auto stack = inv.getStack("main", 0);
        REQUIRE(stack.getName() == "base:stone");
        REQUIRE(stack.getCount() == 64);
        REQUIRE_FALSE(inv.isEmpty("main"));
    }

    SECTION("out-of-bounds returns empty")
    {
        inv.setSize("main", 3);
        auto stack = inv.getStack("main", 99);
        REQUIRE(stack.isEmpty());
    }
}
```

**Lua test scripts:**

**metadata_basic.lua:**
```lua
voxel.register_block({
    id = "test:sign",
    on_construct = function(pos)
        local meta = voxel.get_meta(pos)
        meta:set_string("text", "Hello")
        meta:set_int("line_count", 4)
        meta:set_float("scale", 1.5)
        test_meta_constructed = true
    end,
})
```

**inventory_chest.lua:**
```lua
voxel.register_block({
    id = "test:chest",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("main", 27)
        test_chest_constructed = true
    end,
    allow_inventory_put = function(pos, listname, index, stack, player)
        return stack:get_count()
    end,
    on_inventory_put = function(pos, listname, index, stack, player)
        test_put_fired = true
        test_put_item = stack:get_name()
    end,
})
```

**inventory_deny.lua:**
```lua
voxel.register_block({
    id = "test:locked_chest",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("main", 9)
    end,
    allow_inventory_put = function(pos, listname, index, stack, player)
        return 0 -- Deny all
    end,
    allow_inventory_take = function(pos, listname, index, stack, player)
        return 0 -- Deny all
    end,
})
```

**inventory_notify.lua:**
```lua
voxel.register_block({
    id = "test:hopper",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("input", 5)
        inv:set_size("output", 5)
    end,
    on_inventory_move = function(pos, from_list, from_index, to_list, to_index, count, player)
        test_move_from = from_list
        test_move_to = to_list
        test_move_count = count
    end,
})
```

### Project Structure Notes

- `ItemStack.h/.cpp` in `voxel::world` — it's a data type closely related to blocks and inventories
- `BlockMetadata.h/.cpp` in `voxel::world` — per-block data, lives alongside ChunkColumn
- `BlockInventory.h/.cpp` in `voxel::world` — per-block data, uses ItemStack
- `BinaryIO.h` in `engine/src/world/` (NOT `include/`) — internal utility, not public API
- All 3 new classes follow one-class-per-file rule
- Test scripts in `tests/scripting/test_scripts/` alongside existing test scripts

### Future Story Dependencies

This story establishes patterns used by:
- **Story 9.8**: `voxel.get_meta(pos)` and `voxel.get_inventory(pos)` provide the data layer that world query APIs build upon. `preserveMetadata` callback (already in BlockCallbacks since 9.2) uses `BlockMetadata` to transfer metadata to dropped items.
- **Story 9.9**: `get_color` callback can read metadata (`voxel.get_meta(pos):get_int("power_level")`) to determine visual state.
- **Story 9.10**: `block_changed` global event can carry metadata snapshots.
- **Story 9.11**: Hot-reload preserves metadata/inventory (data is in ChunkColumn, not Lua state).
- **Future Block GUI epic (12)**: Inventory UI (formspec) renders `BlockInventory` contents. Permission callbacks gate player interaction with container UI.
- **Future crafting system**: Furnace `on_timer` reads fuel/input inventories, produces output items.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.7 full specification]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 3: ECS, System 4: Block Registry, System 9: Scripting, System 10: Command Pattern, ADR-007, ADR-010]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards, serialization]
- [Source: engine/include/voxel/scripting/BlockCallbacks.h — Current 25 callback fields across 4 categories]
- [Source: engine/include/voxel/scripting/BlockCallbackInvoker.h — Current invoke method signatures]
- [Source: engine/src/scripting/LuaBindings.cpp — parseBlockDefinition callback extraction, checkAndStore pattern]
- [Source: engine/include/voxel/world/Block.h — BlockDefinition with BlockCallbacksPtr]
- [Source: engine/include/voxel/world/ChunkColumn.h — Current storage: sections, lightmaps, heightmap]
- [Source: engine/src/world/ChunkSerializer.cpp — BinaryWriter/BinaryReader, section serialization, LZ4]
- [Source: engine/include/voxel/world/ChunkSerializer.h — serialize/deserialize API]
- [Source: _bmad-output/implementation-artifacts/9-6-entity-block-interaction-callbacks.md — EntityHandle pattern, callback dispatch in GameApp]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
