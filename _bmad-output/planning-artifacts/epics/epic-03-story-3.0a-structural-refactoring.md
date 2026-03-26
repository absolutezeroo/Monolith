# Story 3.0a — Structural Refactoring (Code Organization)

**Priority**: P0 — do before Story 3.0b
**Epic**: 3 (inserted)
**Scope**: Reorganize code without changing Vulkan rendering behavior. No GPU changes. Safe to implement part by part with compile+test between each.

---

## Part A: Extract InputManager from GameApp

### The actual problem

`GameApp.cpp` lines 38-138: three `static` callbacks (`keyCallback`, `cursorPosCallback`, `mouseButtonCallback`) use `glfwSetWindowUserPointer(this)` to route GLFW events to `GameApp`. This works now because only the Camera reads input.

When `ChunkManager` arrives (Story 3.4), it needs to know the camera position for chunk loading. When DDA raycasting arrives (Story 7.4), it needs mouse clicks. When Lua scripting arrives (Epic 9), it needs to intercept events. All of these will need input access — and right now the only way to get it is through `GameApp`.

### What to do

Create `engine/include/voxel/input/InputManager.h` + `engine/src/input/InputManager.cpp`:

```cpp
class InputManager
{
public:
    explicit InputManager(GLFWwindow* window);
    ~InputManager();

    // Call once per frame
    void update(float dt);

    // Key state — instant
    bool isKeyDown(int key) const;
    bool wasKeyPressed(int key) const;   // Edge: true only on the frame it was pressed
    bool wasKeyReleased(int key) const;  // Edge: true only on the frame it was released

    // Key state — duration
    float getKeyHoldDuration(int key) const;  // Seconds held, 0 if not held
    bool wasKeyDoubleTapped(int key, float maxInterval = 0.3f) const;  // Two presses within interval

    // Mouse — instant
    glm::vec2 getMouseDelta() const;
    bool isMouseButtonDown(int button) const;
    bool wasMouseButtonPressed(int button) const;
    bool wasMouseButtonReleased(int button) const;

    // Mouse — duration
    float getMouseButtonHoldDuration(int button) const;  // Seconds held, 0 if not held

    // Cursor capture
    void setCursorCaptured(bool captured);
    bool isCursorCaptured() const;

private:
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);

    GLFWwindow* m_window;
    std::array<bool, 512> m_keyStates{};
    std::array<bool, 512> m_keyPressed{};    // Edge-triggered, cleared each frame
    std::array<bool, 512> m_keyReleased{};   // Edge-triggered, cleared each frame
    std::array<float, 512> m_keyHoldTime{};  // Seconds held (0 if not held)
    std::array<float, 512> m_keyLastPressTime{};  // Timestamp of last press (for double-tap)
    std::array<bool, 8> m_mouseStates{};
    std::array<bool, 8> m_mousePressed{};
    std::array<bool, 8> m_mouseReleased{};
    std::array<float, 8> m_mouseHoldTime{};
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    bool m_cursorCaptured = true;
    bool m_firstMouse = true;
    float m_totalTime = 0.0f;  // Running clock for double-tap detection
};

// update() implementation:
// - For each key that is down: m_keyHoldTime[key] += dt
// - For each key that was just released: m_keyHoldTime[key] = 0
// - For double-tap: on press, check if (m_totalTime - m_keyLastPressTime[key]) < maxInterval
// - Same logic for mouse buttons
```

### Changes to GameApp

- Remove: `keyCallback`, `cursorPosCallback`, `mouseButtonCallback`, `setupInputCallbacks()`, `m_keyStates`, `m_lastCursorX/Y`, `m_mouseDeltaX/Y`, `m_cursorCaptured`, `m_firstMouse` (all of lines 38-138 and the member declarations)
- Add: `InputManager m_input;` member, constructed with `m_window.getHandle()`
- `tick()` becomes:

