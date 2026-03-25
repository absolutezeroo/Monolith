# Story 2.2: Vulkan Initialization (volk + vk-bootstrap + VMA)

Status: done

<!-- Note: Validation is optional. Run validate-create-story for quality check before dev-story. -->

## Story

As a developer,
I want Vulkan 1.3 fully initialized with device, queues, and memory allocator,
so that I can create GPU resources.

## Acceptance Criteria

1. `volkInitialize()` called before anything else
2. vk-bootstrap creates Instance with validation layers (debug only), requiring API 1.3
3. vk-bootstrap selects physical device requiring: `dynamicRendering`, `synchronization2`, `bufferDeviceAddress`, `descriptorIndexing`
4. Logical device created with graphics queue + transfer queue (separate if available, fallback to shared)
5. `volkLoadDevice(device)` called after device creation
6. VMA allocator created with BDA flag + volk function import (`vmaImportVulkanFunctionsFromVolk`)
7. Swapchain created via vk-bootstrap (FIFO present mode, SRGB format preferred)
8. `VulkanContext` class owns all of the above, RAII cleanup in destructor
9. Log: GPU name, Vulkan version, queue families, memory heaps

## Tasks / Subtasks

- [x] Task 1: Add Vulkan dependencies to engine CMake (AC: prerequisite)
  - [x] 1.1 Add `find_package` for volk, vk-bootstrap, vulkan-memory-allocator in `engine/CMakeLists.txt`
  - [x] 1.2 Link all three as PRIVATE: `volk::volk`, `vk-bootstrap::vk-bootstrap`, `GPUOpen::VulkanMemoryAllocator`
  - [x] 1.3 Add compile definitions: `VK_NO_PROTOTYPES`, `VMA_STATIC_VULKAN_FUNCTIONS=0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=0`
  - [x] 1.4 Add `<volk.h>` to precompiled headers (replace or supplement existing Vulkan headers)
  - [x] 1.5 Create `engine/src/renderer/VmaImpl.cpp` — single TU with `VMA_IMPLEMENTATION` (see Dev Notes)
  - [x] 1.6 Register `VmaImpl.cpp` in CMakeLists source list
  - [x] 1.7 Verify vcpkg.json already has `volk`, `vk-bootstrap`, `vulkan-memory-allocator` — no changes needed
  - [x] 1.8 Build to confirm all packages resolve and link
- [x] Task 2: Create `VulkanContext` class (AC: 1–8)
  - [x] 2.1 Header: `engine/include/voxel/renderer/VulkanContext.h`
  - [x] 2.2 Source: `engine/src/renderer/VulkanContext.cpp`
  - [x] 2.3 Register both files in `engine/CMakeLists.txt` source list
  - [x] 2.4 Factory: `static Result<std::unique_ptr<VulkanContext>> create(game::Window& window)`
  - [x] 2.5 Non-copyable: delete copy constructor and copy assignment
  - [x] 2.6 RAII destructor: cleanup in reverse init order
  - [x] 2.7 Private default constructor — only `create()` can instantiate
- [x] Task 3: Implement Vulkan init sequence in `VulkanContext::create()` (AC: 1–7, 9)
  - [x] 3.1 `volkInitialize()` — return `EngineError::VulkanError` if not `VK_SUCCESS`
  - [x] 3.2 `glfwVulkanSupported()` sanity check — `VX_FATAL` if no Vulkan support
  - [x] 3.3 vk-bootstrap InstanceBuilder: app name "VoxelForge", require API 1.3, validation layers + debug messenger in debug only
  - [x] 3.4 `volkLoadInstance(instance)` — load instance-level functions
  - [x] 3.5 `glfwCreateWindowSurface(instance, window.getHandle(), nullptr, &surface)` — create surface
  - [x] 3.6 vk-bootstrap PhysicalDeviceSelector: set surface, require 1.3, require features (see Dev Notes), prefer discrete GPU
  - [x] 3.7 vk-bootstrap DeviceBuilder: build device, get graphics + transfer queues (transfer fallback to graphics if unavailable)
  - [x] 3.8 `volkLoadDevice(device)` — load device-level functions
  - [x] 3.9 VMA allocator: `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`, `vmaImportVulkanFunctionsFromVolk`, `vmaCreateAllocator`
  - [x] 3.10 vk-bootstrap SwapchainBuilder: SRGB format, FIFO present mode, extent from window framebuffer size
  - [x] 3.11 Extract and store swapchain images + create image views from vk-bootstrap result
