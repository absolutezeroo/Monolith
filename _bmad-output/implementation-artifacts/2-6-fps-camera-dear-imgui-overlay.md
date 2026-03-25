# Story 2.6: FPS Camera + Dear ImGui Overlay

Status: ready-for-dev

## Story

As a developer,
I want a fly-mode camera and debug UI overlay,
so that I can navigate the world and inspect engine state.

## Acceptance Criteria

1. `Camera` class: position (`dvec3`), yaw/pitch, view matrix, projection matrix (perspective, configurable FOV)
2. Mouse look: raw mouse delta -> yaw/pitch rotation, pitch clamped +/-89 degrees
3. WASD+Space+Shift fly movement (no collision yet), speed configurable
4. `extractFrustumPlanes()` returns 6 planes for culling (used in future epics)
5. Dear ImGui initialized with Vulkan backend (dynamic rendering, no VkRenderPass)
6. F3 toggles overlay: FPS, camera position (x,y,z), yaw/pitch, memory usage (gigabuffer used/free)
7. Mouse capture toggle (Escape to release cursor, click to recapture)
8. Wireframe rendering toggle (F4) -- switches polygon mode to LINE for all draws
9. Chunk boundary visualization toggle (F5) -- stub flag for now, rendering deferred until ChunkManager exists
10. Chunk state color coding in ImGui -- placeholder section ("No chunks loaded") until ChunkManager exists

## Tasks / Subtasks

