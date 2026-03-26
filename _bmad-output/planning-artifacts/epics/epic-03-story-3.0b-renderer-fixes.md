# Story 3.0b — Renderer Fixes + Missing Features

**Priority**: P0 — do after Story 3.0a, before Story 3.3
**Epic**: 3 (inserted)
**Scope**: Fix Vulkan bugs (flushTransfers, swapchain), add depth buffer, add gameplay HUD (crosshair, hotbar), config persistence, window features (fullscreen, screenshot). All parts touch the renderer or GPU pipeline.
**Prerequisite**: Story 3.0a must be done first — Parts E and J depend on `beginFrame()`/`endFrame()` split from Part C, and Part I depends on InputManager from Part A.

---

## Part E: Fix `flushTransfers(VK_NULL_HANDLE)` — Wire Gigabuffer into Renderer

### The actual problem

`Renderer.cpp` line 711:
```cpp
auto flushResult = m_stagingBuffer->flushTransfers(VK_NULL_HANDLE);
```

`StagingBuffer::flushTransfers()` takes a `VkBuffer gigabuffer` parameter and calls `vkCmdCopyBuffer(cmd, m_buffer, gigabuffer, ...)`. Passing `VK_NULL_HANDLE` means any actual transfer would copy to a null buffer — undefined behavior / validation error.

This "works" now because `m_pendingTransfers` is always empty (nobody uploads anything yet), so the function returns early. But it's a ticking time bomb — Story 5.7 (Mesh Upload) will push transfers and hit this bug.

### What to do

The Gigabuffer already exists (Story 2.4, `Gigabuffer` class in `engine/include/voxel/renderer/Gigabuffer.h`). It just isn't wired into the Renderer.

1. Add `Gigabuffer` ownership to Renderer (or receive it by reference):
```cpp
// Renderer.h — add member:
std::unique_ptr<Gigabuffer> m_gigabuffer;
```

2. In `Renderer::init()`, create the Gigabuffer:
```cpp
auto gigaResult = Gigabuffer::create(m_vulkanContext);
if (!gigaResult) return std::unexpected(gigaResult.error());
m_gigabuffer = std::move(*gigaResult);
```

3. Fix the `flushTransfers` call:
```cpp
// Renderer.cpp — in draw() / endFrame():
auto flushResult = m_stagingBuffer->flushTransfers(m_gigabuffer->getBuffer());
```

4. In `Renderer::shutdown()`, destroy Gigabuffer before VMA cleanup:
```cpp
m_gigabuffer.reset();
```

5. Update the debug overlay to show real Gigabuffer stats:
```cpp
ImGui::Text("Gigabuffer: %zu / %zu MB (%.0f%%)",
    m_gigabuffer->usedBytes() / (1024*1024),
    m_gigabuffer->getCapacity() / (1024*1024),
    100.0f * m_gigabuffer->usedBytes() / m_gigabuffer->getCapacity());
```

---


---

## Part H: Improve Swapchain Recreation

### The actual problem

`VulkanContext.cpp` line 270:
```cpp
core::Result<void> VulkanContext::recreateSwapchain(game::Window& window)
{
    vkDeviceWaitIdle(m_device);  // ← Stalls the ENTIRE GPU
    // ... destroy old resources, create new ones
```

`vkDeviceWaitIdle` waits for ALL GPU queues to drain — every in-flight frame, every pending transfer. On a loaded renderer this causes a visible freeze (50-100ms+). The audit flagged this as "acceptable for now but not durable."

### What to do (minimal fix, not a full rewrite)

The full solution (per-frame old-swapchain retirement) is complex. For now, the minimal improvement:

1. **Don't idle on suboptimal** — only recreate on `VK_ERROR_OUT_OF_DATE_KHR`, not `VK_SUBOPTIMAL_KHR`. Suboptimal means "still works, just not ideal" and doesn't need recreation.

2. **Defer recreation to frame start** — instead of recreating immediately in the present callback, set a flag and recreate at the start of the next `beginFrame()`, after the in-flight frame's fence is waited on anyway:

