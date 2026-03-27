# Epic 12 — Block GUI System (Container Screens)

**Priority**: P1
**Dependencies**: Epic 3 (BlockRegistry, ChunkManager), Epic 7 (PlayerController, block interaction), Epic 9 (Lua scripting, inventory callbacks)
**Goal**: A complete, moddable block GUI system where Lua defines container screen layouts declaratively, C++ handles slot interaction state machine and rendering. Supports chests, furnaces, anvils, enchanting tables, beacons, brewing stands, and any modded machine (energy processors, grinders, pipes). Modders provide PNG backgrounds and Lua table definitions — zero C++ needed for new GUIs.

**Architecture decision**: RmlUi as rendering foundation (HTML/CSS layout + sprite sheets + custom elements + Vulkan backend + sol3/Lua bindings), Minecraft's server-authoritative container model (7 click-types, dual-channel slot+property sync), Lua table DSL for mod-facing API.

**Reference**: See `_bmad-output/planning-artifacts/technical-research-gui.md` — Cross-engine GUI analysis (Minecraft, Luanti formspec, Vintage Story, Hytale, Terasology).

---

## Story 12.1: RmlUi Integration + Vulkan Backend

**As a** developer,
**I want** RmlUi integrated with the engine's Vulkan renderer,
**so that** I have a proven layout/styling/text rendering foundation for all GUI screens.

**Acceptance Criteria:**
- RmlUi added to vcpkg dependencies (or vendored if not in vcpkg)
- Custom `RenderInterface` implementation using VoxelForge's Vulkan pipeline:
  - `RenderGeometry()` — batched textured quads via a dedicated GUI VkPipeline (alpha blend, no depth test)
  - `EnableScissorRegion()` / `SetScissorRegion()` — maps to `vkCmdSetScissor`
  - `LoadTexture()` / `GenerateTexture()` — creates VkImage + VkImageView, managed by a `GuiTextureManager`
- Custom `SystemInterface` — provides time via `glfwGetTime()`, logging via spdlog
- Custom `FileInterface` — loads `.rml` and `.rcss` files from `assets/gui/` with mod overlay support (mod files override base files)
- `GuiSystem` class in `engine/include/voxel/gui/GuiSystem.h`:
  - `init(VulkanContext&, Window&)` — creates Rml::Context at window resolution
  - `shutdown()` — destroys context, releases textures
  - `processInput(InputManager&)` — routes mouse/keyboard events to RmlUi
  - `render(VkCommandBuffer)` — called between scene rendering and ImGui debug overlay
  - `onResize(int w, int h)` — updates Rml::Context dimensions