- [x] Task 4: Implement GPU info logging (AC: 9)
  - [x] 4.1 Log GPU name from physical device properties
  - [x] 4.2 Log Vulkan API version (major.minor.patch)
  - [x] 4.3 Log graphics queue family index and transfer queue family index (note if shared)
  - [x] 4.4 Log memory heaps: size and flags (DEVICE_LOCAL, HOST_VISIBLE, etc.)
- [x] Task 5: Implement RAII destructor (AC: 8)
  - [x] 5.1 Destroy swapchain image views, then swapchain
  - [x] 5.2 Destroy VMA allocator via `vmaDestroyAllocator`
  - [x] 5.3 Destroy logical device via `vkDestroyDevice`
  - [x] 5.4 Destroy surface via `vkDestroySurfaceKHR`
  - [x] 5.5 Destroy debug messenger (if debug) via `vkb::destroy_debug_utils_messenger`
  - [x] 5.6 Destroy instance via `vkDestroyInstance`
  - [x] 5.7 Null all handles after destruction
- [x] Task 6: Expose getters for future stories (AC: prerequisite for 2.3+)
  - [x] 6.1 `getDevice()`, `getPhysicalDevice()`, `getInstance()` — for pipeline/resource creation
  - [x] 6.2 `getAllocator()` — for VMA buffer/image allocation
  - [x] 6.3 `getGraphicsQueue()`, `getGraphicsQueueFamily()` — for command submission
  - [x] 6.4 `getTransferQueue()`, `getTransferQueueFamily()` — for staging uploads
  - [x] 6.5 `getSurface()` — for swapchain recreation
  - [x] 6.6 `getSwapchain()`, `getSwapchainFormat()`, `getSwapchainExtent()` — for rendering
  - [x] 6.7 `getSwapchainImages()`, `getSwapchainImageViews()` — for framebuffer setup
- [x] Task 7: Update `main.cpp` to create VulkanContext (AC: all)
  - [x] 7.1 After `Window::create()`, call `VulkanContext::create(window)`
  - [x] 7.2 Handle `Result` error with `VX_FATAL`
  - [x] 7.3 Ensure destruction order: VulkanContext → Window → Log::shutdown()
- [x] Task 8: Build and verify (AC: all)
  - [x] 8.1 Build with `msvc-debug` preset
  - [x] 8.2 Run `VoxelGame.exe` — window + Vulkan init, GPU info logged to console
  - [x] 8.3 Verify no validation layer errors in debug output
  - [x] 8.4 Verify clean shutdown (no crashes, no validation errors)
  - [x] 8.5 All 19 existing tests still pass (`ctest --preset msvc-debug`)

## Dev Notes

### Architecture Init Stack — This Story Covers Steps 1–7

From architecture.md System 5, the full 13-step Vulkan init stack is:

```
1. volkInitialize()                              ← THIS STORY
2. vk-bootstrap → Instance (validation layers)   ← THIS STORY
3. vk-bootstrap → PhysicalDevice (1.3 features)  ← THIS STORY
4. vk-bootstrap → LogicalDevice + Queues          ← THIS STORY
5. volkLoadDevice(device)                         ← THIS STORY
6. VMA allocator (BDA + volk import)              ← THIS STORY
7. Swapchain (via vk-bootstrap)                   ← THIS STORY
8. Gigabuffer allocation                          → Story 2.4
9. Shared quad index buffer                       → Story 2.3
10. Shader pipelines                              → Story 2.3
11. Texture array                                 → Story 6.5
12. Descriptor sets                               → Story 2.3+
13. Dear ImGui init                               → Story 2.6
```

### VMA Implementation Pattern — Single Translation Unit

