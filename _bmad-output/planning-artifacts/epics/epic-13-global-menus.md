# Epic 13 — Global Menus (Main Menu, Pause, Options)

**Priority**: P2
**Dependencies**: Epic 12 Story 12.1 (RmlUi + Vulkan backend), Epic 12 Story 12.11 (Styling + sprite sheets)
**Goal**: Three full-screen menu screens — main menu (accueil), pause menu, and options — all rendered via RmlUi, styled with the same sprite sheet system as block GUIs. No Lua modding needed for these screens (engine-owned), but the options screen writes to `ConfigManager` and applies settings live.

---

## Story 13.1: Main Menu Screen

**As a** player,
**I want** a main menu on launch with Singleplayer, Options, and Quit buttons,
**so that** the game has a proper entry point instead of jumping directly into the world.

**Acceptance Criteria:**
- Main menu displayed on engine launch, BEFORE world loading
- Background: animated panorama (slow-rotating camera around a pre-generated terrain) OR static screenshot PNG (V1 simplification: static `assets/gui/menu_bg.png`)
- VoxelForge logo centered at top (`assets/gui/logo.png`)
- Version text bottom-left: "VoxelForge v0.1.0"
- Three buttons stacked vertically, centered:
  - **Singleplayer** → loads world, transitions to gameplay
  - **Options** → opens options screen (Story 13.3)
  - **Quit** → `glfwSetWindowShouldClose(true)`
- Buttons use sprite sheet from `gui/widgets.png` (same atlas as block GUIs)
- Hover state: brighter texture, optional click sound stub
- No world loaded while on main menu — no chunks, no ticking, no rendering of 3D scene
- Mouse cursor visible (not captured)
- RmlUi document: `assets/gui/screens/main_menu.rml` + `assets/gui/screens/main_menu.rcss`

**GameApp state machine:**
```
ENGINE_START → MAIN_MENU → [Singleplayer] → LOADING → IN_GAME → [ESC] → PAUSED → [Resume] → IN_GAME
                   ↓                                                        ↓
              [Options] → OPTIONS → [Back] → MAIN_MENU              [Save & Quit] → MAIN_MENU
                   ↓
               [Quit] → EXIT
```

- `GameState` enum: `MainMenu, Loading, InGame, Paused, Options`
- `GameApp::tick()` and `GameApp::render()` behavior depends on current state
- `MainMenu` state: only render menu background + RmlUi, no world tick
- Transition `MainMenu → Loading`: create WorldGenerator, ChunkManager, load initial chunks around spawn
- Transition `Loading → InGame`: camera set to spawn point, cursor captured, gameplay begins

**Loading screen (between menu and gameplay):**
- Simple screen: "Loading world..." text + progress bar (chunks loaded / initial target)
- Initial chunk loading: load N chunks in spiral around spawn (e.g., render distance radius)
- Once enough chunks are loaded for the player to stand on ground → transition to InGame

---

## Story 13.2: Pause Menu Screen

**As a** player,
**I want** ESC during gameplay to open a pause menu with Resume, Options, and Save & Quit,
**so that** I can access settings or exit cleanly without losing progress.

**Acceptance Criteria:**
- ESC while `InGame` → transition to `Paused` state
- Pause menu rendered as overlay on top of darkened game view (semi-transparent black fullscreen quad + RmlUi menu)
- Three buttons stacked vertically, centered:
  - **Resume** → return to `InGame`, recapture cursor
  - **Options** → open options screen (Story 13.3), remember to return to pause menu (not main menu)
  - **Save & Quit** → `ChunkManager::saveAllDirty()`, save config, transition to `MainMenu`
- Game world continues ticking while paused (furnaces cook, time passes — matches Minecraft singleplayer behavior)
- Alternatively: game world PAUSES while menu is open (optional, configurable — default: keep ticking)
- Mouse cursor released (visible, not captured)
- ESC again or Resume button → close pause menu, recapture cursor
- RmlUi document: `assets/gui/screens/pause_menu.rml` + `assets/gui/screens/pause_menu.rcss`

**Input handling while paused:**
- Movement keys (WASD, Space, Shift): blocked
- Camera mouse look: disabled
- Mouse: routed to RmlUi pause menu
- ESC: close pause menu (toggle)
- F3: debug overlay still toggleable (useful for debugging while paused)
- F2: screenshot still works

---

## Story 13.3: Options Screen

**As a** player,
**I want** an options screen with sliders and toggles for all game settings,
**so that** I can customize FOV, sensitivity, render distance, and other preferences.

**Acceptance Criteria:**

**Settings exposed (all read/write `ConfigManager`):**