- GUI rendering happens AFTER the 3D scene, BEFORE ImGui debug overlay
- Mouse cursor captured by GUI → game input suppressed (same pattern as ImGui's `WantCaptureMouse`)
- ESC closes any open GUI screen and returns to game
- Unit validation: RmlUi renders a test document (`assets/gui/test.rml`) without Vulkan validation errors

**Technical notes:**
- The Vulkan GUI pipeline uses: alpha blending ON, depth test OFF, dynamic viewport/scissor
- Textures use `VK_FILTER_NEAREST` for pixel-art crispness (not bilinear)
- GUI scaling: `scale_factor = floor(window_height / 320)` for integer scaling that preserves pixel-art edges
- All GUI coordinates in resolution-independent units (1 unit ≈ 18 screen pixels at 1080p)

---

## Story 12.2: Container Model (Server-Side Logic)

**As a** developer,
**I want** a server-authoritative container system that owns slot data and validates all interactions,
**so that** inventory logic is correct, secure, and multiplayer-ready.

**Acceptance Criteria:**

**Container class** (`engine/include/voxel/gui/Container.h`):
```cpp
class Container {
public:
    Container(ContainerType type, int slotCount);
    
    // Slot access
    ItemStack getSlot(int index) const;
    void setSlot(int index, const ItemStack& stack);
    int slotCount() const;
    
    // Integer properties (progress bars, energy levels, etc.)
    int16_t getProperty(int index) const;
    void setProperty(int index, int16_t value);
    int propertyCount() const;
    
    // Dirty tracking for sync
    bool isSlotDirty(int index) const;
    bool isPropertyDirty(int index) const;
    void clearDirtyFlags();
    
    // Lifecycle
    void open(/* player ref */);
    void close();
    bool isOpen() const;
};
```

**Slot class** with validation hooks:
```cpp
struct SlotDefinition {
    int index;
    std::string inventoryName;     // "input", "fuel", "output", "player_main"
    
    // Validation callbacks (set from Lua)
    sol::function filterFn;        // (ItemStack) → bool — can this item go here?
    sol::function onTakeFn;        // (player, ItemStack) → void — side effects on removal
    sol::function maxStackFn;      // (ItemStack) → int — override max stack size
    
    bool outputOnly = false;       // Take-only (crafting result, furnace output)
    bool ghostSlot = false;        // Display-only, no real item
};
```

**ContainerType registration from Lua:**
```lua
voxel.register_container("base:furnace", {
    slots = {
        { name = "input",  count = 1, filter = function(item) return voxel.has_recipe("smelting", item) end },
        { name = "fuel",   count = 1, filter = function(item) return item:burn_time() > 0 end },
        { name = "output", count = 1, output_only = true,
          on_take = function(player, item) player:award_xp(item:smelt_xp()) end },
    },
    properties = { "burn_time", "burn_duration", "cook_progress", "cook_total" },
    
    on_open = function(pos, player) end,
    on_close = function(pos, player) end,
    tick = function(pos, container, dt) 
        -- furnace logic: burn fuel, cook items, update properties
    end,
})
```

- Container instances created when a player opens a block GUI (`on_rightclick` → `voxel.open_container(player, pos, "base:furnace")`)
- Container destroyed when player closes GUI or moves too far from block
- `Container::tick()` called every simulation tick for active containers (furnace cooking, machine processing)
- All slot modifications go through `Container` — never direct array access
- Unit tests: slot set/get, dirty tracking, property sync, filter validation

---

## Story 12.3: Slot Interaction State Machine (7 Click Types)

**As a** developer,
**I want** a complete slot interaction handler matching Minecraft's 7 click-type system,
**so that** inventory manipulation feels identical to Minecraft.

**Acceptance Criteria:**

**SlotInteractionHandler class** (`engine/include/voxel/gui/SlotInteractionHandler.h`):

**PICKUP (left/right click on slot):**
- Left-click empty cursor + slot with item → pick up full stack
- Left-click cursor holding + empty slot → place full stack
- Left-click cursor holding + same-type slot → merge up to max stack
- Left-click cursor holding + different-type slot → swap
- Right-click empty cursor + slot with item → pick up ceil(count/2)
- Right-click cursor holding + compatible slot → place exactly 1
- Click outside window (slotId == -1): left drops all, right drops 1

**QUICK_MOVE (shift-click):**
- `quickMoveStack(slotIndex)` uses shift-click routing rules defined per container
- First pass: merge into existing partial stacks of same type in target range
- Second pass: fill empty slots in target range
- Each container defines routes: `{ from = "input", to = "player_main" }`

**QUICK_CRAFT (drag-painting) — 3-phase protocol:**
- Phase 0 (mouse down + drag begins): record drag type (left=distribute, right=place-one)
- Phase 1 (mouse moves over slots): accumulate slot set
- Phase 2 (mouse up): distribute items across accumulated slots
  - Left-click: each slot gets `floor(cursor_count / num_slots)`
  - Right-click: each slot gets exactly 1
  - Remaining items stay on cursor

**PICKUP_ALL (double-click while holding):**
- Collect all matching items from all visible slots into cursor, up to max stack
- Two passes: non-full stacks first, then full stacks
- Output slots excluded

**SWAP (number keys 1-9):**
- Swap hovered slot with hotbar slot N
- Respects filter validation on both ends

**CLONE (middle-click, creative mode only):**
- Copy item from slot to cursor at max stack size

**THROW (Q key while hovering):**
- Drop 1 item from hovered slot (Ctrl+Q drops full stack)

- All click types implemented in C++ (not Lua) — latency-sensitive
- Every action validates against `SlotDefinition::filterFn` and `outputOnly` flag
- Cursor item state (`m_cursorStack`) tracked in handler
- Unit tests: every click type + edge cases (empty slots, full stacks, filter rejection, output slots)

---

## Story 12.4: Lua GUI DSL + Widget Tree Builder

**As a** developer,
**I want** Lua table definitions parsed into a widget tree,
**so that** mods define GUI layouts declaratively without C++.

**Acceptance Criteria:**

**Lua DSL API — the mod-facing interface:**
```lua
local gui = voxel.gui

voxel.register_gui("base:chest", function(container)
    return gui.screen {
        title = "Chest",
        width = 11, height = 10,
        background = "gui/chest.png",
        
        gui.slot_grid {
            x = 0.5, y = 0.5, columns = 9, rows = 3,
            inventory = "main",
        },
        
        gui.player_inventory { x = 0.5, y = 5 },
        
        gui.shift_click_rules {
            { from = "main", to = "player_main" },
            { from = "player_main", to = "main" },
        },
    }
end)
```

**Widget types (18 total):**

| Widget | Description | Key properties |
|--------|------------|----------------|
| `screen` | Root container | title, width, height, background |
| `slot` | Single inventory slot | inventory, index, filter, output_only |
| `output_slot` | Take-only slot | inventory, index, on_take |
| `ghost_slot` | Display-only hint | display_item, tooltip |
| `slot_grid` | N×M slot grid | inventory, columns, rows, output_only |
| `player_inventory` | Standard 9×3 + hotbar | x, y (position only) |
| `progress_bar` | Partial texture fill | value, max_value, direction, bg_texture, fill_texture |
| `vertical_bar` | Energy/fluid bar | value, max_value, fill_texture, tooltip |
| `fluid_tank` | Liquid display | amount, capacity, fluid_type |
| `button` | Clickable action | texture, text, on_click |
| `text_button` | Button with dynamic text | text_source, on_click |
| `label` | Static/dynamic text | text or text_source, color |
| `text_field` | Text input | placeholder, max_length, on_changed |
| `dropdown` | Selection list | options, selected, on_change |
| `toggle` | Boolean switch | label, value, on_toggle |
| `tab_bar` | Tab navigation | tabs (id, icon), side |
| `panel` | Conditional container | tab (visible when tab active), visible_if |
| `scrollable_grid` | Scrollable item grid | columns, visible_rows, items_source, on_click |
| `image` | Static/dynamic image | texture, visible_if |
| `shift_click_rules` | Routing declaration | from/to inventory names |

**C++ widget tree construction:**
- `GuiBuilder` class receives the Lua table via sol2
- Recursively parses the table, creates corresponding C++ widget nodes
- Each widget maps to an RmlUi custom element (or standard element for text/buttons)
- Data-bound properties link to `ContainerView` via dirty flag observation
- `gui.screen` → RmlUi `Rml::Document` with loaded `.rcss` stylesheet
- Build function called ONCE on container open — not every frame

**Error handling:**
- Unknown widget type → log warning, skip element
- Missing required property → log error with widget ID, use default
- Invalid inventory reference → log error, slot renders as empty/locked

---

## Story 12.5: GUI Renderer + Item Icon Pipeline

**As a** developer,
**I want** GUI screens rendered with pixel-perfect item icons and smooth progress animations,
**so that** container screens look polished and responsive.

**Acceptance Criteria:**

**Background rendering:**
- PNG backgrounds loaded as RmlUi textures via `@spritesheet` declarations
- 256×256 atlas convention (matching Minecraft)
- Nine-slice support for resizable panels via RmlUi decorators
- `VK_FILTER_NEAREST` for all GUI textures (pixel-art)

**Slot rendering (custom RmlUi element `<inventory-slot>`):**
- Empty slot: background sprite (18×18 px dark square with border)
- Slot with item: background + item icon centered + stack count text (bottom-right, white with shadow)
- Hovered slot: brighter background sprite + tooltip trigger
- Slot highlight during drag: semi-transparent white overlay

**Item icon rendering:**
- For block items: render the block's 3D model from an isometric angle into a small render target, cache as texture
- For flat items (tools, food): render the 2D sprite directly
- Icon cache: `ItemIconCache` class — generates icons on first request, stores as `VkImage` per item type
- Cache invalidated on hot-reload (F6)

**Progress bar rendering:**
- Partial UV fill: given value/max ratio, crop the fill texture UV coordinates
- Direction support: right (arrow), up (flame), down, left
- Flame example: 14×14 px texture, fill from bottom — `v_start = v_bottom - (ratio * height)`
- Arrow example: 24×17 px texture, fill from left — `u_end = u_left + (ratio * width)`
- Client-side interpolation between server ticks: `display = lerp(display, target, 1 - exp(-12 * dt))` for smooth visual

**Cursor item rendering:**
- Rendered at highest z-order, follows mouse position
- Item icon + stack count, offset by (-8, -8) from cursor
- Drawn AFTER all RmlUi rendering, BEFORE ImGui debug

**Tooltip rendering:**
- Appears after 250ms hover delay
- Item tooltip: name (white, bold), type (gray, italic), damage/durability if applicable, enchantments, lore text
- Custom tooltip: widget provides `tooltip` callback → string or table of lines
- Positioned above cursor, clamped to screen bounds
- Semi-transparent dark background with 1px border

**GUI scaling:**
- `scale_factor = max(1, floor(window_height / 320))`
- All coordinates multiplied by scale_factor
- Recalculated on window resize
- Integer scaling only — no fractional scaling (preserves pixel-art)

---

## Story 12.6: Data Binding + Container Sync

**As a** developer,
**I want** GUI widgets automatically updated when container data changes,
**so that** progress bars, slot contents, and labels stay in sync without per-frame Lua calls.

**Acceptance Criteria:**

**ContainerView (client-side mirror):**
```cpp
class ContainerView {
    std::vector<ItemStack> m_slots;
    std::vector<int16_t> m_properties;
    std::bitset<64> m_dirtySlots;
    std::bitset<16> m_dirtyProperties;
    
    // Called when server sends sync data
    void onSyncPacket(const std::vector<SlotUpdate>& slots, const std::vector<PropertyUpdate>& props);
    
    // Called once per frame before rendering
    void flushToWidgets(GuiDocument& doc);
};
```

**Property binding in Lua DSL:**
```lua
gui.progress_bar {
    value = container:prop("cook_progress"),      -- binds to property index
    max_value = container:prop("cook_total"),
}

gui.label {
    text_source = function(container)             -- dynamic text callback
        return string.format("Energy: %d FE", container:prop("energy"))
    end,
}
```

**Dirty flag update cycle:**
1. Container server-side changes slot/property → marks dirty
2. Sync packet sent with only dirty entries (delta compression)
3. `ContainerView::onSyncPacket()` updates local mirrors, sets dirty bits
4. `ContainerView::flushToWidgets()` iterates dirty bits, updates only affected RmlUi elements
5. Clear dirty flags
6. V1 (singleplayer): sync is direct function call, no network serialization yet. But the interface uses packets so multiplayer is a swap, not a rewrite.

**Progress interpolation:**
- Properties like cook_progress update discretely (every server tick = 50ms)
- Visual display interpolates: `displayValue += (targetValue - displayValue) * (1 - exp(-speed * dt))`
- Speed = 12.0 → 95% converged in ~250ms (smooth but responsive)

**Conditional visibility:**
```lua
gui.panel {
    visible_if = function(container)
        return container:prop("power_level") > 0
    end,
}
```
- `visible_if` re-evaluated every frame (fast Lua call, cached result)
- Widget hidden via RmlUi `display: none` (no layout impact when hidden)

---

## Story 12.7: Furnace GUI (Reference Implementation)

**As a** developer,
**I want** a complete furnace GUI as the reference implementation,
**so that** the entire pipeline (container registration, Lua DSL, rendering, interaction) is validated end-to-end.

**Acceptance Criteria:**

**Server-side furnace logic (in Lua):**
```lua
voxel.register_container("base:furnace", {
    slots = {
        { name = "input",  count = 1, filter = function(item) return voxel.has_recipe("smelting", item) end },
        { name = "fuel",   count = 1, filter = function(item) return item:burn_time() > 0 end },
        { name = "output", count = 1, output_only = true },
    },
    properties = { "burn_time", "burn_duration", "cook_progress", "cook_total" },
    
    tick = function(pos, container, dt)
        local input = container:get_slot("input", 0)
        local fuel = container:get_slot("fuel", 0)
        local output = container:get_slot("output", 0)
        
        -- Burn fuel
        local burn = container:get_prop("burn_time")
        if burn > 0 then
            container:set_prop("burn_time", burn - 1)
        elseif not input:is_empty() and not fuel:is_empty() then
            local fuel_time = fuel:item():burn_time()
            container:set_prop("burn_time", fuel_time)
            container:set_prop("burn_duration", fuel_time)
            fuel:take(1)
        end
        
        -- Cook item
        if burn > 0 and not input:is_empty() then
            local progress = container:get_prop("cook_progress") + 1
            local total = 200  -- 10 seconds at 20 ticks/sec
            container:set_prop("cook_progress", progress)
            container:set_prop("cook_total", total)
            if progress >= total then
                local result = voxel.get_recipe_output("smelting", input:item())
                if output:can_merge(result) then
                    output:add(result)
                    input:take(1)
                end
                container:set_prop("cook_progress", 0)
            end
        else
            container:set_prop("cook_progress", 0)
        end
    end,
})
```

**Client-side furnace GUI (in Lua):**
```lua
voxel.register_gui("base:furnace", function(container)
    return gui.screen {
        title = "Furnace",
        width = 11, height = 10.5,
        background = "gui/furnace.png",
        
        gui.slot { x = 3.5, y = 0.5, inventory = "input", index = 0 },
        gui.slot { x = 3.5, y = 2.5, inventory = "fuel", index = 0 },
        gui.output_slot { x = 7.5, y = 1.5, inventory = "output", index = 0 },
        
        gui.progress_bar {
            x = 3.5, y = 1.5, w = 1, h = 1,
            direction = "up",
            value = container:prop("burn_time"),
            max_value = container:prop("burn_duration"),
            bg_texture = "gui/flame_bg.png",
            fill_texture = "gui/flame_fill.png",
        },
        
        gui.progress_bar {
            x = 5, y = 1.5, w = 1.5, h = 1,
            direction = "right",
            value = container:prop("cook_progress"),
            max_value = container:prop("cook_total"),
            bg_texture = "gui/arrow_bg.png",
            fill_texture = "gui/arrow_fill.png",
        },
        
        gui.player_inventory { x = 0.5, y = 5.5 },
        
        gui.shift_click_rules {
            { from = "input", to = "player_main" },
            { from = "fuel", to = "player_main" },
            { from = "output", to = "player_main" },
            { from = "player_main", to = "input" },
            { from = "player_hotbar", to = "input" },
        },
    }
end)
```

**Wiring into block interaction:**
```lua
voxel.register_block({
    id = "base:furnace",
    on_rightclick = function(pos, node, player)
        voxel.open_container(player, pos, "base:furnace")
    end,
    on_construct = function(pos)
        local meta = voxel.get_meta(pos)
        meta:set_int("burn_time", 0)
        meta:set_int("burn_duration", 0)
        meta:set_int("cook_progress", 0)
        meta:set_int("cook_total", 0)
    end,
})
```

**End-to-end validation:**
- Player right-clicks furnace → GUI opens with background PNG, 3 slots, 2 progress indicators
- Place coal in fuel slot → flame progress bar animates upward
- Place raw iron in input slot → arrow progress bar animates rightward
- When cook completes → iron ingot appears in output slot, input consumed
- Shift-click iron ingot from output → moves to player inventory
- Drag-paint coal across fuel slot → places exactly 1 per slot
- ESC closes GUI → furnace continues ticking (container persists while chunk loaded)
- All interactions match Minecraft feel

---

## Story 12.8: Chest, Crafting Table, Anvil GUIs

**As a** developer,
**I want** chest (27-slot storage), crafting table (3×3 grid + result), and anvil (rename + combine) GUIs,
**so that** the 3 most common container types work out of the box.

**Acceptance Criteria:**

**Chest:**
- 27 slots (3 rows × 9 columns) + player inventory
- No special logic (pure storage)
- Shift-click: chest → player, player → chest
- Double chest support: 54 slots (6 rows × 9), two containers linked

**Crafting Table:**
- 3×3 input grid + 1 result slot
- Result slot: `outputOnly = true`, `on_take` consumes grid ingredients
- Recipe lookup: `voxel.get_crafting_result(grid_contents) → ItemStack`
- Grid clears when GUI closes (items return to player inventory or drop)
- Shift-click on result: craft maximum possible (repeat until input exhausted or inventory full)

**Anvil:**
- 2 input slots + 1 output slot + TextField for rename
- TextField: `on_changed` recalculates output and XP cost
- Output slot: `on_take` deducts XP, applies rename/combine
- Level cost displayed as green label (or red if insufficient)
- Combines: tool + tool = repaired tool, item + enchanted book = enchanted item

**Each GUI includes:**
- PNG background texture (256×256 atlas)
- Lua container registration with slots + validation
- Lua GUI definition with layout
- Shift-click rules
- Integration test: place items, shift-click, drag-paint, verify results

---

## Story 12.9: Advanced Widgets (Beacon, Tabs, Scroll)

**As a** developer,
**I want** radio buttons, tabs, scrollable grids, and conditional panels,
**so that** complex GUIs like beacon, creative inventory, and modded machines work.

**Acceptance Criteria:**

**Beacon GUI:**
```lua
voxel.register_gui("base:beacon", function(container)
    return gui.screen {
        title = "Beacon",
        width = 14, height = 13,
        background = "gui/beacon.png",
        
        gui.slot { x = 10, y = 6.5, inventory = "payment", index = 0 },
        
        gui.radio_group {
            id = "primary_effect",
            selected = container:prop("primary_effect"),
            on_changed = function(player, value)
                container:send_action("set_primary", value)
            end,
            options = {
                { id = "speed",      x = 4.5, y = 1.5, w = 1.25, h = 1.25, texture = "gui/effect_speed.png" },
                { id = "haste",      x = 6,   y = 1.5, w = 1.25, h = 1.25, texture = "gui/effect_haste.png" },
                { id = "resistance", x = 4.5, y = 3,   w = 1.25, h = 1.25, texture = "gui/effect_resistance.png" },
                { id = "jump",       x = 6,   y = 3,   w = 1.25, h = 1.25, texture = "gui/effect_jump.png" },
            },
        },
        
        gui.button {
            x = 10, y = 8.5, w = 2, h = 1,
            texture = "gui/beacon_confirm.png",
            visible_if = function(c) return c:prop("power_level") > 0 end,
            on_click = function(player) container:send_action("activate") end,
        },
        
        gui.player_inventory { x = 1, y = 8 },
    }
end)
```

**TabBar + Panel:**
```lua
gui.tab_bar {
    x = -1.5, y = 0, side = "left",
    tabs = {
        { id = "main",   icon = "gui/tab_main.png" },
        { id = "config", icon = "gui/tab_config.png" },
    },
},
gui.panel { tab = "main",
    gui.slot_grid { ... },
},
gui.panel { tab = "config",
    gui.toggle { label = "Auto-eject", value = container:prop("auto_eject"), ... },
    gui.dropdown { options = {"None","Up","Down","North","South","East","West"}, ... },
    gui.number_input { label = "Threshold", min = 0, max = 15, ... },
},
```

**ScrollableGrid (creative inventory, recipe browser):**
```lua
gui.scrollable_grid {
    x = 0.5, y = 1, columns = 9, visible_rows = 5,
    slot_size = 1,
    items_source = "gui:creative_items",
    on_click = function(player, item_id)
        player:give_item(item_id, 64)
    end,
    search = true,  -- shows search bar above grid
},
```

**Dropdown:**
- Click opens list below, click option selects and closes
- `on_change(index)` callback fires
- Renders as RmlUi `<select>` custom element

**NumberInput:**
- Integer input with +/- buttons and direct text entry
- Clamped to min/max range
- `on_change(value)` callback fires

**FluidTank display:**
```lua
gui.fluid_tank {
    x = 11, y = 0.5, w = 1, h = 4,
    amount = container:prop("fluid_amount"),
    capacity = container:prop("fluid_capacity"),
    fluid_type = container:prop("fluid_type"),  -- indexes a fluid texture table
    tooltip = function(amount, cap, type)
        return string.format("%d / %d mB %s", amount, cap, type)
    end,
},
```

---

## Story 12.10: Container Actions + Sound Integration

**As a** developer,
**I want** custom action packets from GUI to server and sound feedback on interactions,
**so that** modded machines can have buttons/toggles that affect game state, and the GUI feels responsive.

**Acceptance Criteria:**

**Container actions (GUI → server):**
```lua
-- In GUI definition:
gui.button {
    on_click = function(player)
        container:send_action("toggle_mode", { mode = "fast" })
    end,
}

-- In container registration:
voxel.register_container("techmod:processor", {
    on_action = function(pos, player, action, data)
        if action == "toggle_mode" then
            local meta = voxel.get_meta(pos)
            meta:set_string("mode", data.mode)
        elseif action == "set_threshold" then
            -- validate and apply
        end
    end,
})
```

- `container:send_action(name, data_table)` — serializes action + data, sends to server
- Server calls `on_action` callback with deserialized data
- V1 (singleplayer): direct function call, no network packet
- Action names are strings — mods define their own without engine changes
- Data table: flat key-value pairs (string, number, boolean)
- Validation: server ALWAYS validates (never trust client data)

**Sound integration:**
- Default sounds (no Lua needed):
  - `ui.slot_pickup` — when item picked up from slot
  - `ui.slot_place` — when item placed in slot
  - `ui.button_click` — when button pressed
- Per-widget override:
  ```lua
  gui.button { on_click = ..., click_sound = "mymod:lever_click" }
  gui.output_slot { ..., take_sound = "base:craft_complete" }
  ```
- Sound plays client-side immediately (no server round-trip for audio feedback)
- V1: sounds are stubs (fields exist, audio system from future epic plays them). The API is defined now so mods don't need to change when audio lands.

---

## Story 12.11: GUI Styling + Sprite Sheet System

**As a** developer,
**I want** a CSS-like styling system with sprite sheet atlas support,
**so that** mods can customize GUI appearance via texture packs and stylesheets.

**Acceptance Criteria:**

**Base stylesheet** (`assets/gui/base.rcss`):
```css
@spritesheet gui-widgets {
    src: gui/widgets.png;
    resolution: 256x 256x;
    
    slot-bg:         0px  0px  18px 18px;
    slot-bg-hover:   18px 0px  18px 18px;
    slot-bg-locked:  36px 0px  18px 18px;
    button-normal:   0px  18px 200px 20px;
    button-hover:    0px  38px 200px 20px;
    button-pressed:  0px  58px 200px 20px;
    tab-active:      0px  78px 28px 32px;
    tab-inactive:    28px 78px 28px 32px;
    tooltip-bg:      0px  110px 1px 1px;
    scrollbar-track: 56px 78px 12px 15px;
    scrollbar-thumb: 68px 78px 12px 15px;
}

inventory-slot {
    width: 18dp;
    height: 18dp;
    decorator: image(slot-bg);
}
inventory-slot:hover {
    decorator: image(slot-bg-hover);
}

.container-bg {
    decorator: ninepatch(container-bg, 4px 4px 4px 4px);
}

.tooltip {
    background-color: #100010F0;
    border: 1px #6028B0;
    padding: 4dp;
    font-size: 12dp;
    color: white;
}
```

**Mod style override** (`assets/scripts/mods/mymod/gui/override.rcss`):
- Mods can provide `.rcss` files that override base styles
- Loaded after base stylesheet (CSS cascade applies)
- Texture packs can replace `gui/widgets.png` to restyle all GUIs at once

**Font rendering:**
- Minecraft-style bitmap font: 8×8 pixel characters from `gui/font.png`
- Loaded as RmlUi bitmap font (`@font-face` with `font-weight`, `font-style`)
- Color codes: `§c` = red, `§a` = green, `§e` = yellow, `§f` = white, `§l` = bold, `§o` = italic
- Parsed in C++ before passing text to RmlUi (strip `§` codes, apply RmlUi `<span style="color:...">`)

**Required texture assets (`assets/gui/`):**
- `widgets.png` — sprite atlas (256×256) with all shared UI elements
- `font.png` — bitmap font atlas (128×128)
- `furnace.png` — furnace background (176×166 logical, in 256×256 atlas)
- `chest.png` — chest background
- `crafting_table.png` — crafting table background
- `anvil.png` — anvil background
- `beacon.png` — beacon background
- Progress textures: `flame_bg.png`, `flame_fill.png`, `arrow_bg.png`, `arrow_fill.png`
- All textures 16×16 or standard GUI sizes, `VK_FILTER_NEAREST`

---

## Story 12.12: Integration + Lifecycle Management

**As a** developer,
**I want** the GUI system wired into GameApp with proper lifecycle management,
**so that** opening/closing containers, input focus, and cleanup all work correctly.

**Acceptance Criteria:**

**Opening a container GUI:**
1. Player right-clicks block with `on_rightclick` callback
2. Callback calls `voxel.open_container(player, pos, "base:furnace")`
3. Engine creates `Container` instance, initializes slots from block metadata
4. Engine calls Lua build function → gets widget tree definition
5. `GuiBuilder` constructs RmlUi document from widget tree
6. `GuiSystem` displays document, captures input
7. Game input suppressed (no movement, no block interaction — only GUI interaction)
8. Cursor released from FPS capture, shown as normal arrow

**Closing:**
1. ESC key, or inventory key (E), or player moves >8 blocks from container block
2. Container items in temporary slots (crafting grid) returned to player or dropped
3. RmlUi document destroyed, widget tree freed
4. Container instance destroyed (server-side)
5. Cursor recaptured for FPS mode
6. Game input resumed

**Multiple containers:**
- Only one container GUI open at a time
- Opening a new one closes the current one first

**Input routing when GUI open:**
- Mouse: routed to `GuiSystem::processInput()` → RmlUi event dispatch → widget handlers
- Keyboard: ESC → close, E → close, number keys → slot swap, Q → throw, text fields capture all other keys
- Movement keys (WASD, Space, Shift): blocked while GUI open
- Camera mouse look: disabled while GUI open

**Pause behavior:**
- GUI does NOT pause the game (furnaces keep cooking, world keeps ticking)
- Container `tick()` keeps running while GUI is open

**Hot-reload (F6):**
- GUI definitions are part of Lua state
- On hot-reload: close all open GUIs, reload scripts, GUI definitions refreshed
- Reopening a GUI uses the new definition

**Unit tests:**
- Open/close lifecycle: container created and destroyed correctly
- Input routing: mouse events reach widgets, game input blocked
- Distance close: moving away closes GUI
- Crafting grid cleanup: items returned on close

---

## Summary

| Story | Scope | Key deliverables |
|-------|-------|-----------------|
| 12.1 | RmlUi + Vulkan | RenderInterface, SystemInterface, FileInterface, GuiSystem class |
| 12.2 | Container model | Container class, SlotDefinition, Lua registration API, tick system |
| 12.3 | Slot interaction | 7 click-type state machine, drag-painting protocol, cursor item tracking |
| 12.4 | Lua DSL | 18 widget types, GuiBuilder, table→widget tree parsing |
| 12.5 | Rendering | Background PNGs, item icons, progress bars, tooltips, GUI scaling |
| 12.6 | Data binding | ContainerView, dirty flags, property interpolation, conditional visibility |
| 12.7 | Furnace (reference) | Complete end-to-end: registration, GUI, tick logic, all interactions |
| 12.8 | Chest + Crafting + Anvil | 3 core container types with full interaction |
| 12.9 | Advanced widgets | RadioGroup, TabBar, ScrollableGrid, FluidTank, Dropdown, NumberInput |
| 12.10 | Actions + Sound | Container actions API, sound hooks (stubs until audio epic) |
| 12.11 | Styling | RCSS sprites, bitmap font, color codes, mod style overrides |
| 12.12 | Integration | Lifecycle, input routing, hot-reload, distance close, cleanup |