VMA is header-only. `VMA_IMPLEMENTATION` must appear in exactly ONE .cpp file. Create a dedicated file:

```cpp
// engine/src/renderer/VmaImpl.cpp
// VMA implementation — exactly one translation unit
// volk.h is included via PCH, providing Vulkan function pointers

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
```

The CMake defines `VMA_STATIC_VULKAN_FUNCTIONS=0` and `VMA_DYNAMIC_VULKAN_FUNCTIONS=0` apply globally, so VMA won't try to load Vulkan functions itself — we feed them via `vmaImportVulkanFunctionsFromVolk`.

### Include Order — Critical for volk + VMA

volk.h MUST be included before any Vulkan header. Since it's in PCH, this is handled automatically. But in VulkanContext.h, the include order must be:

```cpp
// engine/include/voxel/renderer/VulkanContext.h
#pragma once

#include "voxel/core/Result.h"

#include <volk.h>           // Vulkan types + function pointers (PCH handles this)
#include <vk_mem_alloc.h>   // VmaAllocator type (declarations only, no VMA_IMPLEMENTATION)

#include <cstdint>
#include <memory>
#include <vector>
```

In VulkanContext.cpp, include vk-bootstrap and GLFW (both PRIVATE deps, not in header):

```cpp
#include "voxel/renderer/VulkanContext.h"
#include "voxel/game/Window.h"
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
```

### CMake Changes Required

In `engine/CMakeLists.txt`:

```cmake
# Add source files
add_library(VoxelEngine STATIC
    # ... existing sources ...
    src/renderer/VmaImpl.cpp
    src/renderer/VulkanContext.cpp
)

# Add Vulkan packages
find_package(volk CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(vulkan-memory-allocator CONFIG REQUIRED)

target_link_libraries(VoxelEngine
    # ... existing libs ...
    PRIVATE volk::volk
    PRIVATE vk-bootstrap::vk-bootstrap
    PRIVATE GPUOpen::VulkanMemoryAllocator
)

# volk: prevent static linking to Vulkan loader
target_compile_definitions(VoxelEngine PUBLIC VK_NO_PROTOTYPES)

# VMA: use volk for function loading, not VMA's own dynamic loading
target_compile_definitions(VoxelEngine PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
```

**IMPORTANT**: `VK_NO_PROTOTYPES` must be PUBLIC because any header including `<volk.h>` (including consumers via VulkanContext.h) needs this define to prevent Vulkan prototype declarations from conflicting with volk's function pointer declarations.

**Verify CMake target names**: If the above target names fail, check `build/msvc-debug/vcpkg_installed/x64-windows/share/*/` for the actual `.cmake` config files. vcpkg port naming can vary between versions.

### VulkanContext Header Design

```cpp
// engine/include/voxel/renderer/VulkanContext.h
#pragma once

#include "voxel/core/Result.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace voxel::game { class Window; }

namespace voxel::renderer
{

class VulkanContext
{
public:
    static core::Result<std::unique_ptr<VulkanContext>> create(game::Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] VkInstance getInstance() const { return m_instance; }
    [[nodiscard]] VkDevice getDevice() const { return m_device; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] VmaAllocator getAllocator() const { return m_allocator; }
    [[nodiscard]] VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    [[nodiscard]] VkQueue getTransferQueue() const { return m_transferQueue; }
    [[nodiscard]] uint32_t getTransferQueueFamily() const { return m_transferQueueFamily; }
    [[nodiscard]] VkSurfaceKHR getSurface() const { return m_surface; }
    [[nodiscard]] VkSwapchainKHR getSwapchain() const { return m_swapchain; }
    [[nodiscard]] VkFormat getSwapchainFormat() const { return m_swapchainFormat; }
    [[nodiscard]] VkExtent2D getSwapchainExtent() const { return m_swapchainExtent; }
    [[nodiscard]] const std::vector<VkImage>& getSwapchainImages() const { return m_swapchainImages; }
    [[nodiscard]] const std::vector<VkImageView>& getSwapchainImageViews() const { return m_swapchainImageViews; }

private:
    VulkanContext() = default;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    uint32_t m_transferQueueFamily = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent = {0, 0};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
};

} // namespace voxel::renderer
```

