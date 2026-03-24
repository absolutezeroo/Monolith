# Epic 2 — Vulkan Bootstrap

**Priority**: P0
**Dependencies**: Epic 1
**Goal**: Vulkan 1.3 initialized with the full stack (volk + vk-bootstrap + VMA), rendering a test triangle, gigabuffer allocated, staging pipeline working, FPS camera operational.

---

## Story 2.1: GLFW Window + Game Loop Skeleton

**As a** developer,
**I want** a resizable window with a fixed-timestep game loop,
**so that** I have a stable frame for all subsequent rendering and simulation.

**Acceptance Criteria:**
- GLFW window creation (1280×720 default, resizable, titled "VoxelForge")
- Fixed-timestep game loop: 20 ticks/sec simulation, uncapped render with interpolation alpha
- `GameLoop` class with `run()`, `tick(double dt)`, `render(double alpha)` split
- Graceful handling of window minimize (pause rendering), resize (recreate swapchain later), close
- Frame time measurement, FPS counter logged every second
- Clean shutdown: destroy window, terminate GLFW

**Technical Notes:**
- Use `glfwWindowShouldClose` loop; tick accumulator pattern from architecture §10
- This story does NOT init Vulkan — just the window and loop skeleton

---

## Story 2.2: Vulkan Initialization (volk + vk-bootstrap + VMA)

**As a** developer,
**I want** Vulkan 1.3 fully initialized with device, queues, and memory allocator,
**so that** I can create GPU resources.

**Acceptance Criteria:**
- `volkInitialize()` called before anything else
- vk-bootstrap creates Instance with validation layers (debug only), requiring API 1.3
- vk-bootstrap selects physical device requiring: `dynamicRendering`, `synchronization2`, `bufferDeviceAddress`, `descriptorIndexing`
- Logical device created with graphics queue + transfer queue (separate if available, fallback to shared)
- `volkLoadDevice(device)` called after device creation
- VMA allocator created with BDA flag + volk function import (`vmaImportVulkanFunctionsFromVolk`)
- Swapchain created via vk-bootstrap (FIFO present mode, SRGB format preferred)
- `VulkanContext` class owns all of the above, RAII cleanup in destructor
- Log: GPU name, Vulkan version, queue families, memory heaps

---

## Story 2.3: Test Triangle with Dynamic Rendering

**As a** developer,
**I want** a colored triangle rendering to the screen,
**so that** I validate the entire Vulkan pipeline end-to-end.

**Acceptance Criteria:**
- Vertex + fragment shader (hardcoded triangle positions + colors in shader)
- Graphics pipeline created with dynamic rendering (no VkRenderPass, no VkFramebuffer)
- Frame rendering: acquire swapchain image → begin dynamic rendering → bind pipeline → draw(3) → end rendering → present
- Synchronization via fences + semaphores (image available, render finished)
- Window clear color visible behind triangle
- No validation layer errors

**Technical Notes:**
- Use `VK_KHR_dynamic_rendering` (core in 1.3)
- Shaders compiled to SPIR-V via glslangValidator (add to `tools/shader_compile.sh`)

---

## Story 2.4: Gigabuffer Allocation + VmaVirtualBlock

**As a** developer,
**I want** a single large GPU buffer with CPU-side sub-allocation tracking,
**so that** all chunk meshes can live in one buffer for indirect rendering.

**Acceptance Criteria:**
- `Gigabuffer` class: creates VkBuffer (256 MB default, configurable) with `STORAGE_BUFFER | TRANSFER_DST | INDIRECT_BUFFER` usage
- VmaVirtualBlock created matching gigabuffer size
- API: `allocate(size, alignment) → Result<GigabufferAllocation>` (returns offset + handle)
- API: `free(GigabufferAllocation)` — returns space to virtual block
- API: `usedBytes()`, `freeBytes()`, `allocationCount()`
- Unit tests (CPU-side only): alloc/free/reuse, fragmentation behavior, out-of-memory returns error

---

## Story 2.5: Staging Buffer + Transfer Queue Upload

**As a** developer,
**I want** a staging pipeline to upload CPU data into the gigabuffer,
**so that** chunk meshes can be transferred to DEVICE_LOCAL memory efficiently.

**Acceptance Criteria:**
- `StagingBuffer` class: HOST_VISIBLE | HOST_COHERENT, persistently mapped
- `uploadToGigabuffer(const void* data, size_t size, VkDeviceSize dstOffset)` — records copy command
- Uses transfer queue if available, graphics queue as fallback
- Semaphore synchronization between transfer and graphics queues
- Batched: multiple uploads per frame, submitted as a single command buffer
- Rate limited: configurable max uploads per frame (default 8)
- Staging buffer recycles (ring buffer or double-buffer pattern)

---

## Story 2.6: FPS Camera + Dear ImGui Overlay

**As a** developer,
**I want** a fly-mode camera and debug UI overlay,
**so that** I can navigate the world and inspect engine state.

**Acceptance Criteria:**
- `Camera` class: position (dvec3), yaw/pitch, view matrix, projection matrix (perspective, configurable FOV)
- Mouse look: raw mouse delta → yaw/pitch rotation, pitch clamped ±89°
- WASD+Space+Shift fly movement (no collision yet), speed configurable
- `extractFrustumPlanes()` returns 6 planes for culling (used in future epics)
- Dear ImGui initialized with Vulkan backend
- F3 toggles overlay: FPS, camera position (x,y,z), yaw/pitch, memory usage (gigabuffer used/free)
- Mouse capture toggle (Escape to release cursor, click to recapture)
- Wireframe rendering toggle (F4) — switches polygon mode to LINE for all chunk draws
- Chunk boundary visualization toggle (F5) — renders translucent wireframe cubes at chunk borders
- Chunk state color coding in ImGui: loaded (green), meshing (yellow), dirty (orange), unloaded (gray)