```cpp
void GameApp::tick(double dt)
{
    m_input.update();

    // F-key toggles
    if (m_input.wasKeyPressed(GLFW_KEY_F3)) m_overlayState.showOverlay = !m_overlayState.showOverlay;
    if (m_input.wasKeyPressed(GLFW_KEY_F4)) m_overlayState.wireframeMode = !m_overlayState.wireframeMode;
    if (m_input.wasKeyPressed(GLFW_KEY_F5)) m_overlayState.showChunkBorders = !m_overlayState.showChunkBorders;
    if (m_input.wasKeyPressed(GLFW_KEY_ESCAPE)) m_input.setCursorCaptured(false);

    // Camera
    ImGuiIO& io = ImGui::GetIO();
    if (m_input.isCursorCaptured() && !io.WantCaptureMouse)
    {
        auto delta = m_input.getMouseDelta();
        m_camera.processMouseDelta(delta.x, delta.y);
    }
    if (!io.WantCaptureKeyboard)
    {
        m_camera.update(static_cast<float>(dt),
            m_input.isKeyDown(GLFW_KEY_W), m_input.isKeyDown(GLFW_KEY_S),
            m_input.isKeyDown(GLFW_KEY_A), m_input.isKeyDown(GLFW_KEY_D),
            m_input.isKeyDown(GLFW_KEY_SPACE), m_input.isKeyDown(GLFW_KEY_LEFT_SHIFT));
    }

    // ... rest same as before
}
```

### Why this matters for the next stories

Story 3.4 (ChunkManager) will need `m_camera.getPosition()` — already accessible.
Story 7.4 (DDA raycasting) will need `m_input.wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)` — now accessible without touching GameApp's internals.
Story 9.3 (Lua event hooks) will need to subscribe to input events — InputManager can add an observer pattern later without changing GameApp.

---

## Part B: Deduplicate Pipeline Creation in Renderer

### The actual problem

`Renderer.cpp` lines 215-348 (`createPipeline`) and lines 350-455 (`createWireframePipeline`) are 240 lines total with ONE meaningful difference: `VK_POLYGON_MODE_FILL` vs `VK_POLYGON_MODE_LINE`. Everything else is identical boilerplate.

When Story 6.2 (chunk vertex pulling shader) arrives, you need a third pipeline with different shaders, a descriptor set layout, and possibly different vertex input state. Story 6.4 adds a compute pipeline. Story 6.6 adds a G-Buffer pipeline with multiple color attachments. You'll be copy-pasting this function 4 more times.

### What to do

