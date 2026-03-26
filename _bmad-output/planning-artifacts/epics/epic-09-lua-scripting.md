# Epic 9 — Lua Scripting (Complete Block Callback System)

**Priority**: P1
**Dependencies**: Epic 3, Epic 7
**Goal**: Full Lua scripting via sol2/LuaJIT with a comprehensive block callback API covering all 9 interaction categories. Mods can register blocks with 40+ callbacks, react to placement/destruction/interaction/ticks/neighbors/entities/physics, query and modify the world, all sandboxed and hot-reloadable.

**Reference**: See `_bmad-output/planning-artifacts/technical-research.md` — Block Callbacks cross-engine analysis (Luanti, Minecraft, Vintage Story).

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

## Story 9.2: Block Registration + Placement & Destruction Callbacks

**As a** developer,
**I want** Lua scripts to register blocks with full placement and destruction callback chains,
**so that** mods can control every aspect of block lifecycle.

**Acceptance Criteria:**

**Registration API:**
- `voxel.register_block(table)` with all `BlockDefinition` properties:
  - Required: `id` (string, namespaced)
  - Core: `solid`, `transparent`, `has_collision`, `light_emission`, `light_filter`, `hardness`
  - Rendering: `render_type` ("opaque"/"cutout"/"translucent"), `model_type` ("full_cube"/"slab"/"stair"/"cross"/"torch"/"connected"/"json_model"/"mesh_model"/"custom"), `tint` ("none"/"grass"/"foliage"/"water"), `waving` (0–3)
  - Textures: `textures = { all = "stone.png" }` or per-face `{ top = "...", bottom = "...", ... }`
  - JsonModel: `model = { elements = { { from={...}, to={...}, rotation={...}, faces={...} } } }` (Minecraft-style rotated cuboids with per-face UV)
  - JsonModel variants: `model_variants = { ["facing=north"] = { model = "...", y_rotation = 0 } }` (state-dependent model selection)
  - MeshModel: `mesh = "models/my_model.obj"` (external .obj file, max 1024 vertices)
  - Physics: `climbable`, `move_resistance` (0–7), `damage_per_second`, `buildable_to`, `floodable`
  - Groups: `groups = { cracky = 3, stone = 1 }` (for tool matching, ABM targeting)
  - Block states: `properties = { facing = {"north","south","east","west"}, open = {"true","false"} }`
  - Connected: `connects_to = { "group:fence", "group:solid" }`, `connect_boxes = { center = {...}, north = {...} }`
  - Node boxes: `node_boxes = { {x1,y1,z1, x2,y2,z2}, ... }` (for custom model type)
  - Visual: `post_effect_color = 0x80000044` (overlay when camera inside)
  - Sound (stubs): `sounds = { footstep = "stone_step", dig = "stone_dig", place = "stone_place" }`
  - Liquid (stubs): `liquid_type` ("none"/"source"/"flowing"), `liquid_viscosity`, `liquid_range`, `liquid_renewable`, `liquid_alternative_flowing`, `liquid_alternative_source`
  - Mechanical: `push_reaction` ("normal"/"destroy"/"block"), `falling` (bool — falls when unsupported)
  - Signal/Redstone (stubs): `power_output` (0–15 static), `is_power_source`, `is_power_conductor`
  - Drop: `drop = "base:cobblestone"` or `drop = function(pos, player, tool) ... end`
- `voxel.register_item(table)` — `id`, `stack_size`, `block` (if placeable)
- All fields optional except `id` — sensible defaults match a basic solid opaque cube
- Validation: duplicate IDs rejected, missing `id` rejected, namespace enforced

**JSON → Lua migration (replaces Story 3.3 JSON loader):**
- Base blocks registered via `assets/scripts/base/init.lua`
- JSON loading code from Story 3.3 deleted
- `blocks.json` removed from repo

**Placement callbacks (7):**
```lua
voxel.register_block({
    id = "base:torch",
    -- ...properties...
    
    -- Can this block be placed here? Return false to prevent.
    can_place = function(pos, player, pointed_thing) return true end,
    
    -- Determine block state/variant for placement (rotation, direction, etc.)
    get_state_for_placement = function(pos, player, pointed_thing) return nil end,
    
    -- Perform the placement. Return remaining itemstack.
    on_place = function(itemstack, placer, pointed_thing) return itemstack end,
    
    -- Called when block is first created (no player context — includes worldgen).
    on_construct = function(pos) end,
    
    -- Called after player places this block. Return true to consume itemstack.
    after_place = function(pos, placer, itemstack, pointed_thing) return true end,
    
    -- Can this block be replaced by a new placement? (like tall grass yields to stone)
    can_be_replaced = function(pos, context) return false end,
})
```

