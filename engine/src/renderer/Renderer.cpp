#include "voxel/renderer/Renderer.h"
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
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

core::Result<void> Renderer::init(const std::string& shaderDir)
{
    auto frameResult = createFrameResources();
    if (!frameResult.has_value())
    {
        return std::unexpected(frameResult.error());
    }

    auto pipelineResult = createPipeline(shaderDir);
    if (!pipelineResult.has_value())
    {
        return std::unexpected(pipelineResult.error());
    }

    // Create staging buffer for CPU→GPU uploads
    auto stagingResult = StagingBuffer::create(m_vulkanContext);
    if (!stagingResult.has_value())
    {
        return std::unexpected(stagingResult.error());
    }
    m_stagingBuffer = std::move(stagingResult.value());

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

        // Command pool — one per frame, resettable command buffers
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;

        VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create command pool for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::VulkanError);
        }

        // Command buffer — one per frame
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to allocate command buffer for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::VulkanError);
        }

        // Fence — must start signaled so first vkWaitForFences does not deadlock
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create render fence for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::VulkanError);
        }

        // Semaphore — imageAvailable (per-frame)
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        result = vkCreateSemaphore(device, &semInfo, nullptr, &frame.imageAvailableSemaphore);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create imageAvailable semaphore for frame {}: {}", i, static_cast<int>(result));
            return std::unexpected(core::EngineError::VulkanError);
        }
    }

    // renderFinished semaphores — one per swapchain image to avoid presentation reuse conflicts
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
            return std::unexpected(core::EngineError::VulkanError);
        }
    }

    VX_LOG_DEBUG("Created per-frame resources for {} frames, {} render-finished semaphores",
        FRAMES_IN_FLIGHT, swapchainImageCount);
    return {};
}

void Renderer::recreateRenderFinishedSemaphores()
{
    VkDevice device = m_vulkanContext.getDevice();

    // Destroy old semaphores
    for (auto& sem : m_renderFinishedSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }

    // Resize to match new swapchain image count
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
        return std::unexpected(core::EngineError::FileNotFound);
    }

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0)
    {
        VX_LOG_ERROR("Invalid SPIR-V file size ({} bytes, must be non-zero multiple of 4): {}", fileSize, path);
        return std::unexpected(core::EngineError::InvalidFormat);
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
        return std::unexpected(core::EngineError::VulkanError);
    }

    return shaderModule;
}

core::Result<void> Renderer::createPipeline(const std::string& shaderDir)
{
    VkDevice device = m_vulkanContext.getDevice();

    // Load shader modules
    std::string vertPath = shaderDir + "/triangle.vert.spv";
    std::string fragPath = shaderDir + "/triangle.frag.spv";

    auto vertResult = loadShaderModule(vertPath);
    if (!vertResult.has_value())
    {
        return std::unexpected(vertResult.error());
    }
    VkShaderModule vertModule = vertResult.value();

    auto fragResult = loadShaderModule(fragPath);
    if (!fragResult.has_value())
    {
        vkDestroyShaderModule(device, vertModule, nullptr);
        return std::unexpected(fragResult.error());
    }
    VkShaderModule fragModule = fragResult.value();

    // Shader stages
    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStageInfo, fragStageInfo};

    // Vertex input — empty (positions hardcoded in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

    // Dynamic state — viewport and scissor set at draw time
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // Viewport state — count only, actual values set dynamically
    VkPipelineViewportStateCreateInfo viewportStateInfo{};
    viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateInfo.viewportCount = 1;
    viewportStateInfo.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
    rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationInfo.depthClampEnable = VK_FALSE;
    rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizationInfo.depthBiasEnable = VK_FALSE;
    rasterizationInfo.lineWidth = 1.0f;

    // Multisampling — no MSAA
    VkPipelineMultisampleStateCreateInfo multisampleInfo{};
    multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleInfo.sampleShadingEnable = VK_FALSE;

    // Color blend — no blending, write RGBA
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
    colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendInfo.logicOpEnable = VK_FALSE;
    colorBlendInfo.attachmentCount = 1;
    colorBlendInfo.pAttachments = &colorBlendAttachment;

    // Pipeline layout — empty (no push constants, no descriptor sets)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create pipeline layout: {}", static_cast<int>(result));
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return std::unexpected(core::EngineError::VulkanError);
    }

    // Dynamic rendering — chain VkPipelineRenderingCreateInfo in pNext
    VkFormat swapchainFormat = m_vulkanContext.getSwapchainFormat();

    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingCreateInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportStateInfo;
    pipelineInfo.pRasterizationState = &rasterizationInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlendInfo;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

    // Destroy shader modules immediately — pipeline has its own copy
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create graphics pipeline: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::VulkanError);
    }

    VX_LOG_INFO("Graphics pipeline created (dynamic rendering, no VkRenderPass)");
    return {};
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

void Renderer::draw(game::Window& window)
{
    if (!m_isInitialized)
    {
        return;
    }

    // Handle deferred swapchain recreation (from VK_SUBOPTIMAL_KHR)
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
        return;
    }

    VkDevice device = m_vulkanContext.getDevice();
    auto& frame = m_frames[m_frameIndex];

    // CPU-GPU sync: wait for this frame's previous work to complete
    vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

    // Advance staging buffer to this frame's ring-buffer region
    m_stagingBuffer->beginFrame(m_frameIndex);

    // Acquire swapchain image
    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        m_vulkanContext.getSwapchain(),
        UINT64_MAX,
        frame.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex);

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
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        VX_LOG_ERROR("Failed to acquire swapchain image: {}", static_cast<int>(acquireResult));
        return;
    }

    // Reset fence only after successful acquire — avoids deadlock if acquire triggers swapchain recreation
    vkResetFences(device, 1, &frame.renderFence);

    // Reset and begin command buffer
    VkCommandBuffer cmd = frame.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
    VkImage swapchainImage = m_vulkanContext.getSwapchainImages()[imageIndex];
    transitionImage(cmd, swapchainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Begin dynamic rendering
    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_vulkanContext.getSwapchainImageViews()[imageIndex];
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

    // Set dynamic viewport and scissor
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

    // Bind pipeline and draw the triangle
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    // Transition: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    transitionImage(cmd, swapchainImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(cmd);

    // Flush any pending staging buffer transfers before graphics submission
    // (no-op if no uploads were enqueued this frame)
    auto flushResult = m_stagingBuffer->flushTransfers(VK_NULL_HANDLE);
    if (!flushResult.has_value())
    {
        VX_LOG_ERROR("Failed to flush staging transfers");
    }

    // Submit via vkQueueSubmit2 (sync2)
    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    // Build wait semaphore list — always wait on imageAvailable,
    // optionally wait on transfer semaphore if transfers were submitted
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
    signalInfo.semaphore = m_renderFinishedSemaphores[imageIndex];
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
        m_vulkanContext.getGraphicsQueue(),
        1,
        &submitInfo,
        frame.renderFence);

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
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_vulkanContext.getGraphicsQueue(), &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        m_framebufferResized = true;
    }
    else if (presentResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to present swapchain image: {}", static_cast<int>(presentResult));
    }

    // Advance frame index
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

    // Destroy staging buffer first (before VulkanContext resources)
    m_stagingBuffer.reset();

    // Destroy in reverse creation order
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

    // Destroy per-swapchain-image renderFinished semaphores
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
