#include "voxel/renderer/Renderer.h"
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/ImGuiBackend.h"
#include "voxel/renderer/StagingBuffer.h"
#include "voxel/renderer/VulkanContext.h"
#include "voxel/game/Window.h"

#include <fstream>
#include <vector>

namespace voxel::renderer
{

Renderer::Renderer(VulkanContext& vulkanContext)
    : m_vulkanContext(vulkanContext)
{
}

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
    VkResult layoutResult = vkCreatePipelineLayout(m_vulkanContext.getDevice(), &layoutInfo, nullptr, &m_pipelineLayout);
    if (layoutResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create pipeline layout: {}", static_cast<int>(layoutResult));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(layoutResult), "Failed to create pipeline layout"));
    }

    std::string vertPath = shaderDir + "/triangle.vert.spv";
    std::string fragPath = shaderDir + "/triangle.frag.spv";

    // Fill pipeline (required)
    auto fillResult = buildPipeline({VK_POLYGON_MODE_FILL, vertPath, fragPath});
    if (!fillResult.has_value())
    {
        return std::unexpected(fillResult.error());
    }
    m_pipeline = fillResult.value();

    // Wireframe pipeline (optional)
    auto wireResult = buildPipeline({VK_POLYGON_MODE_LINE, vertPath, fragPath});
    if (!wireResult.has_value())
    {
        VX_LOG_WARN("Wireframe pipeline creation failed — wireframe mode disabled");
    }
    else
    {
        m_wireframePipeline = wireResult.value();
    }

    // Create staging buffer for CPU→GPU uploads
    auto stagingResult = StagingBuffer::create(m_vulkanContext);
    if (!stagingResult.has_value())
    {
        return std::unexpected(stagingResult.error());
    }
    m_stagingBuffer = std::move(stagingResult.value());

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
            return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create command pool"));
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
            return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to allocate command buffer"));
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create render fence for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create render fence"));
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        result = vkCreateSemaphore(device, &semInfo, nullptr, &frame.imageAvailableSemaphore);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create imageAvailable semaphore for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create imageAvailable semaphore"));
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
            return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create renderFinished semaphore"));
        }
    }

    VX_LOG_DEBUG("Created per-frame resources for {} frames, {} render-finished semaphores",
        FRAMES_IN_FLIGHT, swapchainImageCount);
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
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create shader module from " + path));
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
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    VkFormat swapFmt = m_vulkanContext.getSwapchainFormat();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapFmt;

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
        VX_LOG_ERROR("Failed to create pipeline (polygonMode={}): {}", static_cast<int>(config.polygonMode), static_cast<int>(result));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create graphics pipeline"));
    }

    VX_LOG_INFO("Pipeline created (polygonMode={})", static_cast<int>(config.polygonMode));
    return pipeline;
}

void Renderer::transitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.image = image;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
    }
    else
    {
        VX_LOG_ERROR("Unhandled image layout transition: {} -> {}",
            static_cast<int>(oldLayout), static_cast<int>(newLayout));
        VX_ASSERT(false, "Unhandled image layout transition");
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

bool Renderer::beginFrame(game::Window& window, DebugOverlayState& overlay)
{
    m_frameActive = false;

    if (!m_isInitialized)
    {
        return false;
    }

    // Handle deferred swapchain recreation
    if (m_framebufferResized || window.wasResized())
    {
        m_framebufferResized = false;
        auto recreateResult = m_vulkanContext.recreateSwapchain(window);
        if (!recreateResult.has_value())
        {
            VX_LOG_ERROR("Failed to recreate swapchain");
        }
        else
        {
            recreateRenderFinishedSemaphores();
        }
        return false;
    }

    VkDevice device = m_vulkanContext.getDevice();
    auto& frame = m_frames[m_frameIndex];

    vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

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
        auto recreateResult = m_vulkanContext.recreateSwapchain(window);
        if (!recreateResult.has_value())
        {
            VX_LOG_ERROR("Failed to recreate swapchain after out-of-date acquire");
        }
        else
        {
            recreateRenderFinishedSemaphores();
        }
        return false;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
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

    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_vulkanContext.getSwapchainImageViews()[m_currentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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

    // Flush staging transfers
    auto flushResult = m_stagingBuffer->flushTransfers(VK_NULL_HANDLE);
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

    VkResult submitResult = vkQueueSubmit2(
        m_vulkanContext.getGraphicsQueue(), 1, &submitInfo, frame.renderFence);

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

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        m_framebufferResized = true;
    }
    else if (presentResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to present swapchain image: {}", static_cast<int>(presentResult));
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

} // namespace voxel::renderer