**Destruction callbacks (8):**
```lua
voxel.register_block({
    id = "base:chest",
    
    -- Can player dig this block?
    can_dig = function(pos, player) return true end,
    
    -- Called before block is removed (block still exists).
    on_destruct = function(pos) end,
    
    -- Player digs the block. Return false to prevent default behavior.
    on_dig = function(pos, node, digger) return true end,
    
    -- Called after block removed (receives old node data).
    after_destruct = function(pos, oldnode) end,
    
    -- Called after player digs (receives old metadata for drop generation).
    after_dig = function(pos, oldnode, oldmetadata, digger) end,
    
    -- Block destroyed by explosion.
    on_blast = function(pos, intensity) end,
    
    -- Block replaced by liquid (water flow).
    on_flood = function(pos, oldnode, newnode) return false end,
    
    -- Transfer metadata to dropped items before removal.
    preserve_metadata = function(pos, oldnode, oldmeta, drops) end,
    
    -- Custom drop calculation. Return table of itemstacks.
    get_drops = function(pos, player, tool_groups)
        -- tool_groups = { pickaxe = true, fortune = 2 } (from future tool system)
        -- Return different drops based on tool enchantments
        if tool_groups and tool_groups.silk_touch then
            return { "base:chest" }
        end
        return { "base:plank 4" }
    end,
    
    -- Experience dropped when mined (0 = no XP). Receives tool info for fortune bonus.
    get_experience = function(pos, player, tool_groups) return 0 end,
    
    -- Called each tick while player is mining this block. Return false to cancel mining.
    on_dig_progress = function(pos, player, progress)
        -- progress = 0.0 to 1.0
        -- Example: protected block that can't be mined
        if is_protected(pos, player) then return false end
        return true
    end,
})
```

**C++ engine support required:**
- `BlockDefinition` extended with `std::optional<sol::function>` fields for each callback
- When engine calls `setBlock(pos, AIR)` (break) or `setBlock(pos, id)` (place):
  1. Check `can_dig` / `can_place` → abort if false
  2. Call `on_destruct` / `on_place`
  3. Perform the block change
  4. Call `after_destruct` / `after_place`
  5. Publish events to EventBus → global Lua hooks fire too
- `preserve_metadata` called between destruct and after_destruct when drops are generated
- All callbacks are optional — if nil, default engine behavior applies

**Integration test:** Register a chest block with `can_dig` returning false, verify player can't break it.

---

## Story 9.3: Block Interaction Callbacks (Right-click, Punch, Multi-phase)

**As a** developer,
**I want** blocks to react to player clicks and sustained interactions,
**so that** mods can create interactive blocks (doors, levers, furnaces, crafting tables).

**Acceptance Criteria:**

**Basic interaction (3):**
```lua
voxel.register_block({
    id = "base:door",
    
    -- Right-click the block. Return modified itemstack.
    on_rightclick = function(pos, node, clicker, itemstack, pointed_thing)
        -- Toggle door state
        return itemstack
    end,
    
    -- Left-click / punch the block (not dig — quick hit).
    on_punch = function(pos, node, puncher, pointed_thing) end,
    
    -- Called when player uses item while not pointing at any block.
    on_secondary_use = function(itemstack, user, pointed_thing) end,
})
```

**Multi-phase interaction (Vintage Story inspired — 4):**
```lua
voxel.register_block({
    id = "mymod:grindstone",
    
    -- Player starts holding right-click on block. Return true to begin sustained interaction.
    on_interact_start = function(pos, player) return true end,
    
    -- Called every tick while player holds right-click. Return true to continue.
    on_interact_step = function(pos, player, elapsed_seconds)
        if elapsed_seconds >= 3.0 then
            -- Grinding complete after 3 seconds
            voxel.set_block(pos, "mymod:grindstone_empty")
            return false -- stop interaction
        end
        return true -- continue
    end,
    
    -- Player releases right-click.
    on_interact_stop = function(pos, player, elapsed_seconds) end,
    
    -- Interaction cancelled (player moves away, takes damage, etc.)
    on_interact_cancel = function(pos, player, elapsed_seconds, reason) return true end,
})
```

**C++ engine support required:**
- `InputManager` tracks mouse button hold duration (already tracks press/release from Story 3.0)
- New `InteractionState` in PlayerController: tracks which block is being interacted with, start time, active callback
- On right-click press: call `on_interact_start` if defined, else fall back to `on_rightclick`
- While held: call `on_interact_step` each tick with elapsed seconds
- On release: call `on_interact_stop`
- On cancel conditions (player moves >2 blocks away, takes damage, opens menu): call `on_interact_cancel`
- ImGui check: if ImGui wants mouse, skip all block interaction

**Integration test:** Register a grindstone with 3-second hold interaction, verify `on_interact_step` fires each tick and stops after release.

---

## Story 9.4: Block Tick & Update System (Timers, ABM, LBM)

**As a** developer,
**I want** blocks to have scheduled timers and area-based random updates,
**so that** mods can create time-dependent mechanics (crop growth, fire spread, leaf decay).

