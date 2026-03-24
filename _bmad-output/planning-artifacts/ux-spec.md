# UX Specification — VoxelForge Engine

> Phase 2 planning artifact. Defines player-facing interaction design, controls, HUD layout, and debug UI.
> Reference for implementation agents during Epic 2 (camera/ImGui), Epic 7 (player interaction).

---

## 1. Target Experience

The player experience should feel immediately familiar to any Minecraft player. Controls, movement physics, and block interaction must match expectations — this is not the place to innovate. The debug UI is developer-facing and should be information-dense without cluttering gameplay.

---

## 2. Control Scheme

### Keyboard & Mouse (Primary)

| Action | Default Binding | Notes |
|--------|----------------|-------|
| Move forward | W | Hold for continuous movement |
| Move backward | S | |
| Strafe left | A | |
| Strafe right | D | |
| Jump | Space | Only when on ground |
| Sneak / crouch | Left Shift (hold) | Prevents falling off edges, slows speed to 0.3× |
| Sprint | Left Ctrl (toggle) or double-tap W | 1.3× walk speed, cancels on collision or sneak |
| Break block | Left Mouse Button | Hold for continuous breaking (V1: instant break) |
| Place block | Right Mouse Button | Places at targeted face |
| Pick block | Middle Mouse Button | Selects block type from world (future: copies to hotbar) |
| Hotbar slot 1–9 | Keys 1–9 | Direct selection |
| Hotbar scroll | Mouse scroll wheel | Cycles through hotbar slots |
| Toggle debug overlay | F3 | Dear ImGui overlay |
| Toggle wireframe | F4 | Wireframe rendering mode |
| Toggle chunk borders | F5 | Chunk boundary visualization |
| Hot-reload scripts | F6 | Re-execute all Lua mods |
| Toggle mouse capture | Escape | Release cursor; click game window to recapture |
| Quit | Alt+F4 or window close | Save modified chunks, clean shutdown |

### Mouse Sensitivity

- Default: 0.1 degrees per pixel of raw mouse movement
- Configurable via ImGui debug panel (slider 0.01–0.5)
- No mouse acceleration — raw input only
- Pitch clamped to ±89° (prevents gimbal lock at poles)
- Yaw wraps 0–360°

---

## 3. Movement Feel

### Target Values (Minecraft Reference)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Walk speed | 4.317 m/s | Base horizontal speed |
| Sprint speed | 5.612 m/s | 1.3× walk |
| Sneak speed | 1.295 m/s | 0.3× walk |
| Jump velocity | ~8.0 m/s initial upward | Reaches ~1.25 blocks height |
| Gravity | 28.0 m/s² | Minecraft-like, not real 9.81 |
| Terminal velocity | ~78.4 m/s | Limits fall speed |
| Air control | 0.02× ground acceleration | Minimal air steering |
| Eye height | 1.62 blocks above feet | Camera position offset |
| Player hitbox | 0.6 × 1.8 × 0.6 blocks | Width × Height × Depth |

### Movement Behavior

- **Ground movement**: instant acceleration to max speed (no ramp-up for V1, like Minecraft)
- **Air movement**: greatly reduced horizontal control, maintains momentum
- **Sneak edge detection**: when sneaking, player position clamped to prevent walking off block edges (check 0.3 blocks ahead for ground)
- **Step-up**: automatically step onto blocks 1 high while walking (no jump needed for single steps)
- **Landing**: no fall damage in V1, but OnGround flag set on landing

---

## 4. Block Interaction

### Targeting

- Raycast from camera center (screen crosshair) into world
- Max reach: 6 blocks distance
- Update every frame (not just on tick) for responsive feel
- Highlight: wireframe overlay on targeted block face (1-pixel white outline with slight depth offset to prevent z-fighting)

### Breaking

- Left click on targeted block → instant break (V1, no break animation)
- Block removed → drop item at block center (future: item entity with velocity)
- Adjacent chunks marked dirty if block was on section boundary
- Light update triggered, remesh queued

### Placing