```cpp
// Renderer.h — add:
bool m_needsSwapchainRecreate = false;

// In present (where OUT_OF_DATE is detected):
m_needsSwapchainRecreate = true;

// In beginFrame(), AFTER vkWaitForFences:
if (m_needsSwapchainRecreate) {
    vkDeviceWaitIdle(m_device); // Still idle, but now only 1 frame in flight instead of N
    m_vulkanContext.recreateSwapchain(window);
    recreateRenderFinishedSemaphores();
    m_needsSwapchainRecreate = false;
    return; // Skip this frame
}
```

3. **Encapsulate swapchain-dependent resources** — collect all resources that depend on swapchain extent into a struct so recreation is one function call, not scattered:

```cpp
struct SwapchainResources {
    // Currently: just image views (owned by VulkanContext)
    // Future (Story 6.6): G-Buffer attachments, depth buffer
    // Recreated together on swapchain resize
};
```

This doesn't eliminate the `vkDeviceWaitIdle` entirely but reduces its impact and prepares the architecture for a proper per-frame retirement pattern when the renderer is more complex.

---


---

## Part J: Add Depth Buffer

### The actual problem

Story 2.3 rendered a triangle without depth testing — acceptable for a single flat triangle. But when chunks arrive (Epic 5-6), every face will z-fight without a depth buffer. No story between 2.3 and 6.6 (G-Buffer with D32_SFLOAT) creates the depth buffer. The chunk rendering pipeline will produce visual garbage without it.

### What to do

Add a depth image to the Renderer, created during `init()`, recreated on swapchain resize:

```cpp
// In Renderer — new members:
VkImage m_depthImage = VK_NULL_HANDLE;
VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
VkImageView m_depthImageView = VK_NULL_HANDLE;

// createDepthResources():
VkImageCreateInfo depthInfo{};
depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
depthInfo.imageType = VK_IMAGE_TYPE_2D;
depthInfo.format = VK_FORMAT_D32_SFLOAT;
depthInfo.extent = { swapchainExtent.width, swapchainExtent.height, 1 };
depthInfo.mipLevels = 1;
depthInfo.arrayLayers = 1;
depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

VmaAllocationCreateInfo allocInfo{};
allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

vmaCreateImage(allocator, &depthInfo, &allocInfo, &m_depthImage, &m_depthAllocation, nullptr);
// Create image view with VK_IMAGE_ASPECT_DEPTH_BIT
```

Wire into the dynamic rendering in `beginFrame()`:
```cpp
VkRenderingAttachmentInfo depthAttachment{};
depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
depthAttachment.imageView = m_depthImageView;
depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

renderingInfo.pDepthAttachment = &depthAttachment;
```

Update pipeline creation (in `buildPipeline` from Part B):
```cpp
VkPipelineDepthStencilStateCreateInfo depthStencil{};
depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
depthStencil.depthTestEnable = VK_TRUE;
depthStencil.depthWriteEnable = VK_TRUE;
depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
// Add to PipelineConfig and VkGraphicsPipelineCreateInfo
```

On swapchain recreate: destroy + recreate depth image at new resolution.
Add to `SwapchainResources` struct (Part H) for clean recreation.

---

## Part I: HUD, Config Persistence, Window Features

### The actual problem

Story 2.6 shipped camera + ImGui overlay but skipped the gameplay HUD (crosshair, hotbar) and quality-of-life features (config save, screenshot, fullscreen). These aren't in any other story. Without them:
- The player has no visual feedback for where they're aiming (crosshair missing)
- The player can't see which block type is selected (hotbar missing)
- Every restart resets FOV, sensitivity, and render distance to defaults (no config persistence)
- No way to capture gameplay (no screenshot)
- No way to go fullscreen (no F11)

### Crosshair + Hotbar HUD

**Crosshair:**
- White `+` symbol, 2px lines, 10px each arm, centered on screen
- Rendered via ImGui (`ImGui::GetForegroundDrawList()->AddLine(...)`) — no separate draw call needed
- Always visible when cursor is captured, hidden when ImGui has mouse