**Acceptance Criteria:**

**Per-block timer:**
```lua
voxel.register_block({
    id = "base:furnace",
    
    -- Called when timer fires. Return true to restart timer, false to stop.
    on_timer = function(pos, elapsed)
        -- Smelt one item
        return has_more_items
    end,
})

-- Start a timer on a specific block
voxel.set_timer(pos, 2.0) -- Fire in 2 seconds
voxel.get_timer(pos) -- Returns remaining time or nil
```

**Active Block Modifier (ABM — Luanti-inspired area scanner):**
```lua
voxel.register_abm({
    label = "Grass spread",
    nodenames = { "base:dirt" },            -- What blocks to scan for
    neighbors = { "base:grass" },           -- Required neighbors (optional)
    interval = 5.0,                          -- Seconds between scans
    chance = 10,                             -- 1/10 chance per matching block
    action = function(pos, node, active_object_count)
        voxel.set_block(pos, "base:grass")
    end,
})
```

**Loading Block Modifier (LBM — fires once when chunk loads):**
```lua
voxel.register_lbm({
    label = "Upgrade old torches",
    nodenames = { "base:torch_old" },
    run_at_every_load = false,              -- true = every load, false = only first time
    action = function(pos, node, dtime_s)
        voxel.set_block(pos, "base:torch")
    end,
})
```

**C++ engine support required:**
- `BlockTimerManager` class: stores `unordered_map<ivec3, TimerEntry>` with pos → (callback, remaining_time, interval)
- Updated in simulation tick: decrement timers, fire callbacks when expired
- Timers persist in chunk serialization (save/load timer state alongside block data)
- ABM system: `ABMRegistry` stores registered ABMs. During world tick, iterate loaded sections, check block types against ABM nodenames, roll chance, fire callback
- ABM runs on a separate timer (not every tick — every `interval` seconds), spread across multiple frames to avoid spikes
- LBM system: `LBMRegistry` stores registered LBMs. On chunk load, scan for matching blocks, fire callbacks. Track which LBMs have run via per-chunk metadata
- Performance: ABM scan capped at N blocks per tick, spread across frames

**Integration test:** Register ABM for grass spread, place dirt next to grass, wait 30 seconds, verify dirt becomes grass.

---

## Story 9.5: Neighbor Change + Physics Shape Callbacks

**As a** developer,
**I want** blocks to react when adjacent blocks change and to provide dynamic collision/selection shapes,
**so that** mods can create blocks that depend on their surroundings (torches, doors, fences, rails).

**Acceptance Criteria:**

**Neighbor change (3):**
```lua
voxel.register_block({
    id = "base:torch",
    
    -- Called when an adjacent block changes.
    on_neighbor_changed = function(pos, neighbor_pos, neighbor_node)
        -- Check if support block removed
        if not voxel.get_block(pos.x, pos.y - 1, pos.z).solid then
            voxel.dig_block(pos) -- Torch falls
        end
    end,
    
    -- Determine new block state based on neighbor (for connected blocks like fences).
    update_shape = function(pos, direction, neighbor_state) return nil end,
    
    -- Can this block survive at this position? Called after neighbor changes.
    can_survive = function(pos)
        return voxel.get_block(pos.x, pos.y - 1, pos.z).solid
    end,
})
```

**Physics/collision shape (5):**
```lua
voxel.register_block({
    id = "base:slab_bottom",
    
    -- Custom collision shape (list of boxes {x1,y1,z1, x2,y2,z2}).
    get_collision_shape = function(pos)
        return {{ 0, 0, 0, 1, 0.5, 1 }} -- Half-height slab
    end,
    
    -- Custom selection (targeting) shape.
    get_selection_shape = function(pos)
        return {{ 0, 0, 0, 1, 0.5, 1 }}
    end,
    
    -- Can another block attach to this face? (for torches, ladders, etc.)
    can_attach_at = function(pos, face) return face == "top" end,
    
    -- Is this block walkable for AI pathfinding?
    is_pathfindable = function(pos, pathtype) return true end,
})
```

**Signal/Power callbacks (stubs — functional when signal system is implemented):**
```lua
voxel.register_block({
    id = "base:redstone_lamp",
    
    -- Called when the block receives or loses redstone/signal power.
    on_powered = function(pos, power_level, source_pos)
        -- power_level: 0 (off) to 15 (max)
        if power_level > 0 then
            voxel.swap_block(pos, "base:redstone_lamp_on")
        else
            voxel.swap_block(pos, "base:redstone_lamp_off")
        end
    end,
    
    -- Comparator reads this block's "fullness" or signal output.
    -- Return 0–15. Used by containers (chest fullness), beacons, etc.
    get_comparator_output = function(pos)
        local inv = voxel.get_inventory(pos)
        if inv then
            return math.floor(inv:get_fullness("main") * 15)
        end
        return 0
    end,
    
    -- Can this block be pushed by a piston? Return "normal", "destroy", or "block".
    get_push_reaction = function(pos)
        return "normal" -- Override the static push_reaction property dynamically
    end,
})
```