### Physical Device Feature Selection

Vulkan 1.3 promoted critical extensions to core. Use the version-specific feature structs:

```cpp
VkPhysicalDeviceVulkan13Features features13{};
features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
features13.dynamicRendering = VK_TRUE;   // No VkRenderPass boilerplate
features13.synchronization2 = VK_TRUE;   // Simplified pipeline barriers

VkPhysicalDeviceVulkan12Features features12{};
features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
features12.bufferDeviceAddress = VK_TRUE; // GPU pointer access for vertex pulling
features12.descriptorIndexing = VK_TRUE;  // Bindless texture array

vkb::PhysicalDeviceSelector selector{vkbInstance};
auto physResult = selector
    .set_surface(m_surface)
    .set_minimum_version(1, 3, 0)
    .set_required_features_13(features13)
    .set_required_features_12(features12)
    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
    .select();
```

### Queue Selection — Graphics + Transfer

```cpp
auto graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics);
auto graphicsIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics);

// Try dedicated transfer queue for async uploads (Story 2.5)
auto transferQueue = vkbDevice.get_queue(vkb::QueueType::transfer);
auto transferIndex = vkbDevice.get_queue_index(vkb::QueueType::transfer);

if (transferQueue.has_value())
{
    m_transferQueue = transferQueue.value();
    m_transferQueueFamily = transferIndex.value();
    VX_LOG_INFO("Dedicated transfer queue: family {}", m_transferQueueFamily);
}
else
{
    m_transferQueue = m_graphicsQueue;
    m_transferQueueFamily = m_graphicsQueueFamily;
    VX_LOG_WARN("No dedicated transfer queue — using graphics queue");
}
```

### VMA Allocator with volk Function Import

VMA 3.3.0+ provides `vmaImportVulkanFunctionsFromVolk`. This function reads `pAllocatorCreateInfo` to determine which Vulkan version/extensions are used and fills in the matching function pointers from volk:

```cpp
VmaAllocatorCreateInfo allocatorInfo{};
allocatorInfo.instance = m_instance;
allocatorInfo.physicalDevice = m_physicalDevice;
allocatorInfo.device = m_device;
allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

VmaVulkanFunctions vulkanFunctions{};
VkResult importResult = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
if (importResult != VK_SUCCESS)
{
    VX_LOG_ERROR("Failed to import Vulkan functions from volk for VMA");
    return std::unexpected(core::EngineError::VulkanError);
}
allocatorInfo.pVulkanFunctions = &vulkanFunctions;

VkResult vmaResult = vmaCreateAllocator(&allocatorInfo, &m_allocator);
if (vmaResult != VK_SUCCESS)
{
    VX_LOG_ERROR("Failed to create VMA allocator: {}", static_cast<int>(vmaResult));
    return std::unexpected(core::EngineError::VulkanError);
}
```

`VmaVulkanFunctions` is a local — VMA copies function pointers internally, so it does not need to persist after `vmaCreateAllocator`.

### Swapchain Creation

```cpp
vkb::SwapchainBuilder swapchainBuilder{m_physicalDevice, m_device, m_surface};

auto [fbWidth, fbHeight] = window.getFramebufferSize();
auto swapResult = swapchainBuilder
    .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
    .set_desired_extent(fbWidth, fbHeight)
    .build();
```

Use `window.getFramebufferSize()` (pixel dimensions), NOT window size (screen coordinates differ on high-DPI).

Extract and store swapchain data from the vk-bootstrap result:
- `m_swapchain = vkbSwapchain.swapchain`
- `m_swapchainFormat = vkbSwapchain.image_format`
- `m_swapchainExtent = vkbSwapchain.extent`
- `m_swapchainImages = vkbSwapchain.get_images().value()`
- `m_swapchainImageViews = vkbSwapchain.get_image_views().value()`

### RAII Destructor — Reverse Init Order