**Hotbar:**
- 9 slots at bottom center, 48×48 px each, 4px gap
- Semi-transparent dark background (ImGui `ImDrawList::AddRectFilled` with rgba 0,0,0,0.6)
- Selected slot: brighter border (white 2px)
- V1: block type name rendered as ImGui text inside each slot (no 3D item preview yet)
- Keys 1–9 select slot (already handled by InputManager from Part A), scroll wheel cycles
- Both rendered in `GameApp::buildDebugOverlay()` (after Part C moved it there) — the hotbar is always visible, not just in F3 mode

### Config file persistence

- `ConfigManager` class in `engine/include/voxel/core/ConfigManager.h`
- `load(path) → Result<void>` — reads `config.json`, applies values to systems
- `save(path)` — writes current settings to `config.json`
- Settings stored:

```json
{
    "window": {
        "width": 1280,
        "height": 720,
        "fullscreen": false
    },
    "graphics": {
        "fov": 70.0,
        "render_distance": 16
    },
    "input": {
        "mouse_sensitivity": 0.1
    },
    "world": {
        "seed": 8675309,
        "last_player_position": [127.5, 72.0, -56.0]
    }
}
```

- On startup: load before window creation (so window size is correct)
- On shutdown: save after game loop exits, before Vulkan cleanup
- Format: manual JSON write using spdlog's fmt (no new dependency needed — just `fprintf` a known structure). Or use the JSON parser already pulled in for Story 3.3 (if applicable)
- GameApp holds `ConfigManager`, passes values to Camera, Renderer, etc. at init

### Window features

**Fullscreen toggle (F11):**
```cpp
void Window::toggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        // Save windowed size for restore
        glfwGetWindowPos(m_window, &m_savedX, &m_savedY);
        glfwGetWindowSize(m_window, &m_savedWidth, &m_savedHeight);
        // Switch to borderless fullscreen on primary monitor
        auto* monitor = glfwGetPrimaryMonitor();
        auto* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_savedX, m_savedY, m_savedWidth, m_savedHeight, 0);
    }
}
```
- F11 handled in GameApp tick via `m_input.wasKeyPressed(GLFW_KEY_F11)`
- Triggers swapchain recreation (already handled by the resize path from Part H)

**Screenshot (F2):**
```cpp
void Renderer::takeScreenshot(const std::string& directory) {
    // 1. Create a HOST_VISIBLE staging image
    // 2. vkCmdCopyImage from swapchain image to staging image (after present, before next frame)
    // 3. Map staging image, copy pixels to CPU buffer
    // 4. stbi_write_png(path, width, height, 4, pixels, stride)
    // 5. Cleanup staging image
}
```
- F2 sets a flag `m_screenshotRequested = true` in GameApp
- Renderer captures after `endFrame()` on the next frame
- Saved to `screenshots/screenshot_YYYY-MM-DD_HH-MM-SS.png`
- `stb_image_write.h` already available via `stb` vcpkg dependency
- Log: `"Screenshot saved: screenshots/screenshot_2026-03-26_14-30-45.png"`
- Create `screenshots/` directory if it doesn't exist (`std::filesystem::create_directories`)

---


---

## Definition of Done

- [ ] `flushTransfers` receives `m_gigabuffer->getBuffer()`, not VK_NULL_HANDLE
- [ ] Gigabuffer created in `Renderer::init()`, stats shown in debug overlay
- [ ] Swapchain recreation deferred to frame start via flag, skip on VK_SUBOPTIMAL_KHR
- [ ] `SwapchainResources` struct groups all extent-dependent resources
- [ ] Depth buffer (D32_SFLOAT) created, wired into dynamic rendering, depth test ON, recreated on resize
- [ ] Pipeline creation (from 3.0a Part B) includes `VkPipelineDepthStencilStateCreateInfo`
- [ ] Crosshair rendered centered on screen via ImGui foreground draw list
- [ ] Hotbar 9 slots rendered at bottom center, keys 1-9 + scroll select slot
- [ ] `ConfigManager` loads `config.json` on startup, saves on shutdown
- [ ] Settings persisted: FOV, sensitivity, render distance, window size, fullscreen, seed, player position
- [ ] F11 toggles borderless fullscreen
- [ ] F2 saves screenshot as PNG to `screenshots/` with timestamp filename
- [ ] All existing tests still pass
- [ ] Triangle still renders correctly with depth test (no z-fighting)
- [ ] No visual or functional regressions