**C++ engine support required:**
- `ChunkManager::setBlock()` now fires neighbor notifications: after setting a block, call `on_neighbor_changed` on all 6 adjacent blocks
- After `on_neighbor_changed`, check `can_survive` — if false, destroy the block (cascade: this may trigger more neighbor changes, cap recursion at 64)
- `get_collision_shape` / `get_selection_shape`: if defined, the collision system (Story 7.2) and DDA raycasting (Story 7.4) use the returned boxes instead of the default full-block AABB
- Cache dynamic shapes: only re-query when the block or its neighbors change (dirty flag)
- Default shapes: if callbacks are nil, use full block (solid) or empty (air)

**Integration test:** Register torch with `can_survive` checking block below, break support block, verify torch is destroyed too.

---

## Story 9.6: Entity-Block Interaction Callbacks

**As a** developer,
**I want** blocks to react when entities step on, fall on, or move inside them,
**so that** mods can create soul sand (slowness), cactus (damage), slime blocks (bounce), and pressure plates.

**Acceptance Criteria:**

**Entity callbacks (5):**
```lua
voxel.register_block({
    id = "base:cactus",
    
    -- Entity is inside the block's collision volume.
    on_entity_inside = function(pos, entity)
        entity:damage(0.5) -- Half heart per tick
    end,
    
    -- Entity steps on top of the block.
    on_entity_step_on = function(pos, entity)
        -- Play step sound, etc.
    end,
    
    -- Entity falls onto the block. Return modified damage.
    on_entity_fall_on = function(pos, entity, fall_distance)
        return fall_distance * 1.0 -- Full fall damage (default)
    end,
    
    -- Entity collides with any face. Provides velocity and impact info.
    on_entity_collide = function(pos, entity, facing, velocity, is_impact) end,
    
    -- Projectile hits the block.
    on_projectile_hit = function(pos, projectile, hit_result) end,
})
```

**C++ engine support required:**
- In collision resolution (Story 7.2): after resolving AABB collision, check if the block at the resolved position has `on_entity_step_on` (Y collision from above) or `on_entity_collide` (any axis)
- In physics tick: check if player AABB overlaps any block with `on_entity_inside` — iterate blocks within player AABB
- `on_entity_fall_on`: called when OnGround transitions from false to true, with accumulated fall distance
- `on_projectile_hit`: V1 stub — no projectile system yet, but the callback slot exists in the registration table. Will be wired when projectile entities are implemented
- Entity Lua wrapper: `entity:damage(amount)`, `entity:get_velocity()`, `entity:get_position()`, `entity:set_velocity(vec3)` — minimal interface, expanded when mob system is added

**V1 scope:** Only the player triggers these callbacks. Full entity support (mobs, items) comes when ECS entity system is populated in a future epic.

**Integration test:** Register slime block with `on_entity_fall_on` returning 0 damage, fall on it, verify no damage.

---

## Story 9.7: Metadata & Inventory Callbacks

**As a** developer,
**I want** blocks to store custom data and manage per-block inventories,
**so that** mods can create chests, furnaces, and other container blocks.

**Acceptance Criteria:**

**Block metadata API:**
```lua
voxel.register_block({
    id = "base:sign",
    on_construct = function(pos)
        local meta = voxel.get_meta(pos)
        meta:set_string("text", "")
    end,
    on_rightclick = function(pos, node, clicker, itemstack)
        local meta = voxel.get_meta(pos)
        -- Show sign editing UI (future formspec system)
    end,
})

-- Metadata API
local meta = voxel.get_meta(pos)
meta:set_string("key", "value")
meta:get_string("key") --> "value"
meta:set_int("count", 42)
meta:get_int("count") --> 42
meta:set_float("temperature", 1200.5)
```

**Block inventory callbacks (6):**
```lua
voxel.register_block({
    id = "base:chest",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("main", 27) -- 27 slots
    end,
    
    -- Permission: how many items can be put? Return 0 to deny.
    allow_inventory_put = function(pos, listname, index, stack, player)
        return stack:get_count() -- Allow all
    end,
    
    -- Permission: how many items can be taken?
    allow_inventory_take = function(pos, listname, index, stack, player)
        return stack:get_count()
    end,
    
    -- Permission: how many items can be moved within inventory?
    allow_inventory_move = function(pos, from_list, from_index, to_list, to_index, count, player)
        return count
    end,
    
    -- Notification: items were put.
    on_inventory_put = function(pos, listname, index, stack, player) end,
    
    -- Notification: items were taken.
    on_inventory_take = function(pos, listname, index, stack, player) end,
    
    -- Notification: items were moved.
    on_inventory_move = function(pos, from_list, from_index, to_list, to_index, count, player) end,
})
```