| Setting | Widget | Range | Default | Live apply? |
|---------|--------|-------|---------|-------------|
| FOV | Slider | 50–120 | 70 | ✅ Immediate |
| Mouse sensitivity | Slider | 0.01–0.5 | 0.1 | ✅ Immediate |
| Render distance | Slider (integer) | 4–32 | 16 | ✅ Triggers chunk load/unload |
| Fullscreen | Toggle | on/off | off | ✅ Immediate (calls Window::toggleFullscreen) |
| VSync | Toggle | on/off | on | ⚠️ Requires swapchain recreate |
| GUI scale | Slider (integer) | 1–4 | auto | ✅ Immediate |
| Audio volume (master) | Slider | 0–100 | 100 | Stub (no audio system yet) |
| Audio volume (music) | Slider | 0–100 | 100 | Stub |
| Audio volume (SFX) | Slider | 0–100 | 100 | Stub |

**Layout:**
- Tab bar or section headers: Video, Controls, Audio
- Each section contains its settings as labeled slider/toggle rows
- **Done** button at bottom → returns to previous screen (main menu or pause menu)
- Settings applied immediately on change (no "Apply" button needed)
- Settings saved to `config.json` when Done is pressed (or on any change — debounced)

**RmlUi implementation:**
- `assets/gui/screens/options.rml` + `assets/gui/screens/options.rcss`
- Sliders: RmlUi `<input type="range">` with custom RCSS styling
- Toggles: RmlUi `<input type="checkbox">` styled as toggle switches
- Labels: `<label>` elements with current value display (e.g., "FOV: 90")
- Sections: `<div>` containers with header text

**Options ↔ ConfigManager wiring:**
- On open: read all values from `ConfigManager`, set widget states
- On change: write to `ConfigManager` + apply to live system (Camera, Window, Renderer)
- On Done: `ConfigManager::save("config.json")`
- `ConfigManager` extended with new fields: `vsync`, `guiScale`, `audioMaster`, `audioMusic`, `audioSfx`

**Return navigation:**
- Options opened from main menu → Done returns to main menu
- Options opened from pause menu → Done returns to pause menu
- Track `m_returnState` to know where to go back

**Mod settings API (Lua-defined, engine-rendered):**

Mods register their own settings via Lua. The engine adds a "Mods" tab in the options screen with one section per mod:

```lua
voxel.register_settings("techmod", {
    label = "Tech Mod",  -- section header displayed in Mods tab
    settings = {
        { id = "max_transfer", type = "slider", label = "Max Power Transfer",
          min = 0, max = 1000, default = 100, step = 10 },
        { id = "enable_sparks", type = "toggle", label = "Enable Spark Effects",
          default = true },
        { id = "pipe_mode", type = "dropdown", label = "Default Pipe Mode",
          options = { "input", "output", "disabled" }, default = "input" },
        { id = "update_rate", type = "slider", label = "Machine Tick Rate (ms)",
          min = 50, max = 1000, default = 200, step = 50 },
    },
})
```

- Widget types supported: `slider` (int/float), `toggle` (bool), `dropdown` (string list)
- Each mod's settings persisted in `config.json` under `"mod_settings": { "techmod": { "max_transfer": 100, ... } }`
- Settings read via `voxel.get_setting("techmod", "max_transfer") → number`
- Settings applied live on change — engine calls `on_setting_changed(id, value)` callback if provided
- Mods tab only appears if at least one mod has registered settings
- Sections sorted alphabetically by mod label
- Mod settings loaded AFTER Lua scripts init, BEFORE world loading — available from the first tick

---

## Story 13.4: World Selection Screen (V1 Minimal)

**As a** player,
**I want** to create a new world with a name and seed, or load an existing world,
**so that** I can manage multiple saved worlds.

**Acceptance Criteria:**

**World list:**
- Scan `worlds/` directory for subdirectories containing `world.json`
- Display each world as a row: world name, seed, last played date, world size on disk
- Sort by last played (most recent first)
- Click to select, double-click or "Play" button to load

**Create new world:**
- "Create New World" button opens inline panel:
  - World name text field (default: "New World")
  - Seed text field (default: random, shown as number)
  - "Create" button → creates `worlds/{name}/world.json`, transitions to loading
- World directory name: sanitize the world name (lowercase, replace spaces with underscores, strip special chars)

**Delete world:**
- "Delete" button on selected world → confirmation dialog ("Delete world '{name}'? This cannot be undone.")
- Confirm → `std::filesystem::remove_all(worldDir)`

