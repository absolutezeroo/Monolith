#include "voxel/renderer/Renderer.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/ImGuiBackend.h"
#include "voxel/renderer/StagingBuffer.h"
#include "voxel/renderer/VulkanContext.h"

#include <stb_image_write.h>

#include <fstream>
#include <vector>

namespace voxel::renderer
{

Renderer::Renderer(VulkanContext& vulkanContext) : m_vulkanContext(vulkanContext) {}

Renderer::~Renderer()
{
    shutdown();
}

core::Result<void> Renderer::init(const std::string& shaderDir, game::Window& window)
{
    auto frameResult = createFrameResources();
    if (!frameResult.has_value())
    {
        return std::unexpected(frameResult.error());
    }

    // Create pipeline layout (shared by fill and wireframe pipelines)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkResult layoutResult =
        vkCreatePipelineLayout(m_vulkanContext.getDevice(), &layoutInfo, nullptr, &m_pipelineLayout);
    if (layoutResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create pipeline layout: {}", static_cast<int>(layoutResult));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(layoutResult), "Failed to create pipeline layout"));
    }

    std::string vertPath = shaderDir + "/triangle.vert.spv";
    std::string fragPath = shaderDir + "/triangle.frag.spv";

    // Fill pipeline (required)
    auto fillResult = buildPipeline({VK_POLYGON_MODE_FILL, true, true, vertPath, fragPath});
    if (!fillResult.has_value())
    {
        return std::unexpected(fillResult.error());
    }
    m_pipeline = fillResult.value();

    // Wireframe pipeline (optional)
    auto wireResult = buildPipeline({VK_POLYGON_MODE_LINE, true, true, vertPath, fragPath});
    if (!wireResult.has_value())
    {
        VX_LOG_WARN("Wireframe pipeline creation failed — wireframe mode disabled");
    }
    else
    {
        m_wireframePipeline = wireResult.value();
    }

    // Create depth buffer and other extent-dependent resources
    auto swapResResult = createSwapchainResources();
    if (!swapResResult.has_value())
    {
        return std::unexpected(swapResResult.error());
    }

    // Create staging buffer for CPU→GPU uploads
    auto stagingResult = StagingBuffer::create(m_vulkanContext);
    if (!stagingResult.has_value())
    {
        return std::unexpected(stagingResult.error());
    }
    m_stagingBuffer = std::move(stagingResult.value());

    // Create Gigabuffer for GPU mesh storage
    auto gigaResult = Gigabuffer::create(m_vulkanContext);
    if (!gigaResult.has_value())
    {
        return std::unexpected(gigaResult.error());
    }
    m_gigabuffer = std::move(gigaResult.value());

    // Initialize ImGui
    auto imguiResult = ImGuiBackend::create(m_vulkanContext, window.getHandle());
    if (!imguiResult.has_value())
    {
        return std::unexpected(imguiResult.error());
    }
    m_imguiBackend = std::move(imguiResult.value());

    m_isInitialized = true;
    VX_LOG_INFO("Renderer initialized — {} frames in flight", FRAMES_IN_FLIGHT);
    return {};
}

core::Result<void> Renderer::createFrameResources()
{
    VkDevice device = m_vulkanContext.getDevice();
    uint32_t graphicsFamily = m_vulkanContext.getGraphicsQueueFamily();

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
        auto& frame = m_frames[i];

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;

        VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create command pool for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create command pool"));
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to allocate command buffer for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to allocate command buffer"));
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create render fence for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create render fence"));
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        result = vkCreateSemaphore(device, &semInfo, nullptr, &frame.imageAvailableSemaphore);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create imageAvailable semaphore for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create imageAvailable semaphore"));
        }
    }

    uint32_t swapchainImageCount = static_cast<uint32_t>(m_vulkanContext.getSwapchainImages().size());
    m_renderFinishedSemaphores.resize(swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        VkResult result = vkCreateSemaphore(device, &semInfo, nullptr, &m_renderFinishedSemaphores[i]);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create renderFinished semaphore for image {}: {}", i, static_cast<int>(result));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create renderFinished semaphore"));
        }
    }

    VX_LOG_DEBUG(
        "Created per-frame resources for {} frames, {} render-finished semaphores",
        FRAMES_IN_FLIGHT,
        swapchainImageCount);
    return {};
}

void Renderer::recreateRenderFinishedSemaphores()
{
    VkDevice device = m_vulkanContext.getDevice();

    for (auto& sem : m_renderFinishedSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }

    uint32_t swapchainImageCount = static_cast<uint32_t>(m_vulkanContext.getSwapchainImages().size());
    m_renderFinishedSemaphores.assign(swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        VkResult result = vkCreateSemaphore(device, &semInfo, nullptr, &m_renderFinishedSemaphores[i]);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to recreate renderFinished semaphore for image {}: {}", i, static_cast<int>(result));
        }
    }

    VX_LOG_DEBUG("Recreated {} render-finished semaphores for new swapchain", swapchainImageCount);
}