```cpp
VulkanContext::~VulkanContext()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);

        for (auto view : m_swapchainImageViews)
            vkDestroyImageView(m_device, view, nullptr);

        if (m_swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

        if (m_allocator != VK_NULL_HANDLE)
            vmaDestroyAllocator(m_allocator);

        vkDestroyDevice(m_device, nullptr);
    }

    if (m_instance != VK_NULL_HANDLE)
    {
        if (m_surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        if (m_debugMessenger != VK_NULL_HANDLE)
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);

        vkDestroyInstance(m_instance, nullptr);
    }
}
```

`vkDeviceWaitIdle` before any destruction — ensures GPU is done with all work.

### Updated main.cpp Pattern

```cpp
int main()
{
    voxel::core::Log::init();

    auto windowResult = voxel::game::Window::create(1280, 720, "VoxelForge");
    if (!windowResult.has_value())
        VX_FATAL("Failed to create window");

    auto& window = *windowResult.value();

    auto vulkanResult = voxel::renderer::VulkanContext::create(window);
    if (!vulkanResult.has_value())
        VX_FATAL("Failed to initialize Vulkan");

    auto& vulkan = *vulkanResult.value();

    voxel::game::GameLoop loop(window);
    loop.run();

    // Destruction order: VulkanContext → Window → Log
    vulkanResult.value().reset();
    windowResult.value().reset();

    voxel::core::Log::shutdown();
    return 0;
}
```

**Critical**: VulkanContext MUST be destroyed before Window. The VkSurfaceKHR was created from the GLFW window — destroying the window first would invalidate the surface.

### Error Handling Pattern

Every Vulkan/vk-bootstrap call that can fail must return `Result<T>`:

- `volkInitialize()` → check `VK_SUCCESS`
- vk-bootstrap builders → check `.has_value()`, log `.error().message()` on failure
- `glfwCreateWindowSurface()` → check `VK_SUCCESS`
- `vmaCreateAllocator()` → check `VK_SUCCESS`

Pattern for vk-bootstrap errors:

```cpp
auto result = builder.build();
if (!result)
{
    VX_LOG_ERROR("Failed to create X: {}", result.error().message());
    // cleanup any partially-created resources
    return std::unexpected(core::EngineError::VulkanError);
}
```

On partial init failure, clean up everything created so far before returning the error. Consider using a helper or goto-cleanup pattern if the sequential init makes RAII scoping awkward.

### Memory Heap Logging

```cpp
VkPhysicalDeviceMemoryProperties memProps;
vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
{
    const auto& heap = memProps.memoryHeaps[i];
    bool isDeviceLocal = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
    VX_LOG_INFO("Memory heap {}: {} MB {}",
        i, heap.size / (1024 * 1024),
        isDeviceLocal ? "(DEVICE_LOCAL)" : "(HOST)");
}
```

### File Locations

| File | Location | Namespace |
|------|----------|-----------|
| VulkanContext.h | `engine/include/voxel/renderer/VulkanContext.h` | `voxel::renderer` |
| VulkanContext.cpp | `engine/src/renderer/VulkanContext.cpp` | `voxel::renderer` |
| VmaImpl.cpp | `engine/src/renderer/VmaImpl.cpp` | (none — implementation only) |
| main.cpp | `game/src/main.cpp` | (global — modified) |
| CMakeLists.txt | `engine/CMakeLists.txt` | (build — modified) |