- [ ] Task 1 -- Update vcpkg.json and CMake for ImGui (AC: #5)
  - [ ] 1.1 Update `vcpkg.json`: change `"imgui"` to `{"name": "imgui", "features": ["docking-experimental"]}`
  - [ ] 1.2 Create `engine/src/renderer/ImGuiImpl.cpp` that compiles `imgui_impl_vulkan.cpp` and `imgui_impl_glfw.cpp` backend sources with `VK_NO_PROTOTYPES` and `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` (see Dev Notes: ImGui + volk Integration)
  - [ ] 1.3 Add `find_package(imgui CONFIG REQUIRED)` to `engine/CMakeLists.txt`
  - [ ] 1.4 Link `imgui::imgui` PRIVATE to VoxelEngine
  - [ ] 1.5 Add `ImGuiImpl.cpp` to engine source list
  - [ ] 1.6 Verify build compiles with imgui headers available

- [ ] Task 2 -- Create `Camera` class header (AC: #1, #4)
  - [ ] 2.1 Create `engine/include/voxel/renderer/Camera.h`
  - [ ] 2.2 Define `Camera` class in `voxel::renderer` namespace
  - [ ] 2.3 Members: `glm::dvec3 m_position{0.0, 80.0, 0.0}`, `float m_yaw = 0.0f`, `float m_pitch = 0.0f`
  - [ ] 2.4 Members: `float m_fov = 70.0f`, `float m_nearPlane = 0.1f`, `float m_farPlane = 1000.0f`, `float m_aspectRatio = 16.0f/9.0f`
  - [ ] 2.5 Members: `float m_moveSpeed = 10.0f`, `float m_sensitivity = 0.1f`
  - [ ] 2.6 API: `void processMouseDelta(float dx, float dy)` -- apply yaw/pitch from raw mouse movement
  - [ ] 2.7 API: `void update(float dt, bool forward, bool backward, bool left, bool right, bool up, bool down)` -- apply fly movement
  - [ ] 2.8 API: `void setAspectRatio(float aspect)` -- call on window resize
  - [ ] 2.9 API: `[[nodiscard]] glm::mat4 getViewMatrix() const`
  - [ ] 2.10 API: `[[nodiscard]] glm::mat4 getProjectionMatrix() const`
  - [ ] 2.11 API: `[[nodiscard]] std::array<glm::vec4, 6> extractFrustumPlanes() const` -- from VP matrix
  - [ ] 2.12 API: `[[nodiscard]] glm::vec3 getForward() const`, `getRight() const`, `getUp() const`
  - [ ] 2.13 Getters/setters for FOV, sensitivity, move speed, position, yaw, pitch

- [ ] Task 3 -- Implement `Camera` class (AC: #1, #2, #3, #4)
  - [ ] 3.1 Create `engine/src/renderer/Camera.cpp`
  - [ ] 3.2 Add to `engine/CMakeLists.txt`
  - [ ] 3.3 Implement `processMouseDelta()`: yaw += dx * sensitivity, pitch -= dy * sensitivity, clamp pitch to [-89, 89]
  - [ ] 3.4 Implement directional vectors: forward = normalize(cos(yaw)*cos(pitch), sin(pitch), sin(yaw)*cos(pitch)) -- angles in radians
  - [ ] 3.5 Implement `getViewMatrix()`: `glm::lookAt(position, position + forward, worldUp)` where worldUp = (0,1,0)
  - [ ] 3.6 Implement `getProjectionMatrix()`: `glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane)`
  - [ ] 3.7 Implement `update()`: move along forward/right/up vectors scaled by dt and moveSpeed
  - [ ] 3.8 Implement `extractFrustumPlanes()`: compute from VP matrix using Gribb-Hartmann method, normalize each plane

- [ ] Task 4 -- Create `ImGuiBackend` class (AC: #5)
  - [ ] 4.1 Create `engine/include/voxel/renderer/ImGuiBackend.h`
  - [ ] 4.2 Define `ImGuiBackend` class in `voxel::renderer` namespace
  - [ ] 4.3 Factory: `static Result<std::unique_ptr<ImGuiBackend>> create(VulkanContext& context, GLFWwindow* window)`
  - [ ] 4.4 API: `void beginFrame()` -- calls ImGui_ImplVulkan_NewFrame, ImGui_ImplGlfw_NewFrame, ImGui::NewFrame
  - [ ] 4.5 API: `void render(VkCommandBuffer cmd)` -- calls ImGui::Render, ImGui_ImplVulkan_RenderDrawData
  - [ ] 4.6 Destructor: ImGui_ImplVulkan_Shutdown, ImGui_ImplGlfw_Shutdown, ImGui::DestroyContext, destroy descriptor pool
  - [ ] 4.7 Non-copyable, non-movable

- [ ] Task 5 -- Implement `ImGuiBackend::create()` (AC: #5)
  - [ ] 5.1 Create `engine/src/renderer/ImGuiBackend.cpp`
  - [ ] 5.2 Add to `engine/CMakeLists.txt`
  - [ ] 5.3 Create `VkDescriptorPool` for ImGui (1000 combined image samplers -- ImGui default recommendation)
  - [ ] 5.4 Call `ImGui::CreateContext()`
  - [ ] 5.5 Call `ImGui_ImplGlfw_InitForVulkan(window, true)` -- true = install callbacks
  - [ ] 5.6 Fill `ImGui_ImplVulkan_InitInfo`: Instance, PhysicalDevice, Device, QueueFamily, Queue, DescriptorPool, MinImageCount=2, ImageCount=swapchain image count, MSAASamples=1BIT, UseDynamicRendering=true, ApiVersion=VK_API_VERSION_1_3
  - [ ] 5.7 Fill `PipelineRenderingCreateInfo`: colorAttachmentCount=1, pColorAttachmentFormats=&swapchainFormat
  - [ ] 5.8 Call `ImGui_ImplVulkan_Init(&initInfo)`
  - [ ] 5.9 Style: `ImGui::StyleColorsDark()`
  - [ ] 5.10 Log success

- [ ] Task 6 -- Integrate input handling in GameApp (AC: #2, #3, #7)
  - [ ] 6.1 Add `Camera` member to `GameApp`
  - [ ] 6.2 Register GLFW key callback via `glfwSetKeyCallback` -- store key states in a bool array or bitset
  - [ ] 6.3 Register GLFW cursor position callback via `glfwSetCursorPosCallback` -- compute delta from last position
  - [ ] 6.4 Register GLFW mouse button callback for recapture (left click when cursor is released)
  - [ ] 6.5 Implement `tick()` override: update Camera with WASD/Space/Shift state and dt, apply mouse delta
  - [ ] 6.6 Start with cursor captured: `glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED)`
  - [ ] 6.7 Enable raw mouse motion if supported: `glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE)`
  - [ ] 6.8 F3 key toggles `m_showDebugOverlay` bool
  - [ ] 6.9 F4 key toggles `m_wireframeMode` bool
  - [ ] 6.10 F5 key toggles `m_showChunkBorders` bool (stub, no rendering effect yet)
  - [ ] 6.11 Escape key releases cursor (`GLFW_CURSOR_NORMAL`); left click recaptures (`GLFW_CURSOR_DISABLED`)
  - [ ] 6.12 When cursor is released, do NOT send mouse delta to camera

- [ ] Task 7 -- Integrate Camera + ImGui into Renderer (AC: #5, #6, #8)
  - [ ] 7.1 Add `ImGuiBackend` ownership to Renderer (or keep in GameApp -- see Dev Notes)
  - [ ] 7.2 In `Renderer::init()`: create ImGuiBackend after frame resources
  - [ ] 7.3 In `Renderer::draw()`: after scene rendering but before `vkCmdEndRendering()`, call `m_imguiBackend->beginFrame()`, build ImGui windows, call `m_imguiBackend->render(cmd)`
  - [ ] 7.4 Create wireframe pipeline variant: clone current pipeline with `polygonMode = VK_POLYGON_MODE_LINE`
  - [ ] 7.5 `draw()` selects pipeline based on wireframe flag: `vkCmdBindPipeline(cmd, ..., wireframe ? m_wireframePipeline : m_pipeline)`
  - [ ] 7.6 Pass Camera reference to Renderer for matrix access (for ImGui display and future push constants)
  - [ ] 7.7 In `shutdown()`: destroy ImGuiBackend before other Vulkan resources, destroy wireframe pipeline

- [ ] Task 8 -- Implement F3 debug overlay (AC: #6, #10)
  - [ ] 8.1 Build ImGui window in Renderer or GameApp when `m_showDebugOverlay` is true
  - [ ] 8.2 Display: "VoxelForge v0.1.0" header
  - [ ] 8.3 Display: FPS counter and frame time (compute from delta time)
  - [ ] 8.4 Display: Camera position XYZ (from Camera::getPosition(), format as dvec3 with 2 decimal places)
  - [ ] 8.5 Display: Yaw/Pitch values
  - [ ] 8.6 Display: Facing direction label (North/South/East/West based on yaw)
  - [ ] 8.7 Display: Gigabuffer memory -- `usedBytes() / capacity` in MB with percentage (show "N/A" if Gigabuffer not yet wired)
  - [ ] 8.8 Display: Chunk state placeholder: "Chunks: No ChunkManager active"
  - [ ] 8.9 Display: Toggle states -- [F4] Wireframe, [F5] Chunk borders
  - [ ] 8.10 ImGui window: top-left, semi-transparent, `ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav`
  - [ ] 8.11 Add FOV slider (50-110) and mouse sensitivity slider (0.01-0.5) for runtime tuning

- [ ] Task 9 -- Write unit tests for Camera (AC: #1, #4)
  - [ ] 9.1 Create `tests/renderer/TestCamera.cpp`
  - [ ] 9.2 Add to `tests/CMakeLists.txt`
  - [ ] 9.3 Test: default position and orientation produce valid view matrix
  - [ ] 9.4 Test: pitch clamp at +/-89 degrees
  - [ ] 9.5 Test: yaw wraps correctly (no NaN/Inf for extreme values)
  - [ ] 9.6 Test: frustum plane extraction produces 6 normalized planes
  - [ ] 9.7 Test: forward/right/up vectors are orthonormal
  - [ ] 9.8 Test: movement along forward vector changes position correctly
  - [ ] 9.9 Test: aspect ratio change updates projection matrix

- [ ] Task 10 -- Build and verify (AC: all)
  - [ ] 10.1 Build with `msvc-debug` preset -- no warnings, no errors
  - [ ] 10.2 Run `ctest --preset msvc-debug` -- all existing + new tests pass
  - [ ] 10.3 Manual verification: launch app, move camera with WASD, verify mouse look works
  - [ ] 10.4 Manual verification: F3 shows overlay with live data
  - [ ] 10.5 Manual verification: F4 toggles wireframe on the test triangle
  - [ ] 10.6 Manual verification: Escape releases cursor, click recaptures
  - [ ] 10.7 Manual verification: no Vulkan validation layer errors

## Dev Notes

### Architecture Init Stack -- This Story Covers Step 13

From architecture.md System 5:

```
1-7.   volkInit, Instance, PhysicalDevice, Device, Queues, VMA, Swapchain  <- Story 2.2 (DONE)
8.     Gigabuffer allocation (256 MB DEVICE_LOCAL)                          <- Story 2.4 (DONE)
9.     Shared quad index buffer                                             <- Story 6.1
10.    Shader pipelines                                                     <- Story 2.3 (DONE)
11.    Texture array                                                        <- Story 6.5
12.    Descriptor sets                                                      <- Story 6.2+
13.    Dear ImGui init                                                      <- THIS STORY
```

This story adds the final bootstrap-phase component: ImGui + Camera. After this, the renderer has everything needed to start integrating real chunk meshes (Epic 5-6).

### ImGui + volk Integration (CRITICAL)

**Problem**: The project defines `VK_NO_PROTOTYPES` globally (via volk). vcpkg's pre-compiled ImGui Vulkan backend was likely compiled WITHOUT this define, causing potential link issues.

**Recommended solution**: Compile ImGui Vulkan and GLFW backends as part of our engine build with correct defines.

Create `engine/src/renderer/ImGuiImpl.cpp`:
```cpp
// ImGuiImpl.cpp — Compile ImGui backends with project-correct defines.
// VK_NO_PROTOTYPES is set globally via CMake. IMGUI_IMPL_VULKAN_NO_PROTOTYPES
// is auto-detected by imgui_impl_vulkan.h when VK_NO_PROTOTYPES is defined.
//
// We compile the backend .cpp files ourselves instead of using vcpkg's
// pre-compiled versions because vcpkg may compile without VK_NO_PROTOTYPES.

#include <imgui_impl_glfw.cpp>
#include <imgui_impl_vulkan.cpp>
```

In `engine/CMakeLists.txt`, add this file to the source list. The `imgui::imgui` library (core) is linked for the headers and core implementation. The backends are compiled in our translation unit.

**Alternative**: If the above include-cpp approach causes issues, copy `imgui_impl_vulkan.cpp` and `imgui_impl_glfw.cpp` from vcpkg's installed include directory into `engine/src/renderer/` and add them to CMake directly.

**vcpkg.json change**:
```json
{
    "name": "imgui",
    "features": ["docking-experimental"]
}
```

Only `docking-experimental` is needed from vcpkg (for the docking-branch build of core ImGui). Do NOT add `vulkan-binding` or `glfw-binding` features since we compile those backends ourselves.

### ImGui Vulkan Init with Dynamic Rendering

ImGui's Vulkan backend natively supports dynamic rendering (no VkRenderPass). Key configuration:

```cpp
ImGui_ImplVulkan_InitInfo initInfo{};
initInfo.Instance        = context.getInstance();
initInfo.PhysicalDevice  = context.getPhysicalDevice();
initInfo.Device          = context.getDevice();
initInfo.QueueFamily     = context.getGraphicsQueueFamily();
initInfo.Queue           = context.getGraphicsQueue();
initInfo.DescriptorPool  = m_imguiDescriptorPool;
initInfo.MinImageCount   = 2;
initInfo.ImageCount      = static_cast<uint32_t>(context.getSwapchainImages().size());
initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
initInfo.UseDynamicRendering = true;
initInfo.ApiVersion      = VK_API_VERSION_1_3;

// Dynamic rendering pipeline info
VkFormat swapchainFormat = context.getSwapchainFormat();
initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
```

**Do NOT create a VkRenderPass** for ImGui. The `UseDynamicRendering = true` flag makes ImGui use `vkCmdBeginRendering`/`vkCmdEndRendering` internally when it creates its pipeline.

**As of 2025+**: ImGui loads dynamic rendering functions via `vkGetDeviceProcAddr()` and tries both non-KHR and KHR variants. With `ApiVersion = VK_API_VERSION_1_3`, it uses `vkCmdBeginRendering` (core, no KHR suffix).

### ImGui Descriptor Pool

ImGui needs its own descriptor pool. Create it with generous limits -- ImGui allocates from this pool internally:

```cpp
VkDescriptorPoolSize poolSizes[] = {
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}
};
VkDescriptorPoolCreateInfo poolCI{};
poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
poolCI.maxSets       = 100;
poolCI.poolSizeCount = 1;
poolCI.pPoolSizes    = poolSizes;
vkCreateDescriptorPool(device, &poolCI, nullptr, &m_imguiDescriptorPool);
```

The `FREE_DESCRIPTOR_SET_BIT` flag is required -- ImGui frees individual descriptor sets.

### ImGui Rendering Integration

ImGui draw data must be recorded INSIDE the dynamic rendering pass (between `vkCmdBeginRendering` and `vkCmdEndRendering`). The render order in `Renderer::draw()`:

```
vkCmdBeginRendering(cmd, &renderingInfo)
  1. Set viewport + scissor
  2. Bind scene pipeline (fill or wireframe)
  3. Draw scene (test triangle for now)
  4. ImGui::Render()
  5. ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd)
vkCmdEndRendering(cmd)
```

ImGui internally binds its own pipeline and descriptor sets, so it does not interfere with the scene pipeline. The attachment load/store ops remain the same (CLEAR on the scene pass, STORE at the end -- ImGui draws on top).

### Camera Coordinate System

The project uses **left-handed, Y-up** (set by `GLM_FORCE_LEFT_HANDED` and `GLM_FORCE_DEPTH_ZERO_TO_ONE`):
- +X = right
- +Y = up
- +Z = forward (into screen)

Camera direction vectors from yaw/pitch (yaw=0 faces +Z, increases counterclockwise):
```cpp
glm::vec3 forward;
forward.x = std::cos(glm::radians(m_pitch)) * std::sin(glm::radians(m_yaw));
forward.y = std::sin(glm::radians(m_pitch));
forward.z = std::cos(glm::radians(m_pitch)) * std::cos(glm::radians(m_yaw));
forward = glm::normalize(forward);

glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
glm::vec3 up    = glm::normalize(glm::cross(right, forward));
```

Use `dvec3` for position (world-space precision) but `mat4`/`vec3` for matrices/directions (GPU-compatible float precision). Cast position to `vec3` only when building the view matrix:

```cpp
glm::mat4 getViewMatrix() const
{
    glm::vec3 pos = glm::vec3(m_position);
    return glm::lookAt(pos, pos + getForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}
```

### Frustum Plane Extraction (Gribb-Hartmann Method)

Extract 6 planes from the combined VP matrix. Each plane is `ax + by + cz + d = 0`:

```cpp
std::array<glm::vec4, 6> extractFrustumPlanes() const
{
    glm::mat4 vp = getProjectionMatrix() * getViewMatrix();
    std::array<glm::vec4, 6> planes;
    // Left, Right, Bottom, Top, Near, Far
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);
    // Normalize each plane
    for (auto& p : planes) {
        float len = glm::length(glm::vec3(p));
        p /= len;
    }
    return planes;
}
```

Note: GLM is column-major. `vp[col][row]` -- so `vp[0][3]` is row 3 of column 0.

### GLFW Input Handling

GLFW callbacks must be static or use `glfwSetWindowUserPointer` for instance access. Since `Window` already sets itself as user pointer (for resize callback), use a struct to hold both Window and GameApp pointers, or use a separate mechanism.

**Recommended approach**: Set `glfwSetWindowUserPointer` to a struct containing all callback targets, or use GameApp as the user pointer since it owns the Window reference:

```cpp
// In GameApp::init(), after Window is created:
glfwSetWindowUserPointer(m_window.getHandle(), this);
glfwSetKeyCallback(m_window.getHandle(), &GameApp::keyCallback);
glfwSetCursorPosCallback(m_window.getHandle(), &GameApp::cursorPosCallback);
glfwSetMouseButtonCallback(m_window.getHandle(), &GameApp::mouseButtonCallback);

// Enable raw mouse motion
if (glfwRawMouseMotionSupported())
{
    glfwSetInputMode(m_window.getHandle(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}
glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
```

**WARNING**: `Window` currently sets itself as the user pointer in `framebufferSizeCallback`. Either:
1. Change Window to use a struct that holds both Window* and GameApp* as user pointer
2. Move the framebuffer resize callback to GameApp (recommended -- GameApp owns Window reference)
3. Use a global or thread-local callback dispatcher

**Recommended**: Move the user pointer ownership to GameApp. Store a struct:
```cpp
struct InputContext {
    game::Window* window;
    GameApp* app;
};
```

Or simpler: have GameApp be the user pointer and forward resize events to Window.

### ImGui GLFW Callback Integration

`ImGui_ImplGlfw_InitForVulkan(window, true)` with `true` installs ImGui's GLFW callbacks. These callbacks chain with previously installed callbacks. Order matters:
1. Set your own GLFW callbacks FIRST
2. Then call `ImGui_ImplGlfw_InitForVulkan(window, true)`

This way ImGui's callbacks are installed on top of yours. ImGui will process input first (for UI interaction) and then pass events through.

**When ImGui wants keyboard/mouse input** (`ImGui::GetIO().WantCaptureMouse`, `.WantCaptureKeyboard`), you should NOT process game input:

```cpp
void GameApp::processInput()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard)
    {
        // Process game keyboard input (WASD, etc.)
    }
    if (!io.WantCaptureMouse)
    {
        // Process game mouse input (camera look)
    }
}
```

### Wireframe Pipeline Variant

Create a second graphics pipeline identical to `m_pipeline` but with `VK_POLYGON_MODE_LINE`:

```cpp
rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
```

This requires `VkPhysicalDeviceFeatures::fillModeNonSolid` to be enabled. Check and enable this feature in VulkanContext device creation (add to vk-bootstrap device builder):

```cpp
VkPhysicalDeviceFeatures features{};
features.fillModeNonSolid = VK_TRUE;  // Required for wireframe
features.wideLines = VK_FALSE;        // Not needed, keep off
deviceBuilder.add_pNext(&features);   // or set_required_features()
```

If `fillModeNonSolid` is not supported (unlikely on desktop GPUs), skip wireframe pipeline creation and log a warning.

### Wireframe Toggle Flow

```
F4 pressed → m_wireframeMode = !m_wireframeMode
Renderer::draw():
  if (m_wireframeMode && m_wireframePipeline != VK_NULL_HANDLE)
      vkCmdBindPipeline(cmd, GRAPHICS, m_wireframePipeline);
  else
      vkCmdBindPipeline(cmd, GRAPHICS, m_pipeline);
```

ImGui always renders with its own pipeline (FILL mode), so the overlay is unaffected by F4.

### UX Spec Reference

From `ux-spec.md` Section 2 (Controls) and Section 7 (Camera):
- Mouse sensitivity: 0.1 deg/px default, configurable 0.01-0.5
- Pitch clamped +/-89 degrees, yaw wraps 0-360
- No mouse acceleration -- raw input only
- FOV: 70 default, configurable 50-110
- Near: 0.1, Far: render_distance * 16 + 64 (use 1000.0 default until render distance exists)
- WASD movement, Space=up, Shift=down (in fly mode)

From `ux-spec.md` Section 6 (Debug Overlay):
- Top-left corner, semi-transparent, monospace font
- Updated every frame for FPS/position
- Does NOT capture mouse input

### What This Story Does NOT Do

- **Does NOT pass VP matrices to shaders** -- the test triangle uses hardcoded positions. Push constants/UBO for camera matrices will be added when chunk rendering begins (Epic 6)
- **Does NOT render chunk boundaries** -- F5 flag is stored but actual rendering requires ChunkManager (Epic 3)
- **Does NOT show real chunk states** -- placeholder text in ImGui until ChunkManager exists
- **Does NOT create an InputManager class** -- direct GLFW callbacks in GameApp are sufficient for now. A full InputManager with rebindable keys is deferred to Epic 7
- **Does NOT add depth buffer** -- the test triangle doesn't need it, and the depth buffer will be added with the G-Buffer (Epic 6)
- **Does NOT wire Gigabuffer into Renderer** -- Gigabuffer exists as a class but is not instantiated in the render loop yet. ImGui shows "N/A" for memory stats until wired
- **Does NOT implement head bob, step-up, or physics-based movement** -- pure fly-mode only

### Anti-Patterns to Avoid

- **DO NOT** create a `VkRenderPass` for ImGui -- use `UseDynamicRendering = true`
- **DO NOT** call `ImGui_ImplVulkan_CreateFontsTexture()` manually -- as of ImGui 2025+, font atlas upload is automatic (handled by `ImGuiBackendFlags_RendererHasTextures`)
- **DO NOT** use vcpkg's `vulkan-binding` feature -- compile the Vulkan backend ourselves for VK_NO_PROTOTYPES compatibility
- **DO NOT** include `<vulkan/vulkan.h>` -- use `<volk.h>` exclusively
- **DO NOT** make Camera thread-safe -- it is only accessed from the main thread
- **DO NOT** store raw GLFW key states in the engine layer -- keep GLFW coupling in the game layer (GameApp)
- **DO NOT** use `glfwGetKey()` polling -- use callbacks for F3/F4/F5 toggles (polling misses rapid presses)
- **DO NOT** use `float` for camera world position -- use `dvec3` for precision at large coordinates
- **DO NOT** put ImGui logic inside `tick()` -- ImGui must run at render frequency, not tick frequency

### Project Structure Notes

All new files follow the existing mirror pattern:

| File | Path | Namespace | Action |
|------|------|-----------|--------|
| Camera.h | `engine/include/voxel/renderer/Camera.h` | `voxel::renderer` | CREATE |
| Camera.cpp | `engine/src/renderer/Camera.cpp` | `voxel::renderer` | CREATE |
| ImGuiBackend.h | `engine/include/voxel/renderer/ImGuiBackend.h` | `voxel::renderer` | CREATE |
| ImGuiBackend.cpp | `engine/src/renderer/ImGuiBackend.cpp` | `voxel::renderer` | CREATE |
| ImGuiImpl.cpp | `engine/src/renderer/ImGuiImpl.cpp` | -- | CREATE |
| TestCamera.cpp | `tests/renderer/TestCamera.cpp` | -- | CREATE |

### Existing Files to Modify

| File | Change |
|------|--------|
| `vcpkg.json` | Add imgui features: `docking-experimental` |
| `engine/CMakeLists.txt` | Add Camera.cpp, ImGuiBackend.cpp, ImGuiImpl.cpp; link imgui::imgui; add `<imgui.h>` to PCH |
| `engine/src/renderer/Renderer.h` | Add ImGuiBackend + wireframe pipeline members, add wireframe flag setter |
| `engine/src/renderer/Renderer.cpp` | Init ImGuiBackend, create wireframe pipeline, render ImGui in draw loop |
| `engine/src/renderer/VulkanContext.cpp` | Enable `fillModeNonSolid` physical device feature |
| `game/src/GameApp.h` | Add Camera, input state, callbacks, tick() override |
| `game/src/GameApp.cpp` | Wire input callbacks, camera update, pass state to Renderer |
| `game/src/main.cpp` | No changes needed (GameApp::init handles everything) |
| `tests/CMakeLists.txt` | Add TestCamera.cpp |

### Previous Story Intelligence

**From Story 2.4 (Gigabuffer -- DONE)**:
- `Gigabuffer` class exists at `engine/include/voxel/renderer/Gigabuffer.h`
- Provides `usedBytes()`, `freeBytes()`, `getCapacity()`, `allocationCount()`
- Class is implemented and committed but NOT instantiated in Renderer yet
- ImGui overlay should display "N/A" or placeholder for gigabuffer stats until it's wired

**From Story 2.3 (Test Triangle -- DONE)**:
- `Renderer::draw()` at `engine/src/renderer/Renderer.cpp:354-527` -- full frame loop reference
- `FrameData` struct: commandPool, commandBuffer, imageAvailableSemaphore, renderFence
- Pipeline created with dynamic rendering, VkPipelineRenderingCreateInfo in pNext
- `transitionImage()` helper for layout transitions (UNDEFINED->COLOR_ATTACHMENT, COLOR_ATTACHMENT->PRESENT)
- `vkQueueSubmit2` with sync2 for submission
- Clear color: `{0.1f, 0.1f, 0.1f, 1.0f}` (dark gray)

**From Story 2.2 (VulkanContext -- DONE)**:
- `VulkanContext` at `engine/include/voxel/renderer/VulkanContext.h` -- all device/queue accessors
- VMA initialized with BDA flag + volk import
- Swapchain format: `VK_FORMAT_B8G8R8A8_SRGB`
- Present mode: `VK_PRESENT_MODE_FIFO_KHR`
- Graphics queue + transfer queue (dedicated if available)

**From Window class**:
- `Window::getHandle()` returns `GLFWwindow*` for GLFW API calls
- `framebufferSizeCallback` uses `glfwSetWindowUserPointer` -- must coordinate with GameApp (see GLFW Input Handling section)
- `wasResized()` returns and resets the resize flag

**From GameLoop class**:
- `tick(double dt)` virtual -- 20 TPS, override in GameApp for camera update
- `render(double alpha)` virtual -- uncapped, override in GameApp for draw call
- Accumulator-based fixed timestep with spiral-of-death protection (max 0.25s)

### Git Intelligence

Recent commits follow `feat(renderer):` prefix convention:
```
87617ca feat(renderer): add Gigabuffer with VmaVirtualBlock sub-allocation
7f9d52f feat(renderer): integrate GLSL shader pipeline and Renderer with GameApp
37903e8 feat(renderer): add dynamic rendering pipeline with test triangle example
```

Follow same convention:
```
feat(renderer): add FPS camera and Dear ImGui debug overlay
```

### Testing Requirements

Camera unit tests are CPU-only and CI-eligible. Use Catch2 v3 with SECTION-based organization:

```cpp
TEST_CASE("Camera", "[renderer][camera]")
{
    voxel::renderer::Camera camera;

    SECTION("default state produces valid matrices") { ... }
    SECTION("pitch clamps at +/-89 degrees") { ... }
    SECTION("frustum planes are normalized") { ... }
    SECTION("forward/right/up are orthonormal") { ... }
    SECTION("WASD movement updates position") { ... }
}
```

ImGui integration is NOT unit tested -- verified manually via visual inspection and validation layer output.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-02-vulkan-bootstrap.md -- Story 2.6 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md -- System 5: Init Stack step 13, Vulkan Renderer]
- [Source: _bmad-output/planning-artifacts/ux-spec.md -- Sections 2, 6, 7: Controls, Debug Overlay, Camera Behavior]
- [Source: _bmad-output/project-context.md -- Naming conventions, error handling, GLM config, tech stack]
- [Source: engine/include/voxel/renderer/Renderer.h -- FrameData struct, Renderer API]
- [Source: engine/src/renderer/Renderer.cpp -- Draw loop, pipeline creation, sync2 submission pattern]
- [Source: engine/include/voxel/renderer/VulkanContext.h -- Device/queue accessors]
- [Source: engine/include/voxel/renderer/Gigabuffer.h -- Memory stats API]
- [Source: engine/include/voxel/game/Window.h -- getHandle(), framebuffer callback]
- [Source: engine/include/voxel/game/GameLoop.h -- tick/render virtual methods]
- [Source: game/src/GameApp.h -- Current GameApp structure]
- [Source: engine/CMakeLists.txt -- Current build targets and dependencies]
- [ImGui Dynamic Rendering: github.com/ocornut/imgui/issues/8326]
- [vkguide.dev ImGui Setup: vkguide.dev/docs/new_chapter_2/vulkan_imgui_setup/]
- [Gribb-Hartmann Frustum Extraction: www8.cs.umu.se/kurser/5DV180/VT18/lab/frustum.pdf]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