core::Result<void> Renderer::createSwapchainResources()
{
    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    // Create depth image (D32_SFLOAT, device-local)
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImageInfo.extent = {extent.width, extent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    depthAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkResult result = vmaCreateImage(
        m_vulkanContext.getAllocator(),
        &depthImageInfo,
        &depthAllocInfo,
        &m_swapchainResources.depthImage,
        &m_swapchainResources.depthAllocation,
        nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create depth image: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create depth image"));
    }

    // Create depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_swapchainResources.depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(
        m_vulkanContext.getDevice(), &depthViewInfo, nullptr, &m_swapchainResources.depthImageView);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create depth image view: {}", static_cast<int>(result));
        vmaDestroyImage(
            m_vulkanContext.getAllocator(), m_swapchainResources.depthImage, m_swapchainResources.depthAllocation);
        m_swapchainResources = {};
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create depth image view"));
    }

    VX_LOG_DEBUG("Created depth buffer {}x{} (D32_SFLOAT)", extent.width, extent.height);
    return {};
}

void Renderer::destroySwapchainResources()
{
    VkDevice device = m_vulkanContext.getDevice();

    if (m_swapchainResources.depthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_swapchainResources.depthImageView, nullptr);
    }
    if (m_swapchainResources.depthImage != VK_NULL_HANDLE)
    {
        vmaDestroyImage(
            m_vulkanContext.getAllocator(), m_swapchainResources.depthImage, m_swapchainResources.depthAllocation);
    }
    m_swapchainResources = {};
}

core::Result<VkShaderModule> Renderer::loadShaderModule(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        VX_LOG_ERROR("Failed to open shader file: {}", path);
        return std::unexpected(core::EngineError::file(path));
    }

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0)
    {
        VX_LOG_ERROR("Invalid SPIR-V file size ({} bytes, must be non-zero multiple of 4): {}", fileSize, path);
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidFormat, "Invalid SPIR-V file: " + path});
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = fileSize;
    createInfo.pCode = buffer.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(m_vulkanContext.getDevice(), &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create shader module from {}: {}", path, static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create shader module from " + path));
    }

    return shaderModule;
}

core::Result<VkPipeline> Renderer::buildPipeline(const PipelineConfig& config)
{
    VkDevice device = m_vulkanContext.getDevice();

    auto vertResult = loadShaderModule(config.vertShaderPath);
    if (!vertResult.has_value())
    {
        return std::unexpected(vertResult.error());
    }
    VkShaderModule vertModule = vertResult.value();

    auto fragResult = loadShaderModule(config.fragShaderPath);
    if (!fragResult.has_value())
    {
        vkDestroyShaderModule(device, vertModule, nullptr);
        return std::unexpected(fragResult.error());
    }
    VkShaderModule fragModule = fragResult.value();

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynState.pDynamicStates = dynStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = config.polygonMode;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkFormat swapFmt = m_vulkanContext.getSwapchainFormat();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapFmt;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.pNext = &rendering;
    ci.stageCount = static_cast<uint32_t>(stages.size());
    ci.pStages = stages.data();
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewportState;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &msaa;
    ci.pDepthStencilState = &depthStencil;
    ci.pColorBlendState = &blendState;
    ci.pDynamicState = &dynState;
    ci.layout = m_pipelineLayout;
    ci.renderPass = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR(
            "Failed to create pipeline (polygonMode={}): {}",
            static_cast<int>(config.polygonMode),
            static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create graphics pipeline"));
    }

    VX_LOG_INFO("Pipeline created (polygonMode={})", static_cast<int>(config.polygonMode));
    return pipeline;
}

