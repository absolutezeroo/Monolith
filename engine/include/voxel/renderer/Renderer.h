#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/Gigabuffer.h"
#include "voxel/renderer/RendererConstants.h"
#include "voxel/renderer/TintPalette.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <glm/glm.hpp>

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
class ChunkRenderInfoBuffer;
class DescriptorAllocator;
class GBuffer;
class ImGuiBackend;
class IndirectDrawBuffer;
class QuadIndexBuffer;
class StagingBuffer;
class TextureArray;
class VulkanContext;

/// Push constants for chunk rendering: VP matrix, animation time, and forward-lit lighting params.
/// chunkWorldPos is now read from the ChunkRenderInfo SSBO via gl_InstanceIndex.
struct ChunkPushConstants
{
    glm::mat4 viewProjection; // 64 bytes, offset 0
    float time;               // 4 bytes,  offset 64
    float ambientStrength;    // 4 bytes,  offset 68
    float pad[2];             // 8 bytes,  offset 72
    glm::vec4 sunDirection;   // 16 bytes, offset 80 (w unused)
};
static_assert(sizeof(ChunkPushConstants) == 96);

/// Push constants for the deferred lighting pass.
struct LightingPushConstants
{
    glm::vec3 sunDirection;   // 12 bytes
    float ambientStrength;    // 4 bytes
};
static_assert(sizeof(LightingPushConstants) == 16);

