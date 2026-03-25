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
     * @return Success or EngineError on failure.
     */
    core::Result<void> init(const std::string& shaderDir);

    /**
     * @brief Renders one frame: acquire image, record commands, submit, present.
     * @param window Reference to the window for resize handling.
     */
    void draw(game::Window& window);

    /// Waits for GPU idle and destroys all owned resources.
    void shutdown();

private:
    core::Result<void> createFrameResources();
    core::Result<void> createPipeline(const std::string& shaderDir);
    core::Result<VkShaderModule> loadShaderModule(const std::string& path);

    void recreateRenderFinishedSemaphores();

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

    std::unique_ptr<StagingBuffer> m_stagingBuffer;

    bool m_isInitialized = false;
    bool m_framebufferResized = false;
};

} // namespace voxel::renderer