Extract a helper function in `Renderer.cpp` (private method, not a new class — it's too early for a PipelineBuilder class when you only have 2 pipelines):

```cpp
struct PipelineConfig
{
    std::string vertPath;
    std::string fragPath;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    // Future: descriptor set layouts, vertex input, depth test, etc.
};

core::Result<VkPipeline> Renderer::buildPipeline(const PipelineConfig& config);
```

Then `createPipeline` and `createWireframePipeline` collapse to:

```cpp
core::Result<void> Renderer::createPipelines(const std::string& shaderDir)
{
    // Layout (shared by both for now)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
    if (result != VK_SUCCESS) return std::unexpected(core::EngineError::VulkanError);

    VkFormat format = m_vulkanContext.getSwapchainFormat();

    // Fill pipeline
    auto fillResult = buildPipeline({
        .vertPath = shaderDir + "/triangle.vert.spv",
        .fragPath = shaderDir + "/triangle.frag.spv",
        .polygonMode = VK_POLYGON_MODE_FILL,
        .layout = m_pipelineLayout,
        .colorFormat = format
    });
    if (!fillResult) return std::unexpected(fillResult.error());
    m_pipeline = *fillResult;

    // Wireframe pipeline
    auto wireResult = buildPipeline({
        .vertPath = shaderDir + "/triangle.vert.spv",
        .fragPath = shaderDir + "/triangle.frag.spv",
        .polygonMode = VK_POLYGON_MODE_LINE,
        .layout = m_pipelineLayout,
        .colorFormat = format
    });
    if (!wireResult)
    {
        VX_LOG_WARN("Wireframe pipeline failed — wireframe mode disabled");
    }
    else
    {
        m_wireframePipeline = *wireResult;
    }

    return {};
}
```

This cuts `Renderer.cpp` by ~120 lines and makes Story 6.2 trivial: just add another `buildPipeline()` call with chunk shaders and a descriptor set layout in the config.

### Do NOT extract yet

- Don't make `PipelineBuilder` a separate class yet — you don't know what Epic 6 will actually need. Keep it as a private method for now.
- Don't extract `ShaderManager` — `loadShaderModule` is 30 lines and only called by `buildPipeline`. No need to add a class for a single function.
- Don't move `FrameData` — it's already a clean struct in `Renderer.h`, line 30-36. It's fine where it is.

---

## Part C: Move Debug Overlay out of Renderer

### The actual problem

`Renderer::buildDebugOverlay()` (lines 507-569) is ImGui code that reads Camera state and DebugOverlayState. When ChunkManager arrives (Story 3.4), you'll want to show chunk stats. When meshing arrives (Epic 5), mesh queue size. When lighting arrives (Epic 8), light values. Each of these systems will need to push stats into the overlay.

If `buildDebugOverlay` stays in Renderer, every new system that wants to show a stat needs Renderer to know about it. That's backwards coupling.

### What to do

Move `buildDebugOverlay` to `GameApp::buildDebugOverlay()`. GameApp already has access to Camera, OverlayState, and will have access to future systems (ChunkManager, etc.):

```cpp
// GameApp.h — add:
void buildDebugOverlay();

// GameApp.cpp — render() becomes:
void GameApp::render(double /*alpha*/)
{
    m_renderer.beginFrame(m_window);

    // Game will draw its own ImGui here
    buildDebugOverlay();

    m_renderer.endFrame(m_window, m_camera, m_overlayState);
}
```

This requires splitting `Renderer::draw()` into `beginFrame()` / `endFrame()`:
- `beginFrame()`: acquire image, begin command buffer, begin dynamic rendering, set viewport/scissor, bind pipeline, draw triangle
- `endFrame()`: ImGui render (the actual rendering call, not the UI building), end rendering, transition, submit, present

The overlay **building** (ImGui::Begin, ImGui::Text, etc.) happens in GameApp between begin and end. The overlay **rendering** (ImGui::Render, vkCmd draw) stays in Renderer.

### The Renderer split

In `Renderer.h`, replace `draw()` with:

```cpp
void beginFrame(game::Window& window);
void endFrame(game::Window& window, const Camera& camera, DebugOverlayState& overlay);
```

The existing `draw()` logic (lines 572-783) splits cleanly:
- Lines 572-697 (acquire, fence, begin rendering, bind pipeline, draw) → `beginFrame()`
- Lines 699-703 (ImGui build) → **removed from Renderer**, moved to GameApp
- Lines 704-783 (end rendering, submit, present) → `endFrame()`

`m_imguiBackend->beginFrame()` goes in `beginFrame()`.
`m_imguiBackend->render(cmd)` goes in `endFrame()`.

---

## Part D: Add Config Files to Repo

### The actual problem

The `_bmad-output/sprint-status.yaml` shows Stories 1.2 (.clang-format + .clang-tidy + .editorconfig) as `done`, but these files don't exist in the repo root:

```
$ ls /home/claude/Monolith/.clang*
ls: cannot access '/home/claude/Monolith/.clang*': No such file or directory
```

### What to do

1. Create `.clang-format`, `.clang-tidy`, `.editorconfig` at repo root with the configs from `_bmad-output/project-context.md`
2. Run `clang-format -i` on every `.h` and `.cpp` file
3. Commit the formatted result as a single commit: `chore: add config files and format entire codebase`

---


---

## Part F: Enrich EngineError with Context

### The actual problem

`Result.h` line 12-21: `EngineError` is a bare `enum class` with 8 values. When something fails with `VulkanError`, you don't know which Vulkan call failed, what `VkResult` was returned, or which subsystem triggered it. The audit flagged this as a scalability concern — when the engine gets complex, `VulkanError` tells you nothing.

### What to do

Replace the bare enum with a struct that carries context:

```cpp
// Result.h — replace EngineError enum with:

enum class ErrorCode : uint8_t
{
    FileNotFound,
    InvalidFormat,
    ShaderCompileError,
    VulkanError,
    ChunkNotLoaded,
    OutOfMemory,
    InvalidArgument,
    ScriptError
};

struct EngineError
{
    ErrorCode code;
    std::string message;     // Human-readable context
    int32_t nativeResult = 0; // VkResult for Vulkan errors, errno for file errors

    EngineError(ErrorCode c, std::string msg = {}, int32_t native = 0)
        : code(c), message(std::move(msg)), nativeResult(native) {}

    // Convenience constructors
    static EngineError vulkan(int32_t vkResult, std::string_view context) {
        return { ErrorCode::VulkanError,
                 fmt::format("{}: VkResult {}", context, vkResult),
                 vkResult };
    }
    static EngineError file(std::string_view path) {
        return { ErrorCode::FileNotFound, fmt::format("File not found: {}", path) };
    }
};
```

### Migration (every existing `std::unexpected(core::EngineError::VulkanError)` site):

Before:
```cpp
return std::unexpected(core::EngineError::VulkanError);
```

After:
```cpp
return std::unexpected(core::EngineError::vulkan(result, "Failed to create command pool"));
```

This is mechanical — grep for `EngineError::VulkanError` and add context to each site. There are approximately 25 sites in the current codebase.

### Impact on existing code
- `Result<T>` typedef unchanged — still `std::expected<T, EngineError>`
- `.has_value()`, `.value()`, `.error()` all still work
- `.and_then()` / `.or_else()` still work
- The `VX_LOG_ERROR` calls already log context, so the error message is a complement, not a replacement
- Tests that check `!result.has_value()` still pass — they just don't inspect the error code yet

---

## Part G: Make ChunkSection `isEmpty()` and `countNonAir()` O(1)

### The actual problem

`ChunkSection.cpp` lines 34-43:
```cpp
bool ChunkSection::isEmpty() const {
    return std::all_of(std::begin(blocks), std::end(blocks),
        [](uint16_t b) { return b == BLOCK_AIR; });
}
int32_t ChunkSection::countNonAir() const {
    return static_cast<int32_t>(
        std::count_if(std::begin(blocks), std::end(blocks),
            [](uint16_t b) { return b != BLOCK_AIR; }));
}
```

Both scan all 4096 blocks. `isEmpty()` is called by ChunkColumn to decide whether to allocate/deallocate sections, by the mesher to skip empty sections (Story 5.6), and by lazy meshing (Story 11.1) on thousands of sections. At 4096 comparisons per call, on 10,000 sections = 40 million comparisons per frame. This should be O(1).

### What to do

Add an incremental counter maintained by `setBlock()` and `fill()`:

```cpp
// ChunkSection.h — add private member:
int32_t m_nonAirCount = 0;

// ChunkSection.cpp:
ChunkSection::ChunkSection()
{
    std::fill(std::begin(blocks), std::end(blocks), BLOCK_AIR);
    m_nonAirCount = 0;
}

void ChunkSection::setBlock(int x, int y, int z, uint16_t id)
{
    VX_ASSERT(x >= 0 && x < SIZE, "x out of bounds");
    VX_ASSERT(y >= 0 && y < SIZE, "y out of bounds");
    VX_ASSERT(z >= 0 && z < SIZE, "z out of bounds");
    int idx = toIndex(x, y, z);
    uint16_t old = blocks[idx];
    if (old == id) return; // No change
    if (old == BLOCK_AIR && id != BLOCK_AIR) ++m_nonAirCount;
    if (old != BLOCK_AIR && id == BLOCK_AIR) --m_nonAirCount;
    blocks[idx] = id;
}

void ChunkSection::fill(uint16_t id)
{
    std::fill(std::begin(blocks), std::end(blocks), id);
    m_nonAirCount = (id == BLOCK_AIR) ? 0 : VOLUME;
}

bool ChunkSection::isEmpty() const { return m_nonAirCount == 0; }
bool ChunkSection::isFull() const { return m_nonAirCount == VOLUME; }
int32_t ChunkSection::countNonAir() const { return m_nonAirCount; }
```

### Also add `isFull()` — needed by lazy meshing (Story 11.1):
A section that is 100% solid (all non-air) AND all 6 neighbors are 100% solid → no visible faces → skip meshing entirely. This is the single highest-impact optimization in Epic 11.

### Update tests:
The existing `TestChunkSection.cpp` tests `isEmpty()` and `countNonAir()` — they should still pass. Add a test that `setBlock` back to AIR decrements the counter correctly.

---


---

## Definition of Done

- [ ] `InputManager` class in `engine/input/`, GameApp has zero `static` GLFW callbacks
- [ ] `GameApp::tick()` reads from `InputManager` instead of raw `m_keyStates`
- [ ] `wasKeyPressed()` edge detection works (true only on the frame pressed)
- [ ] `getKeyHoldDuration()` tracks hold time for mining system (Story 7.5)
- [ ] `buildPipeline(PipelineConfig)` private method in Renderer, `createPipeline` + `createWireframePipeline` replaced by two calls to it
- [ ] `buildDebugOverlay()` moved to GameApp, `Renderer::draw()` split into `beginFrame()` / `endFrame()`
- [ ] `.clang-format`, `.clang-tidy`, `.editorconfig` committed, all source files formatted
- [ ] EngineError is a struct with `ErrorCode`, `message`, `nativeResult` — all ~25 `VulkanError` sites carry context
- [ ] ChunkSection uses incremental `m_nonAirCount`, `isEmpty()` / `isFull()` / `countNonAir()` are O(1)
- [ ] All existing tests still pass
- [ ] Camera, ImGui overlay, wireframe toggle still work identically
- [ ] No visual or functional regressions