**C++ engine support required:**
- `BlockMetadata` class: per-block key-value store (`unordered_map<string, variant<string, int, float>>`)
- Stored in ChunkColumn alongside block data, serialized/deserialized with chunks
- `BlockInventory` class: named lists of ItemStacks, per-block
- `allow_inventory_*` callbacks return int (0 = deny, N = allow up to N items)
- `on_inventory_*` callbacks are notifications (no return value)
- Metadata and inventory are opt-in: only blocks that call `get_meta`/`get_inventory` in `on_construct` allocate storage

**V1 scope:** Metadata API fully functional. Inventory API is defined and callable but the inventory UI (formspec) is post-V1. Mods can manipulate inventories programmatically but players can't see them until a UI system exists.

**Integration test:** Register a chest, place it, put items via Lua API, verify `allow_inventory_put` is called, verify items persist after save/load.

---

## Story 9.8: World Query & Modification API

**As a** developer,
**I want** Lua scripts to read and modify the voxel world,
**so that** mods can create dynamic gameplay mechanics.

**Acceptance Criteria:**

**Block query & modification (existing):**
- `voxel.get_block(x, y, z) → block_string_id`
- `voxel.get_block_info(string_id) → table` (full BlockDefinition as Lua table)
- `voxel.get_block_state(x, y, z) → table` — returns `{facing="north", open="true", ...}`
- `voxel.set_block(x, y, z, block_string_id)` — pushes PlaceBlockCommand internally, triggers full callback chain
- `voxel.set_block_state(x, y, z, block_string_id, state_table)` — sets block with specific state: `voxel.set_block_state(x,y,z, "base:door", {facing="north", open="true"})`
- `voxel.dig_block(pos)` — triggers destruction callbacks as if a player broke it
- `voxel.swap_block(pos, new_id)` — changes block without triggering place/break callbacks (for state changes like door open/close)
- `voxel.raycast(ox, oy, oz, dx, dy, dz, max_dist) → table or nil` — `{pos={x,y,z}, face=..., block_id=...}`
- `voxel.find_blocks_in_area(pos1, pos2, block_id) → list of positions`
- `voxel.find_blocks_in_area(pos1, pos2, "group:cracky") → list of positions` — group matching
- `voxel.count_blocks_in_area(pos1, pos2, block_id) → int` — faster than find when you only need the count
- `voxel.get_biome(x, z) → biome_string_id`
- `voxel.get_light(x, y, z) → sky_light, block_light`
- `voxel.get_time_of_day() → float` — 0.0 = midnight, 0.5 = noon
- `voxel.set_time_of_day(float)` — for time manipulation mods

**Timer & scheduled ticks:**
- `voxel.set_timer(pos, seconds)` — starts a block timer (fires `on_timer`)
- `voxel.get_timer(pos) → seconds_remaining or nil`
- `voxel.schedule_tick(pos, delay_ticks, priority)` — schedules a one-shot tick at exact game tick precision (not wall-clock). Priority determines execution order when multiple ticks fire on the same tick. Used for redstone-style deterministic propagation.
- `voxel.set_node_timer_active(pos, bool)` — pause/resume a running timer without resetting

**Multiblock pattern matching (helper API):**
```lua
-- Check if blocks around pos match a pattern
-- Returns true/false + list of matched positions
local matches, positions = voxel.check_pattern(pos, {
    -- Each entry: {dx, dy, dz, "block_id" or "group:name"}
    {0, -1, 0, "base:iron_block"},     -- One below
    {0, -2, 0, "base:iron_block"},     -- Two below
    {1, -1, 0, "base:iron_block"},     -- One below, one east
    {-1, -1, 0, "base:iron_block"},    -- One below, one west
    -- ... etc for a beacon pyramid
})

-- Check a 3D box pattern (more efficient for regular shapes)
local matches = voxel.check_box_pattern(
    {x-2, y-4, z-2},              -- Corner 1
    {x+2, y-1, z+2},              -- Corner 2
    "base:iron_block",              -- Required block (or "group:beacon_base")
    { allow_mixed = true }          -- Options: iron OR gold OR diamond OR emerald
)

-- Scan a ring at a specific Y offset (for beacon layers)
local complete, count = voxel.check_ring(pos, y_offset, radius, "group:beacon_base")
-- complete = true if ring is fully filled, count = how many blocks match
```

**Usage example — Beacon activation:**
```lua
voxel.register_block({
    id = "base:beacon",
    on_construct = function(pos)
        voxel.set_timer(pos, 4.0) -- Check pyramid every 4 seconds
    end,
    on_timer = function(pos, elapsed)
        local power_level = 0
        for layer = 1, 4 do
            local ok, _ = voxel.check_ring(pos, -layer, layer, "group:beacon_base")
            if ok then power_level = layer else break end
        end
        local meta = voxel.get_meta(pos)
        meta:set_int("power_level", power_level)
        -- Apply beacon effect to nearby players (future: entity API)
        return true -- Keep checking
    end,
    get_comparator_output = function(pos)
        return voxel.get_meta(pos):get_int("power_level") * 3  -- 0, 3, 6, 9, 12
    end,
})
```