void Renderer::transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.image = image;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else
    {
        VX_LOG_ERROR(
            "Unhandled image layout transition: {} -> {}", static_cast<int>(oldLayout), static_cast<int>(newLayout));
        VX_ASSERT(false, "Unhandled image layout transition");
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

bool Renderer::beginFrame(game::Window& window, const DebugOverlayState& overlay)
{
    m_frameActive = false;

    if (!m_isInitialized)
    {
        return false;
    }

    VkDevice device = m_vulkanContext.getDevice();
    auto& frame = m_frames[m_frameIndex];

    vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

    // Handle deferred swapchain recreation (flag set by present on VK_ERROR_OUT_OF_DATE_KHR)
    if (m_needsSwapchainRecreate)
    {
        vkDeviceWaitIdle(device);
        auto recreateResult = m_vulkanContext.recreateSwapchain(window);
        if (!recreateResult.has_value())
        {
            VX_LOG_ERROR("Failed to recreate swapchain");
            m_needsSwapchainRecreate = false;
            return false;
        }
        recreateRenderFinishedSemaphores();
        destroySwapchainResources();
        auto resResult = createSwapchainResources();
        if (!resResult.has_value())
        {
            VX_LOG_ERROR("Failed to recreate depth resources after swapchain resize");
        }
        m_needsSwapchainRecreate = false;
        return false; // skip this frame
    }

    m_stagingBuffer->beginFrame(m_frameIndex);

    m_currentImageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        m_vulkanContext.getSwapchain(),
        UINT64_MAX,
        frame.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &m_currentImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_needsSwapchainRecreate = true;
        return false;
    }

    if (acquireResult == VK_SUBOPTIMAL_KHR)
    {
        VX_LOG_DEBUG("Swapchain suboptimal — continuing");
    }
    else if (acquireResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to acquire swapchain image: {}", static_cast<int>(acquireResult));
        return false;
    }

    vkResetFences(device, 1, &frame.renderFence);

    VkCommandBuffer cmd = frame.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImage swapchainImage = m_vulkanContext.getSwapchainImages()[m_currentImageIndex];
    transitionImage(cmd, swapchainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Transition depth image to depth attachment layout
    transitionImage(
        cmd,
        m_swapchainResources.depthImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_vulkanContext.getSwapchainImageViews()[m_currentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_swapchainResources.depthImageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Select fill or wireframe pipeline
    bool useWireframe = overlay.wireframeMode && m_wireframePipeline != VK_NULL_HANDLE;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, useWireframe ? m_wireframePipeline : m_pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Begin ImGui frame — caller will build UI between beginFrame/endFrame
    m_imguiBackend->beginFrame();

    m_currentWindow = &window;
    m_frameActive = true;
    return true;
}

void Renderer::endFrame()
{
    if (!m_frameActive)
    {
        return;
    }
    m_frameActive = false;

    auto& frame = m_frames[m_frameIndex];
    VkCommandBuffer cmd = frame.commandBuffer;

    // Finalize ImGui and render
    m_imguiBackend->render(cmd);

    vkCmdEndRendering(cmd);

    VkImage swapchainImage = m_vulkanContext.getSwapchainImages()[m_currentImageIndex];
    transitionImage(cmd, swapchainImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(cmd);

    // Flush staging transfers to Gigabuffer
    auto flushResult = m_stagingBuffer->flushTransfers(m_gigabuffer->getBuffer());
    if (!flushResult.has_value())
    {
        VX_LOG_ERROR("Failed to flush staging transfers");
    }

    // Submit
    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    std::array<VkSemaphoreSubmitInfo, 2> waitInfos{};
    uint32_t waitCount = 0;

    waitInfos[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfos[waitCount].semaphore = frame.imageAvailableSemaphore;
    waitInfos[waitCount].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    ++waitCount;

    if (m_stagingBuffer->hasActiveTransfer())
    {
        waitInfos[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfos[waitCount].semaphore = m_stagingBuffer->getTransferSemaphore();
        waitInfos[waitCount].stageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        ++waitCount;
    }

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_renderFinishedSemaphores[m_currentImageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = waitCount;
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    VkResult submitResult = vkQueueSubmit2(m_vulkanContext.getGraphicsQueue(), 1, &submitInfo, frame.renderFence);

    if (submitResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to submit draw command buffer: {}", static_cast<int>(submitResult));
        return;
    }

    // Present
    VkSwapchainKHR swapchain = m_vulkanContext.getSwapchain();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentImageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_vulkanContext.getGraphicsQueue(), &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_needsSwapchainRecreate = true;
    }
    else if (presentResult == VK_SUBOPTIMAL_KHR)
    {
        VX_LOG_DEBUG("Swapchain suboptimal on present — continuing");
    }
    else if (presentResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to present swapchain image: {}", static_cast<int>(presentResult));
    }

    // Capture screenshot after present (swapchain image in PRESENT_SRC layout)
    if (!m_screenshotPath.empty())
    {
        vkWaitForFences(m_vulkanContext.getDevice(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);
        captureScreenshot(swapchainImage, m_vulkanContext.getSwapchainExtent());
    }

    m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
}

void Renderer::shutdown()
{
    if (!m_isInitialized)
    {
        return;
    }

    VkDevice device = m_vulkanContext.getDevice();
    vkDeviceWaitIdle(device);

    // ImGui must be destroyed before other Vulkan resources
    m_imguiBackend.reset();

    m_stagingBuffer.reset();

    m_gigabuffer.reset();

    destroySwapchainResources();

    if (m_wireframePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_wireframePipeline, nullptr);
        m_wireframePipeline = VK_NULL_HANDLE;
    }

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for (auto& sem : m_renderFinishedSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    m_renderFinishedSemaphores.clear();

    for (auto& frame : m_frames)
    {
        if (frame.renderFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, frame.renderFence, nullptr);
            frame.renderFence = VK_NULL_HANDLE;
        }
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (frame.commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
            frame.commandPool = VK_NULL_HANDLE;
        }
    }

    m_isInitialized = false;
    VX_LOG_INFO("Renderer shut down");
}

void Renderer::requestScreenshot(const std::string& outputPath)
{
    m_screenshotPath = outputPath;
}

void Renderer::captureScreenshot(VkImage swapchainImage, VkExtent2D extent)
{
    VkDevice device = m_vulkanContext.getDevice();
    VmaAllocator allocator = m_vulkanContext.getAllocator();

    // Create a host-visible staging image to copy the swapchain image into
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCI.extent = {extent.width, extent.height, 1};
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_LINEAR;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocCI.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkImage stagingImage = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(allocator, &imageCI, &allocCI, &stagingImage, &stagingAlloc, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Screenshot: failed to create staging image: {}", static_cast<int>(result));
        return;
    }

    // Create a one-shot command buffer for the copy
    VkCommandPool tempPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolCI.queueFamilyIndex = m_vulkanContext.getGraphicsQueueFamily();
    vkCreateCommandPool(device, &poolCI, nullptr, &tempPool);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cmdAI{};
    cmdAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAI.commandPool = tempPool;
    cmdAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cmdAI, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition swapchain image: PRESENT_SRC → TRANSFER_SRC
    VkImageMemoryBarrier2 srcBarrier{};
    srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    srcBarrier.image = swapchainImage;
    srcBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    srcBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // Transition staging image: UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier2 dstBarrier{};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    dstBarrier.image = stagingImage;
    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    dstBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    std::array<VkImageMemoryBarrier2, 2> preCopyBarriers = {srcBarrier, dstBarrier};
    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.imageMemoryBarrierCount = static_cast<uint32_t>(preCopyBarriers.size());
    preDep.pImageMemoryBarriers = preCopyBarriers.data();
    vkCmdPipelineBarrier2(cmd, &preDep);

    // Blit swapchain image (B8G8R8A8_SRGB) → staging image (R8G8B8A8_UNORM)
    // Blit handles format conversion and also the B/R channel swap
    VkImageBlit2 blitRegion{};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {
        static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {
        static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.srcImage = swapchainImage;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = stagingImage;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;
    blitInfo.filter = VK_FILTER_NEAREST;
    vkCmdBlitImage2(cmd, &blitInfo);

    // Transition staging image: TRANSFER_DST → GENERAL (for host read)
    VkImageMemoryBarrier2 readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    readBarrier.image = stagingImage;
    readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    readBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    readBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    readBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.imageMemoryBarrierCount = 1;
    postDep.pImageMemoryBarriers = &readBarrier;
    vkCmdPipelineBarrier2(cmd, &postDep);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fenceCI, nullptr, &fence);

    vkQueueSubmit(m_vulkanContext.getGraphicsQueue(), 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Map and read pixels
    void* mapped = nullptr;
    vmaMapMemory(allocator, stagingAlloc, &mapped);

    // Get the actual row pitch of the staging image (may have padding)
    VkImageSubresource subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(device, stagingImage, &subResource, &layout);

    // Copy pixel data row-by-row (respecting rowPitch which may differ from width*4)
    auto w = static_cast<int>(extent.width);
    auto h = static_cast<int>(extent.height);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    auto* srcBytes = static_cast<uint8_t*>(mapped) + layout.offset;
    for (int row = 0; row < h; ++row)
    {
        std::memcpy(
            pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(w) * 4,
            srcBytes + static_cast<size_t>(row) * layout.rowPitch,
            static_cast<size_t>(w) * 4);
    }

    vmaUnmapMemory(allocator, stagingAlloc);

    // Write PNG
    int writeResult = stbi_write_png(m_screenshotPath.c_str(), w, h, 4, pixels.data(), w * 4);
    if (writeResult != 0)
    {
        VX_LOG_INFO("Screenshot saved: {}", m_screenshotPath);
    }
    else
    {
        VX_LOG_ERROR("Screenshot: failed to write PNG to {}", m_screenshotPath);
    }

    // Cleanup
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyImage(allocator, stagingImage, stagingAlloc);
    m_screenshotPath.clear();
}

} // namespace voxel::renderer