- Right click → place block at the face the player is looking at
- Placement blocked if:
  - Target position overlaps player AABB (can't place inside yourself)
  - Target position has no adjacent solid block (can't place in mid-air) — except if targeting an existing face
- Block placed → light update, remesh queued, Lua event fired

### Hotbar

- 9 slots displayed at bottom center of screen
- V1: hardcoded block selection (stone, dirt, grass, sand, wood, leaves, glass, torch, cobblestone)
- Selected slot highlighted with brighter border
- Scroll wheel cycles left/right through slots
- Number keys 1–9 for direct selection

---

## 5. HUD Layout

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│                                                          │
│                                                          │
│                         +                                │  ← Crosshair (center)
│                                                          │
│                                                          │
│                                                          │
│                                                          │
│                                                          │
│   ┌─┬─┬─┬─┬─┬─┬─┬─┬─┐                                  │
│   │1│2│3│4│5│6│7│8│9│                                    │  ← Hotbar (bottom center)
│   └─┴─┴─┴─┴─┴─┴─┴─┴─┘                                  │
└──────────────────────────────────────────────────────────┘
```

### Crosshair

- Simple white `+` symbol, 2px lines, 10px each arm
- Centered on screen
- Always visible (even in menus — cursor replaces crosshair when mouse released)

### Hotbar

- 9 equally sized slots, 48×48 pixels each, 4px gap between slots
- Background: semi-transparent dark gray (rgba 0,0,0,0.6)
- Selected slot: brighter border (white 2px), slightly larger (52×52)
- Block texture rendered inside each slot (from texture array)
- Position: bottom center, 16px above screen bottom edge

---

## 6. Debug Overlay (F3)

### Layout

```
┌──────────────────────────────────────────────────────────┐
│ VoxelForge v0.1.0                                        │
│ FPS: 144 (6.9ms) | TPS: 20                              │
│                                                          │
│ XYZ: 127.34 / 72.00 / -56.12                            │
│ Chunk: 7, -4 | Section: 4                                │
│ Facing: North (yaw: 180.0, pitch: -12.5)                 │
│ On ground: yes | Sprint: no                              │
│                                                          │
│ Render distance: 16 chunks                               │
│ Chunks loaded: 1089 | Meshed: 1024 | Drawn: 487          │
│ Draw calls: 1 (indirect) | Quads: 2,340,128              │
│ Gigabuffer: 124 / 256 MB (48%)                           │
│ Mesh queue: 12 pending | Upload queue: 3 pending          │
│                                                          │
│ Block light: 0 | Sky light: 15                           │
│ Biome: Plains | Time: 12:00 (Day)                        │
│                                                          │
│ GPU: NVIDIA GeForce GTX 1660                             │
│ VRAM: 312 / 6144 MB                                      │
│                                                          │
│ [F4] Wireframe  [F5] Chunk borders  [F6] Hot-reload      │
└──────────────────────────────────────────────────────────┘
```

### Implementation

- Dear ImGui window, top-left corner, semi-transparent background
- Monospace font (default ImGui font is acceptable)
- Updated every frame for FPS/position, every second for slower metrics (chunk counts, memory)
- Collapsible sections (click header to expand/collapse)
- Does NOT capture mouse — overlay only, game still receives input underneath

### Chunk Visualization (F5)

When enabled:
- Render translucent wireframe cubes at chunk column boundaries (green lines)
- Section boundaries within columns (dimmer green lines)
- Dirty sections highlighted in orange wireframe
- Currently meshing sections highlighted in yellow wireframe
- Only render boundaries within 4 chunks of player (performance)

### Wireframe Mode (F4)

- Toggle polygon mode to `VK_POLYGON_MODE_LINE` for all chunk rendering
- Useful for inspecting mesh density, greedy merging effectiveness
- ImGui checkbox mirrors F4 state

---

## 7. Camera Behavior

### First-Person Camera

- Position: player position + eye height offset (0, 1.62, 0)
- No head bob in V1 (can be added later via Lua)
- FOV: 70° default, configurable 50–110° via ImGui slider
- Near plane: 0.1 blocks
- Far plane: render distance × 16 + 64 blocks
- Aspect ratio: auto from window size, updated on resize

### Mouse Look

- Raw mouse input (GLFW `GLFW_RAW_MOUSE_MOTION` if available)
- Yaw: horizontal mouse movement → rotation around Y axis (left/right)
- Pitch: vertical mouse movement → rotation around X axis (up/down), clamped ±89°
- No roll
- Smooth: no interpolation needed — direct mapping feels most responsive

---

## 8. Visual Feedback

### Block Highlight

- Targeted block shows wireframe outline on the face being looked at
- Color: white with slight transparency
- Line width: 2px (or thinnest available in Vulkan)
- Depth: rendered with slight offset toward camera to prevent z-fighting
- Updates every frame following raycast result

### Crosshair Color

- White by default
- V1: no color change (future: could indicate interactable blocks)

### Sky

- V1: solid color gradient (light blue top, lighter at horizon)
- Changes with day/night cycle (dark blue at night, warm orange at sunset — simple lerp)
- No clouds, no sun/moon geometry in V1

---

## 9. Accessibility Notes (Future)

Not implemented in V1 but designed to not prevent later addition:
- All keybindings should be remappable (store in config file, not hardcoded)
- Mouse sensitivity configurable
- FOV configurable (important for motion sickness)
- UI scale factor (ImGui has built-in support)
- Color-blind modes for block highlight and debug overlay colors