/// Push constants for the compute culling shader (cull.comp).
struct CullPushConstants
{
    glm::vec4 frustumPlanes[6]; // 96 bytes
    uint32_t totalSections;     // 4 bytes
    uint32_t pad[3];            // 12 bytes
};
static_assert(sizeof(CullPushConstants) == 112);

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
 * @brief Manages the Vulkan rendering pipeline for chunk rendering.
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
    core::Result<void> init(const std::string& shaderDir, const std::string& assetsDir, game::Window& window);

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

    /**
     * @brief GPU-driven chunk rendering via compute frustum culling and indirect draw.
     *
     * Resets the draw count, dispatches the compute culling shader, issues pipeline
     * barriers, then calls vkCmdDrawIndexedIndirectCount. Must be called between
     * beginFrame/endFrame.
     *
     * @param viewProjection Combined view-projection matrix from the camera.
     * @param frustumPlanes The 6 frustum planes for culling (Gribb-Hartmann extraction).
     */
    void renderChunksIndirect(const glm::mat4& viewProjection, const std::array<glm::vec4, 6>& frustumPlanes);

    /// Returns the number of draw calls issued in the last renderChunks() call.
    [[nodiscard]] uint32_t getLastDrawCount() const { return m_lastDrawCount; }

    /// Returns the total number of quads rendered in the last renderChunks() call.
    [[nodiscard]] uint32_t getLastQuadCount() const { return m_lastQuadCount; }

    /// Returns the Gigabuffer (non-owning, for stats queries). May be null before init.
    [[nodiscard]] const Gigabuffer* getGigabuffer() const { return m_gigabuffer.get(); }

    /// Returns mutable Gigabuffer for upload operations. Renderer retains ownership.
    [[nodiscard]] Gigabuffer* getMutableGigabuffer() { return m_gigabuffer.get(); }

    /// Returns mutable StagingBuffer for upload operations. Renderer retains ownership.
    [[nodiscard]] StagingBuffer* getMutableStagingBuffer() { return m_stagingBuffer.get(); }

    /// Requests a screenshot to be captured after the next present. Saved as PNG to the given path.
    void requestScreenshot(const std::string& outputPath);

    /// Returns mutable IndirectDrawBuffer. Renderer retains ownership.
    [[nodiscard]] IndirectDrawBuffer* getIndirectDrawBuffer() { return m_indirectDrawBuffer.get(); }

    /// Returns mutable ChunkRenderInfoBuffer for slot management. Renderer retains ownership.
    [[nodiscard]] ChunkRenderInfoBuffer* getMutableChunkRenderInfoBuffer() { return m_chunkRenderInfoBuffer.get(); }

    [[nodiscard]] VkDescriptorSetLayout getChunkDescriptorSetLayout() const { return m_chunkDescriptorSetLayout; }
    [[nodiscard]] VkDescriptorSet getChunkDescriptorSet() const { return m_chunkDescriptorSet; }
    [[nodiscard]] DescriptorAllocator& getDescriptorAllocator() { return *m_descriptorAllocator; }
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    [[nodiscard]] const QuadIndexBuffer* getQuadIndexBuffer() const { return m_quadIndexBuffer.get(); }
    [[nodiscard]] const TextureArray* getTextureArray() const { return m_textureArray.get(); }

    /// Upload a TintPalette to the GPU. Converts vec3 → vec4 (w=1) for std430 alignment.
    void updateTintPalette(const TintPalette& palette);

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
        bool enableBlending = false;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        std::string vertShaderPath;
        std::string fragShaderPath;
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

    core::Result<void> createFrameResources();
    core::Result<VkPipeline> buildPipeline(const PipelineConfig& config);
    core::Result<VkShaderModule> loadShaderModule(const std::string& path);
    void beginRenderPass();

    void recreateRenderFinishedSemaphores();

    core::Result<void> createSwapchainResources();
    void destroySwapchainResources();

    void transitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
    void writeLightingDescriptors();
    core::Result<void> createLightingPipeline(const std::string& shaderDir);

    VulkanContext& m_vulkanContext;

    std::array<FrameData, FRAMES_IN_FLIGHT> m_frames{};
    std::vector<VkSemaphore> m_renderFinishedSemaphores; // one per swapchain image
    uint32_t m_frameIndex = 0;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_wireframePipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_cullPipeline = VK_NULL_HANDLE;

    VkPipeline m_translucentPipeline = VK_NULL_HANDLE;
    VkPipeline m_cullTranslucentPipeline = VK_NULL_HANDLE;
    std::unique_ptr<IndirectDrawBuffer> m_transIndirectDrawBuffer;

    VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_lightingDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_lightingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_lightingPipeline = VK_NULL_HANDLE;

    std::unique_ptr<DescriptorAllocator> m_descriptorAllocator;
    VkDescriptorSetLayout m_chunkDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_chunkDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_transDescriptorSet = VK_NULL_HANDLE;

    std::unique_ptr<StagingBuffer> m_stagingBuffer;
    std::unique_ptr<Gigabuffer> m_gigabuffer;
    std::unique_ptr<QuadIndexBuffer> m_quadIndexBuffer;
    std::unique_ptr<IndirectDrawBuffer> m_indirectDrawBuffer;
    std::unique_ptr<ChunkRenderInfoBuffer> m_chunkRenderInfoBuffer;
    std::unique_ptr<TextureArray> m_textureArray;
    std::unique_ptr<GBuffer> m_gbuffer;
    std::unique_ptr<ImGuiBackend> m_imguiBackend;

    VkBuffer m_tintPaletteBuffer = VK_NULL_HANDLE;
    VmaAllocation m_tintPaletteAllocation = VK_NULL_HANDLE;
    glm::vec4* m_tintPaletteMapped = nullptr;

    SwapchainResources m_swapchainResources{};

    void captureScreenshot(VkImage swapchainImage, VkExtent2D extent);

    bool m_isInitialized = false;
    bool m_needsSwapchainRecreate = false;
    bool m_renderPassActive = false;
    std::string m_screenshotPath;

    // Render stats from last renderChunks() call
    uint32_t m_lastDrawCount = 0;
    uint32_t m_lastQuadCount = 0;

    // Cached VP matrix from renderChunksIndirect() for translucent pass
    glm::mat4 m_lastViewProjection{1.0f};

    // Per-frame state (set by beginFrame, used by endFrame)
    game::Window* m_currentWindow = nullptr;
    uint32_t m_currentImageIndex = 0;
    bool m_frameActive = false;
    bool m_useWireframe = false;
};

} // namespace voxel::renderer