**Settings API:**
- `voxel.get_setting(name) → value` — read engine/game settings
- `voxel.set_setting(name, value)` — modify (only allowed settings, sandboxed)
- Allowed settings: `random_tick_speed` (default 3, affects ABM chance), `time_speed` (default 72, game-minutes per real-minute), `render_distance` (read-only for mods)

**Rate limiting:**
- `set_block`: max 1000/tick/mod
- `raycast`: max 100/tick/mod
- `find_blocks_in_area`: max 10/tick/mod
- `check_pattern`/`check_ring`: max 50/tick/mod
- `schedule_tick`: max 500/tick/mod
- Coordinate validation: out-of-bounds returns nil gracefully

**Integration test:** Lua places a 5×5×5 cube, `find_blocks_in_area` returns all 125 positions. Beacon pattern check returns correct power level for 1-4 layer pyramids.

---

## Story 9.9: Visual & Client Callbacks

**As a** developer,
**I want** blocks to emit particles and customize their visual appearance via Lua,
**so that** mods can create fire particles, smoke, colored blocks, and animated blocks.

**Acceptance Criteria:**

**Visual callbacks (4):**
```lua
voxel.register_block({
    id = "base:fire",
    
    -- Client-side particle tick (called every frame, not every simulation tick).
    on_animate_tick = function(pos, random)
        voxel.add_particle({
            pos = { x = pos.x + random(), y = pos.y + 0.5, z = pos.z + random() },
            velocity = { x = 0, y = 1.5, z = 0 },
            lifetime = 0.5,
            texture = "flame.png",
            size = 0.3,
        })
    end,
    
    -- Override block color (for map rendering, biome-tinted grass, etc.)
    get_color = function(pos) return 0x7CFC00 end, -- Lawn green
    
    -- Creative mode pick block — what item to give when middle-click.
    on_pick_block = function(pos) return "base:fire" end,
})

-- Particle API
voxel.add_particle({
    pos = {x, y, z},
    velocity = {x, y, z},
    acceleration = {x, y, z},  -- Optional, default {0,0,0}
    lifetime = 1.0,             -- Seconds
    texture = "particle.png",
    size = 0.2,
    collide = true,             -- Collide with blocks
})

-- Spawn N particles in an area
voxel.add_particle_spawner({
    amount = 50,
    time = 2.0,                 -- Duration of spawner (0 = permanent until removed)
    minpos = {x, y, z},
    maxpos = {x, y, z},
    minvel = {x, y, z},
    maxvel = {x, y, z},
    texture = "smoke.png",
    size = 0.5,
})
```