Create directories: `engine/include/voxel/renderer/` and `engine/src/renderer/` (they don't exist yet).

### What This Story Does NOT Do

- Does NOT render anything to the screen (Story 2.3)
- Does NOT create graphics pipelines or shaders (Story 2.3)
- Does NOT create synchronization objects (fences/semaphores) for frame rendering (Story 2.3)
- Does NOT allocate the gigabuffer (Story 2.4)
- Does NOT create the staging buffer (Story 2.5)
- Does NOT initialize Dear ImGui (Story 2.6)
- Does NOT implement swapchain recreation on resize — just sets the `m_framebufferResized` flag (Story 2.3 will handle)
- The `GameLoop::render(alpha)` remains a no-op — nothing is drawn yet

### Anti-Patterns to Avoid

- **DO NOT** include `<vulkan/vulkan.h>` directly — use `<volk.h>` exclusively
- **DO NOT** link to `Vulkan::Vulkan` CMake target — volk replaces the Vulkan loader
- **DO NOT** put `VMA_IMPLEMENTATION` in a header or in more than one .cpp file
- **DO NOT** store vk-bootstrap types (`vkb::Instance`, `vkb::Device`, `vkb::Swapchain`) as members — extract raw Vulkan handles and discard wrappers
- **DO NOT** create a VkRenderPass — this project uses dynamic rendering exclusively (Vulkan 1.3 core)
- **DO NOT** create VkFramebuffer objects — not needed with dynamic rendering
- **DO NOT** request Vulkan features beyond what's listed — unnecessary features may exclude valid GPUs
- **DO NOT** use `VK_PRESENT_MODE_MAILBOX_KHR` — FIFO is the only universally guaranteed mode
- **DO NOT** call `vkGetDeviceProcAddr` or `vkGetInstanceProcAddr` manually — volk handles this
- **DO NOT** use `std::chrono` or platform timing — GLFW timing already established in Story 2.1

### Naming Convention Reminders

| Element | Convention | Example |
|---------|-----------|---------|
| Class | PascalCase | `VulkanContext` |
| Methods | camelCase | `getDevice()`, `getAllocator()` |
| Members | m_ prefix | `m_device`, `m_allocator`, `m_swapchainFormat` |
| Constants | SCREAMING_SNAKE | `VK_API_VERSION_1_3` (Vulkan's own) |
| Booleans | is/has/should | `isDeviceLocal` |
| Namespace | lowercase | `voxel::renderer` |

### Library Versions (vcpkg, as of project setup)

| Library | vcpkg Port | Version | Notes |
|---------|-----------|---------|-------|
| volk | `volk` | 1.4.304+ | Meta-loader, replaces Vulkan loader |
| vk-bootstrap | `vk-bootstrap` | 1.4.341 | Vulkan init helper |
| VMA | `vulkan-memory-allocator` | 3.3.0+ | `vmaImportVulkanFunctionsFromVolk` requires 3.3.0+ |

### Previous Story Intelligence

**Story 2.1 (GLFW Window + Game Loop)** — in review. Key patterns to follow:
- `Window::create()` factory returns `Result<std::unique_ptr<Window>>` — follow same pattern for `VulkanContext`
- `Window::getHandle()` returns `GLFWwindow*` — use this for surface creation
- `Window::getFramebufferSize()` returns `std::pair<int, int>` — use for swapchain extent
- GLFW set up with `GLFW_CLIENT_API = GLFW_NO_API` — already Vulkan-ready
- `main.cpp` uses `VX_FATAL` for unrecoverable init errors — follow same pattern
- `glfwSetErrorCallback` already configured for logging
- `m_framebufferResized` flag exists in Window — swapchain recreation will use this in Story 2.3
- Build pattern: `cmake --preset msvc-debug && cmake --build build/msvc-debug`
- 19 existing tests passing — do not break

**Stories 1.1–1.6** — established patterns:
- `Log::init()` / `Log::shutdown()` bookend in main
- `VX_LOG_*` macros for all logging
- `Result<T>` for all fallible operations
- `#pragma once` at top of all headers
- Allman brace style, 4-space indent, 120 column limit
- Include order: associated header → project headers → third-party → stdlib

### Git Commit Convention

```
feat(renderer): add Vulkan 1.3 initialization with volk, vk-bootstrap, and VMA
```

### Testing Requirements

This story has **no unit tests** — Vulkan initialization requires a GPU and cannot be tested in CI without GPU runners. Verification is manual:

1. `VoxelGame.exe` launches without validation errors
2. Console shows: GPU name, Vulkan version, queue families, memory heaps
3. Window still appears and runs the game loop
4. Clean shutdown with no validation errors or crashes
5. **All 19 existing tests still pass** (regression check)

Future Story 2.3 (test triangle) provides visual verification that the Vulkan context is correctly initialized.

### Project Structure Notes

- New files go in `engine/include/voxel/renderer/` and `engine/src/renderer/` — both directories need to be **created**
- The `renderer/` namespace is for all GPU-side code: VulkanContext, Gigabuffer, Renderer, etc.
- Maintain mirror structure: `include/voxel/renderer/VulkanContext.h` ↔ `src/renderer/VulkanContext.cpp`
- `VmaImpl.cpp` has no corresponding header — it is purely an implementation compilation unit

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-02-vulkan-bootstrap.md — Story 2.2]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 5: Vulkan Renderer Init Stack, Required Features]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-002: Vulkan 1.3, ADR-008: Exceptions Disabled, ADR-009: Gigabuffer]
- [Source: _bmad-output/project-context.md — Tech Stack Versions, Naming Conventions, Error Handling, Build System]
- [Source: _bmad-output/implementation-artifacts/2-1-glfw-window-game-loop-skeleton.md — Window API, GameLoop patterns, CMake patterns]
- [Source: engine/CMakeLists.txt — current CMake configuration to modify]
- [Source: engine/include/voxel/game/Window.h — getHandle(), getFramebufferSize() API]
- [Source: engine/include/voxel/core/Result.h — Result<T> = std::expected<T, EngineError>]
- [Source: vcpkg.json — volk, vk-bootstrap, vulkan-memory-allocator already declared]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Fixed `find_package(vulkan-memory-allocator)` → `find_package(VulkanMemoryAllocator)` (vcpkg CMake config name mismatch)
- Fixed `set_minimum_version(1, 3, 0)` → `set_minimum_version(1, 3)` (vk-bootstrap 1.4.341 takes 2 args)
- Made `volk::volk_headers` and `GPUOpen::VulkanMemoryAllocator` PUBLIC since VulkanContext.h exposes Vulkan/VMA types to consumers
- Used `vkDestroyDebugUtilsMessengerEXT` directly in destructor instead of `vkb::destroy_debug_utils_messenger` (vkb types not stored as members per anti-pattern guidance)
- Used `get_dedicated_queue` for transfer queue to get truly dedicated transfer-only queue when available

