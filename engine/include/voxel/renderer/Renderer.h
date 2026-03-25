#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace voxel::game
{
class Window;
}

namespace voxel::renderer
{

class Camera;
class ImGuiBackend;
class StagingBuffer;
class VulkanContext;

/**
 * @brief Per-frame synchronization and command recording resources.
 */
struct FrameData
{
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkFence renderFence = VK_NULL_HANDLE;
};

/// Passed from GameApp to Renderer each frame for ImGui overlay display.
struct DebugOverlayState
{
    bool showOverlay = false;
    bool wireframeMode = false;
    bool showChunkBorders = false;
    float fov = 70.0f;
    float sensitivity = 0.1f;
};

/**
 * @brief Manages the Vulkan rendering pipeline for a test triangle.
 *
 * Uses Vulkan 1.3 dynamic rendering (no VkRenderPass/VkFramebuffer),
 * synchronization2 for barriers and submission, and double-buffered frames.
 * Does NOT own the VulkanContext — caller manages lifetime.
 */
class Renderer
{
public:
    explicit Renderer(VulkanContext& vulkanContext);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * @brief Initializes per-frame resources, loads shaders, and creates the graphics pipeline.
     * @param shaderDir Path to the directory containing compiled .spv shader files.
     * @param window GLFW window for ImGui initialization.
     * @return Success or EngineError on failure.
     */
    core::Result<void> init(const std::string& shaderDir, game::Window& window);

    /**
     * @brief Renders one frame: acquire image, record commands, submit, present.
     * @param window Reference to the window for resize handling.
     * @param camera Camera for overlay display.
     * @param overlay Debug overlay state (F3/F4/F5 toggles).
     */
    void draw(game::Window& window, const Camera& camera, DebugOverlayState& overlay);

    /// Waits for GPU idle and destroys all owned resources.
    void shutdown();

private:
    core::Result<void> createFrameResources();
    core::Result<void> createPipeline(const std::string& shaderDir);
    core::Result<void> createWireframePipeline(const std::string& shaderDir);
    core::Result<VkShaderModule> loadShaderModule(const std::string& path);

    void recreateRenderFinishedSemaphores();

    void buildDebugOverlay(const Camera& camera, DebugOverlayState& overlay);

    void transitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout);

    VulkanContext& m_vulkanContext;

    std::array<FrameData, FRAMES_IN_FLIGHT> m_frames{};
    std::vector<VkSemaphore> m_renderFinishedSemaphores; // one per swapchain image
    uint32_t m_frameIndex = 0;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_wireframePipeline = VK_NULL_HANDLE;

    std::unique_ptr<StagingBuffer> m_stagingBuffer;
    std::unique_ptr<ImGuiBackend> m_imguiBackend;

    bool m_isInitialized = false;
    bool m_framebufferResized = false;

    // FPS tracking (initialized to -1 to detect first frame)
    double m_lastFrameTime = -1.0;
    int m_fpsCount = 0;
    double m_fpsTimer = 0.0;
    int m_displayFps = 0;
};

} // namespace voxel::renderer
