#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/RendererConstants.h"

#include <vk_mem_alloc.h>
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
     * @brief Begins a frame: acquires swapchain image, starts command recording, draws scene.
     *
     * After this returns true, the caller may issue ImGui calls (e.g. overlay building).
     * Must be followed by endFrame().
     * @return true if frame is active and caller should build ImGui. false if frame was skipped.
     */
    bool beginFrame(game::Window& window, const DebugOverlayState& overlay);

    /**
     * @brief Ends the frame: renders ImGui, submits commands, presents.
     *
     * Must only be called after beginFrame() returned true.
     */
    void endFrame();

    /// Waits for GPU idle and destroys all owned resources.
    void shutdown();

    /// Returns the Gigabuffer (non-owning, for stats queries). May be null before init.
    [[nodiscard]] const Gigabuffer* getGigabuffer() const { return m_gigabuffer.get(); }

  private:
    /// Extent-dependent resources that must be recreated on swapchain resize.
    struct SwapchainResources
    {
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
    };

    /// Configuration for graphics pipeline creation.
    struct PipelineConfig
    {
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        std::string vertShaderPath;
        std::string fragShaderPath;
    };

    core::Result<void> createFrameResources();
    core::Result<VkPipeline> buildPipeline(const PipelineConfig& config);
    core::Result<VkShaderModule> loadShaderModule(const std::string& path);

    void recreateRenderFinishedSemaphores();

    core::Result<void> createSwapchainResources();
    void destroySwapchainResources();

    void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    VulkanContext& m_vulkanContext;

    std::array<FrameData, FRAMES_IN_FLIGHT> m_frames{};
    std::vector<VkSemaphore> m_renderFinishedSemaphores; // one per swapchain image
    uint32_t m_frameIndex = 0;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_wireframePipeline = VK_NULL_HANDLE;

    std::unique_ptr<StagingBuffer> m_stagingBuffer;
    std::unique_ptr<Gigabuffer> m_gigabuffer;
    std::unique_ptr<ImGuiBackend> m_imguiBackend;

    SwapchainResources m_swapchainResources{};

    bool m_isInitialized = false;
    bool m_needsSwapchainRecreate = false;

    // Per-frame state (set by beginFrame, used by endFrame)
    game::Window* m_currentWindow = nullptr;
    uint32_t m_currentImageIndex = 0;
    bool m_frameActive = false;
};

} // namespace voxel::renderer