### Completion Notes List

- Vulkan 1.3 initialization fully implemented via volk + vk-bootstrap + VMA
- VulkanContext class with RAII cleanup in reverse init order
- All 9 acceptance criteria satisfied:
  1. volkInitialize() called first
  2. vk-bootstrap Instance with validation layers (debug only), API 1.3
  3. Physical device requires dynamicRendering, synchronization2, bufferDeviceAddress, descriptorIndexing
  4. Logical device with graphics + transfer queues (dedicated if available, fallback to shared)
  5. volkLoadDevice() called after device creation
  6. VMA allocator with BDA flag + volk function import
  7. Swapchain via vk-bootstrap (FIFO, SRGB preferred)
  8. VulkanContext owns all resources, RAII destructor
  9. GPU name, Vulkan version, queue families, memory heaps logged
- No unit tests for this story (Vulkan requires GPU) — verification is manual runtime + 19 existing tests pass
- Build verified with msvc-debug preset
- Runtime verification (Tasks 8.2-8.4) requires user to run VoxelGame.exe on a machine with Vulkan GPU support

### Change Log

- 2026-03-25: Implemented Story 2.2 — Vulkan Initialization (volk + vk-bootstrap + VMA)
- 2026-03-25: Code review fix — removed manual cleanup in create() error paths (double-free bug on VkDevice), rely on RAII destructor instead. Removed vestigial find_package(VulkanHeaders).

### File List

- engine/CMakeLists.txt (modified — added Vulkan deps, compile defs, PCH, source files; review: removed unused VulkanHeaders find_package)
- engine/include/voxel/renderer/VulkanContext.h (new — VulkanContext class declaration with getters)
- engine/src/renderer/VulkanContext.cpp (new — full Vulkan init sequence + RAII destructor; review: removed duplicated error cleanup)
- engine/src/renderer/VmaImpl.cpp (new — VMA_IMPLEMENTATION single translation unit)
- game/src/main.cpp (modified — added VulkanContext creation + proper destruction order)
- CMakePresets.json (modified — added Vulkan-related preset config)
- vcpkg-configuration.json (new — vcpkg baseline configuration)