**World directory structure (from Story 3.7):**
```
worlds/
├── my_world/
│   ├── world.json       # { "name": "My World", "seed": 12345, "created": "...", "last_played": "..." }
│   └── region/
│       └── r.0.0.vxr
└── creative_test/
    ├── world.json
    └── region/
```

**Transition from current single-world system:**
- Current: seed in `config.json`, chunks in working directory
- New: seed in `worlds/{name}/world.json`, chunks in `worlds/{name}/region/`
- Migration: if old-style region files exist in working directory, auto-create `worlds/default/` and move them

**RmlUi document:** `assets/gui/screens/world_select.rml`

**Flow:**
```
Main Menu → [Singleplayer] → World Selection → [Play] → Loading → InGame
                                   ↓
                           [Create New World] → Loading → InGame
```

---

## Story 13.5: Loading Screen + State Machine Wiring

**As a** developer,
**I want** a loading screen between menu and gameplay with progress feedback,
**so that** the transition is smooth and the player knows what's happening.

**Acceptance Criteria:**

**Loading screen:**
- Background: same as main menu (static or darkened)
- Center: "Loading world..." or "Generating terrain..." text
- Progress bar: chunks loaded / target chunks (render distance² × π approximation)
- World name displayed above progress
- Loading runs across multiple frames (not a blocking loop):
  - Each frame: load/generate N chunks (max 4-8 per frame to maintain 30+ FPS during loading)
  - Update progress bar
  - When target reached → transition to InGame

**GameState machine (complete):**
```cpp
enum class GameState {
    MainMenu,
    WorldSelect,
    Loading,
    InGame,
    Paused,
    Options,
};
```

**State transitions wired in GameApp:**
- `MainMenu`: render menu background + RmlUi main menu. No world systems active.
- `WorldSelect`: render world list. No world systems active.
- `Loading`: WorldGenerator + ChunkManager active, loading chunks. Render loading screen.
- `InGame`: full gameplay — tick world, render 3D scene + HUD.
- `Paused`: tick world (optional), render darkened scene + pause menu RmlUi.
- `Options`: render previous screen (darkened) + options overlay. Settings apply live.

**GameApp::tick() dispatch:**
```cpp
void GameApp::tick(double dt) {
    switch (m_state) {
        case GameState::MainMenu:
        case GameState::WorldSelect:
            // Only process menu input
            break;
        case GameState::Loading:
            loadNextChunks(4);
            if (isLoadingComplete()) transition(GameState::InGame);
            break;
        case GameState::InGame:
            handleInputToggles();
            updateCamera(dt);
            // world tick, meshing, etc.
            break;
        case GameState::Paused:
            // optional: world tick
            break;
        case GameState::Options:
            break;
    }
}
```

**GameApp::render() dispatch:**
```cpp
void GameApp::render(double alpha) {
    switch (m_state) {
        case GameState::MainMenu:
        case GameState::WorldSelect:
            renderMenuBackground();
            m_guiSystem.render(cmd);
            break;
        case GameState::Loading:
            renderMenuBackground();
            renderLoadingProgress();
            break;
        case GameState::InGame:
            renderScene();        // 3D world
            drawCrosshair();
            drawHotbar();
            m_guiSystem.render(cmd);  // container GUIs if open
            break;
        case GameState::Paused:
            renderScene();        // darkened
            m_guiSystem.render(cmd);  // pause menu
            break;
        case GameState::Options:
            renderPreviousState(); // darkened
            m_guiSystem.render(cmd);  // options overlay
            break;
    }
}
```

**World system lifecycle:**
- `MainMenu` / `WorldSelect`: no WorldGenerator, no ChunkManager, no BlockRegistry loaded
- `Loading` entry: create BlockRegistry (load blocks.json), create WorldGenerator (with seed), create ChunkManager (inject worldgen)
- `InGame → MainMenu` (Save & Quit): `saveAllDirty()`, destroy ChunkManager, WorldGenerator, BlockRegistry
- This means world systems are created/destroyed per session — no leftover state between worlds

---

## Summary

| Story | Scope | Key deliverables |
|-------|-------|-----------------|
| 13.1 | Main menu | Logo, 3 buttons, GameState enum, menu→game transition |
| 13.2 | Pause menu | ESC toggle, darkened overlay, Resume/Options/Save&Quit |
| 13.3 | Options screen | 9 settings (FOV, sensitivity, render distance, fullscreen, VSync, GUI scale, audio stubs), live apply, ConfigManager wiring |
| 13.4 | World selection | World list from `worlds/`, create new, delete, seed input, multi-world support |
| 13.5 | Loading + state machine | Loading progress bar, GameState dispatch in tick/render, world system lifecycle |