**C++ engine support required:**
- Particle system: `ParticleManager` class, billboard quads rendered in a separate pass
- `on_animate_tick`: called from render loop (not simulation tick) for blocks within a radius of the player (~32 blocks)
- Particle budget: max N active particles (default 2000), oldest killed when exceeded
- `get_color`: cached per-block, invalidated when block or neighbor changes. Used as tint multiplier on the albedo in fragment shader
- `on_pick_block`: wired to middle-click input (V1: just returns the block's own string ID by default)

**V1 scope:** Particle system is simple billboards, no physics. `on_animate_tick` works. `get_color` works for block tinting. Advanced tesselation callbacks (`on_tesselate`) deferred to post-V1.

**Integration test:** Register fire block with particle emission, place it, verify particles appear in debug overlay particle count.

---

## Story 9.10: Global Event Hooks

**As a** developer,
**I want** Lua scripts to subscribe to engine-wide events beyond per-block callbacks,
**so that** mods can react to player actions, world events, and game lifecycle.

**Acceptance Criteria:**

**Global event registration — 41 events + combo API across 7 categories:**

```lua
-- ═══════════════════════════════════════════
-- PLAYER EVENTS (12)
-- ═══════════════════════════════════════════

-- Lifecycle
voxel.on("player_join", function(player) end)
voxel.on("player_leave", function(player) end)
voxel.on("player_respawn", function(player) end)

-- Health
voxel.on("player_damage", function(player, amount, source) end)
    -- source = "fall" | "block" | "void" | "entity" | "environment"
voxel.on("player_death", function(player, source) end)

-- Movement
voxel.on("player_move", function(player, old_pos, new_pos) end)
    -- Throttled: fires once per tick, not per frame. Only if position actually changed.
voxel.on("player_jump", function(player) end)
voxel.on("player_land", function(player, fall_distance) end)
    -- fall_distance in blocks — useful for fall damage mods
voxel.on("player_sprint_toggle", function(player, is_sprinting) end)
voxel.on("player_sneak_toggle", function(player, is_sneaking) end)

-- Interaction (global, fires BEFORE per-block callbacks — return false to cancel)
voxel.on("player_interact", function(player, action, pos, block_id)
    -- action = "place" | "break" | "rightclick" | "punch"
    -- Return false to cancel the action entirely (per-block callbacks won't fire)
    return true
end)
voxel.on("player_hotbar_changed", function(player, old_slot, new_slot) end)


-- ═══════════════════════════════════════════
-- BLOCK EVENTS (6)
-- ═══════════════════════════════════════════

-- Fires AFTER per-block callbacks have executed
voxel.on("block_placed", function(pos, block_id, player) end)
voxel.on("block_broken", function(pos, old_block_id, player) end)
voxel.on("block_changed", function(pos, old_id, new_id) end)
    -- Fires for ANY block change: player, worldgen, script, ABM, timer, etc.
    -- block_placed/block_broken are subsets (player-initiated only)
voxel.on("block_neighbor_changed", function(pos, neighbor_pos) end)
    -- Global version — useful for mods that watch for cascading changes
voxel.on("block_dig_start", function(player, pos, block_id) end)
    -- Player starts mining — return false to prevent (e.g. protection mods)
voxel.on("block_timer_fired", function(pos, block_id) end)
    -- A block timer expired — useful for debugging/monitoring


-- ═══════════════════════════════════════════
-- WORLD EVENTS (5)
-- ═══════════════════════════════════════════

voxel.on("chunk_loaded", function(chunk_x, chunk_z, from_disk) end)
    -- from_disk = true if loaded from save, false if newly generated
voxel.on("chunk_unloaded", function(chunk_x, chunk_z) end)
voxel.on("chunk_generated", function(chunk_x, chunk_z) end)
    -- First-time generation only (not disk load) — mods can post-process terrain
voxel.on("world_saved", function() end)
    -- After all chunks are serialized to disk
voxel.on("section_meshed", function(chunk_x, chunk_z, section_y) end)
    -- A section mesh was rebuilt — useful for profiling/debugging


-- ═══════════════════════════════════════════
-- TIME EVENTS (3)
-- ═══════════════════════════════════════════

voxel.on("tick", function(dtime) end)
    -- Every simulation tick (20/sec). The heartbeat of the game.
voxel.on("day_phase_changed", function(phase) end)
    -- phase = "dawn" | "day" | "dusk" | "night"
    -- Fires once per transition, not every tick
voxel.on("new_day", function(day_count) end)
    -- A full day/night cycle completed


-- ═══════════════════════════════════════════
-- INPUT EVENTS (10)
-- ═══════════════════════════════════════════

-- Instant events
voxel.on("key_pressed", function(player, key) end)
    -- Raw key name: "w", "a", "1", "escape", etc. Return false to consume.
voxel.on("key_released", function(player, key, hold_duration) end)
    -- hold_duration = seconds the key was held before release.
    -- Example: charge a spell based on how long you held "r"
voxel.on("key_held", function(player, key, hold_duration) end)
    -- Fires EVERY TICK while the key is held. hold_duration = time since press started.
    -- Example: channel a beam for as long as "f" is held
    -- WARNING: fires 20×/sec — keep callback lightweight
voxel.on("key_double_tap", function(player, key) end)
    -- Two presses of the same key within 0.3 seconds. Return false to consume.
    -- Example: double-tap W to toggle sprint, double-tap A/D to dodge

-- Mouse events
voxel.on("mouse_click", function(player, button, pos, block_id) end)
    -- button = "left" | "right" | "middle". pos/block_id nil if not targeting a block.
    -- Fires BEFORE place/break processing. Return false to consume.
voxel.on("mouse_released", function(player, button, hold_duration, pos, block_id) end)
    -- hold_duration = seconds the button was held.
    -- Example: bow charge — right-click hold 0-3 seconds, release fires arrow at proportional speed
voxel.on("mouse_held", function(player, button, hold_duration, pos, block_id) end)
    -- Fires every tick while button is held. pos/block_id = current target (may change as player looks around).
    -- Example: mining progress bar, channeled spells, drawing a bow
    -- Note: for block-specific hold interactions, use on_interact_start/step/stop (Story 9.3) instead
voxel.on("scroll_wheel", function(player, delta) end)
    -- delta = +1 (up) or -1 (down). Return false to prevent hotbar scroll.

-- Combo detection helper (not an event — a registration API)
voxel.register_combo("sprint_dodge", {"w", "w", "a"}, 0.5, function(player)
    -- Fires when player presses W, W, A within 0.5 seconds total
    -- Use for fighting game combos, special moves, etc.
end)

-- Usage examples:
-- Bow charging:
voxel.on("mouse_released", function(player, button, duration, pos, block_id)
    if button == "right" and player:get_held_item() == "base:bow" then
        local power = math.min(duration / 3.0, 1.0) -- 0-3 seconds = 0-100% power
        shoot_arrow(player, power)
    end
end)

-- Sprint by double-tap W:
voxel.on("key_double_tap", function(player, key)
    if key == "w" then
        player:set_sprinting(true)
    end
end)

-- Beacon click pattern: hold right-click for 5 seconds on beacon
voxel.register_block({
    id = "base:beacon",
    on_interact_step = function(pos, player, elapsed)
        if elapsed >= 5.0 then
            activate_beacon(pos, player)
            return false -- stop interaction
        end
        -- Show progress to player (future: HUD API)
        return true
    end,
})


-- ═══════════════════════════════════════════
-- ENGINE EVENTS (3)
-- ═══════════════════════════════════════════

voxel.on("shutdown", function() end)
    -- Engine shutting down — save mod state
voxel.on("hot_reload_start", function() end)
    -- About to reload all scripts — mods save transient state to metadata
voxel.on("hot_reload_complete", function(success, errors) end)
    -- Reload done — errors is a table of {mod_name, error_message} for failed mods


-- ═══════════════════════════════════════════
-- MOD EVENTS (2)
-- ═══════════════════════════════════════════

voxel.on("mod_loaded", function(mod_name) end)
    -- A mod finished loading (during startup or hot-reload)
voxel.on("all_mods_loaded", function() end)
    -- ALL mods loaded — safe to query other mods' blocks/items
```

**Event dispatch rules:**
- Multiple callbacks per event, called in registration order
- Error in one callback doesn't prevent others (logged, not fatal)
- `tick` callbacks that exceed 2ms are logged as warnings (performance monitoring)
- Cancelable events (`player_interact`, `block_dig_start`, `key_pressed`, `mouse_click`, `scroll_wheel`): return false to cancel. First callback to return false wins — subsequent callbacks still fire but the action is blocked
- All per-block callbacks (Stories 9.2–9.7) fire BETWEEN `player_interact` and the global `block_placed`/`block_broken` events. Order: `player_interact` → per-block `can_place`/`on_place`/etc. → `block_placed`
- `player_move` is throttled to tick rate (20/sec), not frame rate — prevents Lua overhead on mouse look

**C++ engine support:**
- Each system that produces events calls `ScriptEngine::fireEvent(name, args...)` at the appropriate point
- EventBus (Story 7.1) already dispatches C++ events — ScriptEngine subscribes to all of them and translates to Lua calls
- Cancelable events: the C++ call checks the Lua return value before proceeding with the action

**Integration test:** Register `player_interact` returning false for "break" action, verify player can't break any block. Register `block_placed` global hook, place a block, verify callback fires with correct pos/id.

---

## Story 9.11: Mod Loading + Hot-Reload

**As a** developer,
**I want** mods loaded from folders and hot-reloadable at runtime,
**so that** content iteration is fast during development.

**Acceptance Criteria:**
- Mod discovery: scan `assets/scripts/mods/` for directories containing `init.lua`
- Load order: `base/` first, then mod directories alphabetically (configurable via `mod-order.json`)
- Each mod gets an isolated Lua environment (no global pollution between mods)
- Mod metadata: optional `mod.json` with `name`, `version`, `description`, `dependencies`
- Hot-reload (F6): destroy all Lua state, clear mod-registered content (keep engine defaults), re-execute all scripts
- After hot-reload: existing world keeps block data (numeric IDs), registry rebuilds string→ID mapping
- Timer state preserved across hot-reload (timers keep running)
- Error in one mod: log error with mod name, skip that mod, continue loading others
- ImGui panel: list loaded mods with status (OK / Error), reload button, per-mod callback count

---

## Summary: Callback Coverage

| Category | Callbacks/APIs | Stories | V1 Status |
|----------|---------------|---------|-----------|
| Placement | 7 | 9.2 | Fully functional |
| Destruction | 10 | 9.2 | +get_experience, on_dig_progress |
| Interaction | 7 | 9.3 | Fully functional (incl. 4-phase hold) |
| Tick / Update | 5 (timer + ABM + LBM) | 9.4 | Fully functional |
| Neighbor + Shapes + Signal | 11 | 9.5 | +on_powered, get_comparator_output, get_push_reaction |
| Entity Interaction | 5 | 9.6 | Player only (stubs for mobs) |
| Metadata / Inventory | 7 | 9.7 | Meta full, inv API without UI |
| World API | 20+ functions | 9.8 | +multiblock patterns, scheduled ticks, settings, block states |
| Visual / Client | 4 | 9.9 | Particles + color tint |
| Global Events | 41 + combo API | 9.10 | 10 input events with hold duration, release, double-tap, combos |

**Total: ~120 callbacks/APIs/events across 11 stories.**
