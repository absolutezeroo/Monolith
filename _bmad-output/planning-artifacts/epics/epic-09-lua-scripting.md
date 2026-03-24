# Epic 9 — Lua Scripting

**Priority**: P1
**Dependencies**: Epic 3, Epic 7
**Goal**: Full Lua scripting via sol2/LuaJIT — mods can register blocks/items, react to events, query and modify the world, all sandboxed and hot-reloadable.

---

## Story 9.1: sol2 + LuaJIT Integration

**As a** developer,
**I want** a Lua VM embedded in the engine via sol2,
**so that** scripts can be loaded and executed.

**Acceptance Criteria:**
- `ScriptEngine` class: owns a `sol::state`, initializes with safe libraries only (`base`, `math`, `string`, `table`, `coroutine`)
- `os`, `io`, `debug`, `loadfile`, `dofile`, `load` explicitly removed from the environment
- LuaJIT configured as the Lua runtime (linked via vcpkg)
- `ScriptEngine::loadScript(path) → Result<void>` — executes a Lua file
- `ScriptEngine::callFunction(name, args...) → Result<sol::object>` — calls a global Lua function
- Error handling: Lua errors caught by sol2, converted to `EngineError::ScriptError`, logged with file+line
- Filesystem access restricted: scripts can only read from their own mod directory
- Integration test: load a Lua file that sets a global variable, verify from C++

---

## Story 9.2: Block & Item Registration API

**As a** developer,
**I want** Lua scripts to register new block and item types,
**so that** mods can add content without recompiling.

**Acceptance Criteria:**
- `voxel` table exposed to Lua with registration functions
- `voxel.register_block(table)` — table fields: `id` (string, required), `solid` (bool), `transparent` (bool), `light_emission` (int 0–15), `hardness` (float), `textures` (table: `all` or per-face `top`/`bottom`/`north`/`south`/`east`/`west`), `drop` (string item ID), callbacks
- `voxel.register_item(table)` — fields: `id` (string), `stack_size` (int), `block` (string block ID if placeable)
- Registration adds to BlockRegistry / ItemRegistry at startup
- Validation: duplicate IDs rejected with error log, missing required fields rejected
- Base game blocks registered via `assets/scripts/base/init.lua` — stone, dirt, grass, sand, water, wood, leaves, etc.
- Integration test: Lua registers a block, C++ verifies it exists in the registry with correct properties

---

## Story 9.3: Event Hook System

**As a** developer,
**I want** Lua scripts to subscribe to game events,
**so that** mods can react to gameplay actions.

**Acceptance Criteria:**
- `voxel.on(event_name, callback)` — registers a Lua callback for a named event
- Supported events: `"block_placed"`, `"block_broken"`, `"player_join"`, `"tick"`, `"chunk_loaded"`
- Event data passed as Lua table: `{pos={x,y,z}, block_id=..., player_id=...}` (event-specific fields)
- Multiple callbacks per event, called in registration order
- C++ EventBus publishes events → ScriptEngine dispatches to registered Lua callbacks
- Error in one callback doesn't prevent others from executing (logged, not fatal)
- `voxel.on("block_placed", function(e) ... end)` — e.g., automatically place a torch nearby
- Integration test: register callback for `block_broken`, break a block, verify callback was invoked

---

## Story 9.4: World Query & Modification API

**As a** developer,
**I want** Lua scripts to read and modify the voxel world,
**so that** mods can create dynamic gameplay mechanics.

**Acceptance Criteria:**
- `voxel.get_block(x, y, z) → block_string_id` — returns string ID of block at position
- `voxel.set_block(x, y, z, block_string_id)` — sets block (pushes PlaceBlockCommand internally)
- `voxel.raycast(ox, oy, oz, dx, dy, dz, max_dist) → table or nil` — returns `{pos={x,y,z}, face=..., block_id=...}` or nil
- `voxel.get_block_info(string_id) → table` — returns block definition as Lua table
- Rate limiting: `set_block` capped at 1000 calls per tick per mod; `raycast` capped at 100
- Exceeded rate: logged as warning, call returns nil/false, does NOT crash
- Coordinate validation: out-of-world-bounds calls return nil/air gracefully
- Integration test: Lua script places a 5×5×5 cube of stone, verify blocks exist in ChunkManager

---

## Story 9.5: Mod Loading + Hot-Reload

**As a** developer,
**I want** mods loaded from folders and hot-reloadable at runtime,
**so that** content iteration is fast during development.

**Acceptance Criteria:**
- Mod discovery: scan `assets/scripts/mods/` for directories containing `init.lua`
- Load order: `base/` first, then mod directories in alphabetical order (configurable via `mod-order.json` if present)
- Each mod gets an isolated Lua environment (no global pollution between mods)
- Mod metadata: optional `mod.json` with `name`, `version`, `description`, `dependencies`
- Hot-reload command (F5 or console command): destroy all Lua state, clear mod-registered blocks/items (keep base), re-execute all scripts, re-register everything
- After hot-reload: existing world keeps block data (numeric IDs), registry rebuilds string→ID mapping
- Error in one mod: log error with mod name, skip that mod, continue loading others
- ImGui panel: list loaded mods with status (OK / Error), reload button
