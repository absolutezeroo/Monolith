#include "voxel/renderer/Renderer.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/ChunkRenderInfoBuffer.h"
#include "voxel/renderer/DescriptorAllocator.h"
#include "voxel/renderer/GBuffer.h"
#include "voxel/renderer/ImGuiBackend.h"
#include "voxel/renderer/IndirectDrawBuffer.h"
#include "voxel/renderer/QuadIndexBuffer.h"
#include "voxel/renderer/StagingBuffer.h"
#include "voxel/renderer/TextureArray.h"
#include "voxel/renderer/VulkanContext.h"

#include <GLFW/glfw3.h>

#include <fstream>
#include <stb_image_write.h>
#include <vector>

namespace voxel::renderer
{

Renderer::Renderer(VulkanContext& vulkanContext) : m_vulkanContext(vulkanContext) {}

Renderer::~Renderer()
{
    shutdown();
}

core::Result<void> Renderer::init(const std::string& shaderDir, const std::string& assetsDir, game::Window& window)
{
    auto frameResult = createFrameResources();
    if (!frameResult.has_value())
    {
        return std::unexpected(frameResult.error());
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

    // Create shared quad index buffer for indexed drawing
    auto indexResult = QuadIndexBuffer::create(m_vulkanContext);
    if (!indexResult.has_value())
    {
        return std::unexpected(indexResult.error());
    }
    m_quadIndexBuffer = std::move(indexResult.value());

    // Create indirect draw buffer for GPU-driven rendering (Story 6.3)
    auto indirectResult = IndirectDrawBuffer::create(m_vulkanContext, MAX_RENDERABLE_SECTIONS);
    if (!indirectResult.has_value())
    {
        return std::unexpected(indirectResult.error());
    }
    m_indirectDrawBuffer = std::move(indirectResult.value());

    // Create ChunkRenderInfo SSBO for per-chunk metadata (Story 6.3)
    auto chunkInfoResult = ChunkRenderInfoBuffer::create(m_vulkanContext, MAX_RENDERABLE_SECTIONS);
    if (!chunkInfoResult.has_value())
    {
        return std::unexpected(chunkInfoResult.error());
    }
    m_chunkRenderInfoBuffer = std::move(chunkInfoResult.value());

    // Create texture array for block textures
    std::string textureDir = assetsDir + "/textures/blocks";
    auto textureResult = TextureArray::create(m_vulkanContext, textureDir);
    if (!textureResult.has_value())
    {
        return std::unexpected(textureResult.error());
    }
    m_textureArray = std::move(textureResult.value());

    VkDevice device = m_vulkanContext.getDevice();
    VmaAllocator allocator = m_vulkanContext.getAllocator();

    // Tint palette SSBO: 8 x vec4 = 128 bytes, HOST_VISIBLE + persistently mapped
    {
        VkBufferCreateInfo tintBufInfo{};
        tintBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        tintBufInfo.size = TintPalette::MAX_ENTRIES * sizeof(glm::vec4);
        tintBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo tintAllocInfo{};
        tintAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        tintAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo tintMappedInfo{};
        VkResult tintResult =
            vmaCreateBuffer(allocator, &tintBufInfo, &tintAllocInfo, &m_tintPaletteBuffer, &m_tintPaletteAllocation, &tintMappedInfo);
        if (tintResult != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create tint palette buffer: {}", static_cast<int>(tintResult));
            return std::unexpected(
                core::EngineError::vulkan(static_cast<int32_t>(tintResult), "Failed to create tint palette buffer"));
        }
        m_tintPaletteMapped = static_cast<glm::vec4*>(tintMappedInfo.pMappedData);
        VX_LOG_INFO("Tint palette buffer created ({} bytes, persistently mapped)", TintPalette::MAX_ENTRIES * sizeof(glm::vec4));
    }

    // Create descriptor allocator
    m_descriptorAllocator = std::make_unique<DescriptorAllocator>(device);

    // Build chunk descriptor set layout:
    //   binding 0 = SSBO (gigabuffer, vertex stage)
    //   binding 1 = SSBO (ChunkRenderInfo, vertex + compute stage)
    //   binding 2 = SSBO (indirect command buffer, compute stage)
    //   binding 3 = SSBO (indirect draw count buffer, compute stage)
    //   binding 4 = COMBINED_IMAGE_SAMPLER (block texture array, fragment stage)
    //   binding 5 = SSBO (tint palette, fragment stage)
    DescriptorLayoutBuilder layoutBuilder;
    auto chunkLayoutResult =
        layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .addBinding(
                1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build(device);
    if (!chunkLayoutResult.has_value())
    {
        return std::unexpected(chunkLayoutResult.error());
    }
    m_chunkDescriptorSetLayout = chunkLayoutResult.value();

    // Define push constant range for VP matrix + time
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ChunkPushConstants);

    // Create pipeline layout with descriptor set layout and push constants
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_chunkDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VkResult layoutResult = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout);
    if (layoutResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create pipeline layout: {}", static_cast<int>(layoutResult));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(layoutResult), "Failed to create pipeline layout"));
    }

    std::string vertPath = shaderDir + "/chunk.vert.spv";
    std::string gbufferFragPath = shaderDir + "/gbuffer.frag.spv";

    // Geometry fill pipeline — outputs to G-Buffer MRT (2 color attachments + depth)
    PipelineConfig fillConfig;
    fillConfig.polygonMode = VK_POLYGON_MODE_FILL;
    fillConfig.depthTestEnable = true;
    fillConfig.depthWriteEnable = true;
    fillConfig.vertShaderPath = vertPath;
    fillConfig.fragShaderPath = gbufferFragPath;
    fillConfig.colorAttachmentFormats = {GBUFFER_RT0_FORMAT, GBUFFER_RT1_FORMAT};
    fillConfig.depthAttachmentFormat = GBUFFER_DEPTH_FORMAT;

    auto fillResult = buildPipeline(fillConfig);
    if (!fillResult.has_value())
    {
        return std::unexpected(fillResult.error());
    }
    m_pipeline = fillResult.value();

    // Wireframe pipeline (optional)
    PipelineConfig wireConfig = fillConfig;
    wireConfig.polygonMode = VK_POLYGON_MODE_LINE;

    auto wireResult = buildPipeline(wireConfig);
    if (!wireResult.has_value())
    {
        VX_LOG_WARN("Wireframe pipeline creation failed — wireframe mode disabled");
    }
    else
    {
        m_wireframePipeline = wireResult.value();
    }

    // Translucent forward pipeline: alpha blending, depth test (no write), single swapchain output
    {
        std::string transFragPath = shaderDir + "/translucent.frag.spv";
        PipelineConfig transConfig;
        transConfig.polygonMode = VK_POLYGON_MODE_FILL;
        transConfig.depthTestEnable = true;
        transConfig.depthWriteEnable = false; // depth read only for translucent
        transConfig.enableBlending = true;
        transConfig.cullMode = VK_CULL_MODE_NONE; // glass visible from both sides
        transConfig.vertShaderPath = vertPath;
        transConfig.fragShaderPath = transFragPath;
        transConfig.colorAttachmentFormats = {m_vulkanContext.getSwapchainFormat()};
        transConfig.depthAttachmentFormat = GBUFFER_DEPTH_FORMAT;

        auto transResult = buildPipeline(transConfig);
        if (!transResult.has_value())
        {
            return std::unexpected(transResult.error());
        }
        m_translucentPipeline = transResult.value();
        VX_LOG_INFO("Translucent pipeline created");
    }

    // Create compute pipeline layout — same descriptor set layout, but with compute push constants
    VkPushConstantRange computePushRange{};
    computePushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    computePushRange.offset = 0;
    computePushRange.size = sizeof(CullPushConstants);

    VkPipelineLayoutCreateInfo computeLayoutInfo{};
    computeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayoutInfo.setLayoutCount = 1;
    computeLayoutInfo.pSetLayouts = &m_chunkDescriptorSetLayout;
    computeLayoutInfo.pushConstantRangeCount = 1;
    computeLayoutInfo.pPushConstantRanges = &computePushRange;

    VkResult computeLayoutResult =
        vkCreatePipelineLayout(device, &computeLayoutInfo, nullptr, &m_computePipelineLayout);
    if (computeLayoutResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create compute pipeline layout: {}", static_cast<int>(computeLayoutResult));
        return std::unexpected(core::EngineError::vulkan(
            static_cast<int32_t>(computeLayoutResult), "Failed to create compute pipeline layout"));
    }

    // Load cull.comp shader and create compute pipeline
    std::string cullPath = shaderDir + "/cull.comp.spv";
    auto cullShaderResult = loadShaderModule(cullPath);
    if (!cullShaderResult.has_value())
    {
        return std::unexpected(cullShaderResult.error());
    }
    VkShaderModule cullShader = cullShaderResult.value();

    VkPipelineShaderStageCreateInfo computeStageInfo{};
    computeStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeStageInfo.module = cullShader;
    computeStageInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineInfo{};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage = computeStageInfo;
    computePipelineInfo.layout = m_computePipelineLayout;

    VkResult cullPipelineResult =
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &m_cullPipeline);
    vkDestroyShaderModule(device, cullShader, nullptr);

    if (cullPipelineResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create compute culling pipeline: {}", static_cast<int>(cullPipelineResult));
        return std::unexpected(core::EngineError::vulkan(
            static_cast<int32_t>(cullPipelineResult), "Failed to create compute culling pipeline"));
    }

    VX_LOG_INFO("Compute culling pipeline created");

    // Create translucent cull compute pipeline (same layout, different shader)
    std::string cullTransPath = shaderDir + "/cull_translucent.comp.spv";
    auto cullTransShaderResult = loadShaderModule(cullTransPath);
    if (!cullTransShaderResult.has_value())
    {
        return std::unexpected(cullTransShaderResult.error());
    }
    VkShaderModule cullTransShader = cullTransShaderResult.value();

    VkPipelineShaderStageCreateInfo computeTransStageInfo{};
    computeTransStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeTransStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeTransStageInfo.module = cullTransShader;
    computeTransStageInfo.pName = "main";

    VkComputePipelineCreateInfo computeTransPipelineInfo{};
    computeTransPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeTransPipelineInfo.stage = computeTransStageInfo;
    computeTransPipelineInfo.layout = m_computePipelineLayout;

    VkResult cullTransPipelineResult =
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeTransPipelineInfo, nullptr, &m_cullTranslucentPipeline);
    vkDestroyShaderModule(device, cullTransShader, nullptr);

    if (cullTransPipelineResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create translucent culling pipeline: {}", static_cast<int>(cullTransPipelineResult));
        return std::unexpected(core::EngineError::vulkan(
            static_cast<int32_t>(cullTransPipelineResult), "Failed to create translucent culling pipeline"));
    }

    VX_LOG_INFO("Translucent culling pipeline created");

    // Create translucent indirect draw buffer
    auto transIndirectResult = IndirectDrawBuffer::create(m_vulkanContext, MAX_RENDERABLE_SECTIONS);
    if (!transIndirectResult.has_value())
    {
        return std::unexpected(transIndirectResult.error());
    }
    m_transIndirectDrawBuffer = std::move(transIndirectResult.value());

    // Allocate chunk descriptor set and write gigabuffer to binding 0
    auto descriptorSetResult = m_descriptorAllocator->allocate(m_chunkDescriptorSetLayout);
    if (!descriptorSetResult.has_value())
    {
        return std::unexpected(descriptorSetResult.error());
    }
    m_chunkDescriptorSet = descriptorSetResult.value();

    // Write all 5 bindings in a batch
    VkDescriptorBufferInfo gigabufferInfo{};
    gigabufferInfo.buffer = m_gigabuffer->getBuffer();
    gigabufferInfo.offset = 0;
    gigabufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo chunkRenderInfoInfo{};
    chunkRenderInfoInfo.buffer = m_chunkRenderInfoBuffer->getBuffer();
    chunkRenderInfoInfo.offset = 0;
    chunkRenderInfoInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo indirectCommandInfo{};
    indirectCommandInfo.buffer = m_indirectDrawBuffer->getCommandBuffer();
    indirectCommandInfo.offset = 0;
    indirectCommandInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo indirectCountInfo{};
    indirectCountInfo.buffer = m_indirectDrawBuffer->getCountBuffer();
    indirectCountInfo.offset = 0;
    indirectCountInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo textureInfo{};
    textureInfo.sampler = m_textureArray->getSampler();
    textureInfo.imageView = m_textureArray->getImageView();
    textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo tintPaletteInfo{};
    tintPaletteInfo.buffer = m_tintPaletteBuffer;
    tintPaletteInfo.offset = 0;
    tintPaletteInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};

    // Binding 0: gigabuffer SSBO
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_chunkDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].pBufferInfo = &gigabufferInfo;

    // Binding 1: ChunkRenderInfo SSBO
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_chunkDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].pBufferInfo = &chunkRenderInfoInfo;

    // Binding 2: indirect command buffer
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_chunkDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].pBufferInfo = &indirectCommandInfo;

    // Binding 3: indirect draw count buffer
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = m_chunkDescriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[3].pBufferInfo = &indirectCountInfo;

    // Binding 4: block texture array
    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = m_chunkDescriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[4].pImageInfo = &textureInfo;

    // Binding 5: tint palette SSBO
    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = m_chunkDescriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[5].pBufferInfo = &tintPaletteInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

    // Allocate translucent descriptor set (same layout, but with translucent indirect buffers)
    auto transDescResult = m_descriptorAllocator->allocate(m_chunkDescriptorSetLayout);
    if (!transDescResult.has_value())
    {
        return std::unexpected(transDescResult.error());
    }
    m_transDescriptorSet = transDescResult.value();

    // Write translucent descriptor set: same gigabuffer + chunkRenderInfo, but different indirect buffers
    VkDescriptorBufferInfo transIndirectCommandInfo{};
    transIndirectCommandInfo.buffer = m_transIndirectDrawBuffer->getCommandBuffer();
    transIndirectCommandInfo.offset = 0;
    transIndirectCommandInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo transIndirectCountInfo{};
    transIndirectCountInfo.buffer = m_transIndirectDrawBuffer->getCountBuffer();
    transIndirectCountInfo.offset = 0;
    transIndirectCountInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 6> transDescWrites{};

    // Binding 0: same gigabuffer
    transDescWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[0].dstSet = m_transDescriptorSet;
    transDescWrites[0].dstBinding = 0;
    transDescWrites[0].descriptorCount = 1;
    transDescWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transDescWrites[0].pBufferInfo = &gigabufferInfo;

    // Binding 1: same ChunkRenderInfo
    transDescWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[1].dstSet = m_transDescriptorSet;
    transDescWrites[1].dstBinding = 1;
    transDescWrites[1].descriptorCount = 1;
    transDescWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transDescWrites[1].pBufferInfo = &chunkRenderInfoInfo;

    // Binding 2: translucent indirect command buffer
    transDescWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[2].dstSet = m_transDescriptorSet;
    transDescWrites[2].dstBinding = 2;
    transDescWrites[2].descriptorCount = 1;
    transDescWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transDescWrites[2].pBufferInfo = &transIndirectCommandInfo;

    // Binding 3: translucent indirect draw count buffer
    transDescWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[3].dstSet = m_transDescriptorSet;
    transDescWrites[3].dstBinding = 3;
    transDescWrites[3].descriptorCount = 1;
    transDescWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transDescWrites[3].pBufferInfo = &transIndirectCountInfo;

    // Binding 4: same texture array
    transDescWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[4].dstSet = m_transDescriptorSet;
    transDescWrites[4].dstBinding = 4;
    transDescWrites[4].descriptorCount = 1;
    transDescWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    transDescWrites[4].pImageInfo = &textureInfo;

    // Binding 5: same tint palette SSBO
    transDescWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transDescWrites[5].dstSet = m_transDescriptorSet;
    transDescWrites[5].dstBinding = 5;
    transDescWrites[5].descriptorCount = 1;
    transDescWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transDescWrites[5].pBufferInfo = &tintPaletteInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(transDescWrites.size()), transDescWrites.data(), 0, nullptr);

    // Create depth buffer and other extent-dependent resources
    auto swapResResult = createSwapchainResources();
    if (!swapResResult.has_value())
    {
        return std::unexpected(swapResResult.error());
    }

    // Create G-Buffer for deferred rendering
    auto gbufferResult = GBuffer::create(m_vulkanContext, m_vulkanContext.getSwapchainExtent());
    if (!gbufferResult.has_value())
    {
        return std::unexpected(gbufferResult.error());
    }
    m_gbuffer = std::move(gbufferResult.value());

    // Create lighting pipeline (descriptor layout + pipeline layout + pipeline)
    auto lightingResult = createLightingPipeline(shaderDir);
    if (!lightingResult.has_value())
    {
        return std::unexpected(lightingResult.error());
    }

    // Allocate lighting descriptor set and write G-Buffer bindings
    auto lightingDescResult = m_descriptorAllocator->allocate(m_lightingDescriptorSetLayout);
    if (!lightingDescResult.has_value())
    {
        return std::unexpected(lightingDescResult.error());
    }
    m_lightingDescriptorSet = lightingDescResult.value();
    writeLightingDescriptors();

    // Initialize ImGui
    auto imguiResult = ImGuiBackend::create(m_vulkanContext, window.getHandle());
    if (!imguiResult.has_value())
    {
        return std::unexpected(imguiResult.error());
    }
    m_imguiBackend = std::move(imguiResult.value());

    // Initialize tint palette with default Plains biome colors
    TintPalette defaultPalette = TintPalette::buildForBiome(world::BiomeType::Plains);
    updateTintPalette(defaultPalette);

    m_isInitialized = true;
    VX_LOG_INFO("Renderer initialized — {} frames in flight, deferred rendering active", FRAMES_IN_FLIGHT);
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
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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

    result =
        vkCreateImageView(m_vulkanContext.getDevice(), &depthViewInfo, nullptr, &m_swapchainResources.depthImageView);
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

core::Result<void> Renderer::createLightingPipeline(const std::string& shaderDir)
{
    VkDevice device = m_vulkanContext.getDevice();

    // Build lighting descriptor set layout: 3 combined image samplers for G-Buffer reads
    DescriptorLayoutBuilder lightBuilder;
    auto lightLayoutResult =
        lightBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // RT0
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // RT1
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // Depth
            .build(device);
    if (!lightLayoutResult.has_value())
    {
        return std::unexpected(lightLayoutResult.error());
    }
    m_lightingDescriptorSetLayout = lightLayoutResult.value();

    // Create lighting pipeline layout with lighting descriptor set + LightingPushConstants
    VkPushConstantRange lightingPushRange{};
    lightingPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightingPushRange.offset = 0;
    lightingPushRange.size = sizeof(LightingPushConstants);

    VkPipelineLayoutCreateInfo lightingLayoutInfo{};
    lightingLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightingLayoutInfo.setLayoutCount = 1;
    lightingLayoutInfo.pSetLayouts = &m_lightingDescriptorSetLayout;
    lightingLayoutInfo.pushConstantRangeCount = 1;
    lightingLayoutInfo.pPushConstantRanges = &lightingPushRange;

    VkResult result = vkCreatePipelineLayout(device, &lightingLayoutInfo, nullptr, &m_lightingPipelineLayout);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create lighting pipeline layout: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create lighting pipeline layout"));
    }

    // Build lighting pipeline: fullscreen triangle, no depth, single swapchain color output
    VkFormat swapFmt = m_vulkanContext.getSwapchainFormat();
    PipelineConfig lightingConfig;
    lightingConfig.vertShaderPath = shaderDir + "/lighting.vert.spv";
    lightingConfig.fragShaderPath = shaderDir + "/lighting.frag.spv";
    lightingConfig.colorAttachmentFormats = {swapFmt};
    lightingConfig.depthAttachmentFormat = VK_FORMAT_UNDEFINED; // no depth for fullscreen pass
    lightingConfig.depthTestEnable = false;
    lightingConfig.depthWriteEnable = false;
    lightingConfig.cullMode = VK_CULL_MODE_NONE; // fullscreen triangle
    lightingConfig.pipelineLayout = m_lightingPipelineLayout;

    auto lightingPipelineResult = buildPipeline(lightingConfig);
    if (!lightingPipelineResult.has_value())
    {
        return std::unexpected(lightingPipelineResult.error());
    }
    m_lightingPipeline = lightingPipelineResult.value();

    VX_LOG_INFO("Lighting pipeline created (deferred rendering)");
    return {};
}

void Renderer::writeLightingDescriptors()
{
    VkDescriptorImageInfo albedoInfo{};
    albedoInfo.sampler = m_gbuffer->getSampler();
    albedoInfo.imageView = m_gbuffer->getAlbedoView();
    albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.sampler = m_gbuffer->getSampler();
    normalInfo.imageView = m_gbuffer->getNormalView();
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = m_gbuffer->getSampler();
    depthInfo.imageView = m_swapchainResources.depthImageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 3> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_lightingDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &albedoInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_lightingDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &normalInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_lightingDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &depthInfo;

    vkUpdateDescriptorSets(m_vulkanContext.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
    raster.cullMode = config.cullMode;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // Y-flip in projection reverses winding: world CCW → framebuffer CW
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // One blend attachment per color attachment
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(config.colorAttachmentFormats.size());
    for (auto& blend : blendAttachments)
    {
        blend.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = config.enableBlending ? VK_TRUE : VK_FALSE;
        if (config.enableBlending)
        {
            // Standard alpha blending: srcColor * srcAlpha + dstColor * (1 - srcAlpha)
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blendState.pAttachments = blendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<uint32_t>(config.colorAttachmentFormats.size());
    rendering.pColorAttachmentFormats = config.colorAttachmentFormats.data();
    rendering.depthAttachmentFormat = config.depthAttachmentFormat;

    VkPipelineLayout layoutToUse = config.pipelineLayout != VK_NULL_HANDLE ? config.pipelineLayout : m_pipelineLayout;

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
    ci.layout = layoutToUse;
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

void Renderer::transitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.image = image;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
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
            VX_LOG_ERROR("Failed to recreate depth resources after swapchain resize — renderer disabled");
            m_isInitialized = false;
            m_needsSwapchainRecreate = false;
            return false;
        }

        // Recreate G-Buffer at new swapchain extent
        m_gbuffer.reset();
        auto gbufResult = GBuffer::create(m_vulkanContext, m_vulkanContext.getSwapchainExtent());
        if (!gbufResult.has_value())
        {
            VX_LOG_ERROR("Failed to recreate G-Buffer after swapchain resize — renderer disabled");
            m_isInitialized = false;
            m_needsSwapchainRecreate = false;
            return false;
        }
        m_gbuffer = std::move(gbufResult.value());
        writeLightingDescriptors();

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

    // Transition G-Buffer images for geometry pass (swapchain transition deferred to endFrame)
    transitionImage(cmd, m_gbuffer->getAlbedoImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(cmd, m_gbuffer->getNormalImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(
        cmd,
        m_swapchainResources.depthImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    // Render pass is NOT started here — deferred until renderChunksIndirect()
    // so that compute dispatch can happen before the render pass.
    m_useWireframe = overlay.wireframeMode && m_wireframePipeline != VK_NULL_HANDLE;
    m_renderPassActive = false;

    // Begin ImGui frame — caller will build UI between beginFrame/endFrame
    m_imguiBackend->beginFrame();

    m_currentWindow = &window;
    m_frameActive = true;
    return true;
}

void Renderer::beginRenderPass()
{
    if (m_renderPassActive)
    {
        return;
    }
    m_renderPassActive = true;

    auto& frame = m_frames[m_frameIndex];
    VkCommandBuffer cmd = frame.commandBuffer;
    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    // G-Buffer geometry pass: 2 color attachments (RT0 albedo+AO, RT1 normal) + depth
    std::array<VkRenderingAttachmentInfo, 2> colorAttachments{};

    // RT0: albedo.rgb + AO.a
    colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[0].imageView = m_gbuffer->getAlbedoView();
    colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    // RT1: octahedral normal
    colorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[1].imageView = m_gbuffer->getNormalView();
    colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[1].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

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
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_useWireframe ? m_wireframePipeline : m_pipeline);
}

void Renderer::renderChunksIndirect(
    const glm::mat4& viewProjection,
    const std::array<glm::vec4, 6>& frustumPlanes)
{
    if (!m_frameActive)
    {
        return;
    }

    auto& frame = m_frames[m_frameIndex];
    VkCommandBuffer cmd = frame.commandBuffer;

    m_lastViewProjection = viewProjection;

    uint32_t totalSections = m_chunkRenderInfoBuffer->getHighWaterMark();
    if (totalSections == 0)
    {
        m_lastDrawCount = 0;
        m_lastQuadCount = 0;
        beginRenderPass(); // ensure render pass is active for subsequent ImGui rendering
        return;
    }

    // 1. Reset draw counts to 0 via vkCmdFillBuffer (opaque + translucent)
    m_indirectDrawBuffer->recordCountReset(cmd);
    m_transIndirectDrawBuffer->recordCountReset(cmd);

    // 2. Barrier: TRANSFER_DST → COMPUTE_SHADER (fill completes before compute reads/writes)
    VkMemoryBarrier2 fillToComputeBarrier{};
    fillToComputeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    fillToComputeBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    fillToComputeBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    fillToComputeBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    fillToComputeBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

    VkDependencyInfo fillDep{};
    fillDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    fillDep.memoryBarrierCount = 1;
    fillDep.pMemoryBarriers = &fillToComputeBarrier;
    vkCmdPipelineBarrier2(cmd, &fillDep);

    // 3. Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &m_chunkDescriptorSet, 0, nullptr);

    // 4. Push compute constants: frustum planes + totalSections
    CullPushConstants cullPC{};
    for (int i = 0; i < 6; ++i)
    {
        cullPC.frustumPlanes[i] = frustumPlanes[static_cast<size_t>(i)];
    }
    cullPC.totalSections = totalSections;

    vkCmdPushConstants(
        cmd,
        m_computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(CullPushConstants),
        &cullPC);

    // 5. Dispatch opaque compute shader: ceil(totalSections / 64)
    uint32_t groupCount = (totalSections + 63) / 64;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // 5b. Dispatch translucent cull shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullTranslucentPipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &m_transDescriptorSet, 0, nullptr);
    vkCmdPushConstants(
        cmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPushConstants), &cullPC);
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // 6. Barrier: COMPUTE → DRAW_INDIRECT + VERTEX_SHADER
    VkMemoryBarrier2 computeToDrawBarrier{};
    computeToDrawBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    computeToDrawBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    computeToDrawBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    computeToDrawBarrier.dstStageMask =
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    computeToDrawBarrier.dstAccessMask =
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

    VkDependencyInfo drawDep{};
    drawDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    drawDep.memoryBarrierCount = 1;
    drawDep.pMemoryBarriers = &computeToDrawBarrier;
    vkCmdPipelineBarrier2(cmd, &drawDep);

    // 7. Begin render pass (deferred from beginFrame to allow compute dispatch first)
    beginRenderPass();

    // 8. Bind descriptor set for graphics
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_chunkDescriptorSet, 0, nullptr);

    // 9. Bind shared quad index buffer
    m_quadIndexBuffer->bind(cmd);

    // 10. Push graphics constants: VP + time + lighting params (no chunkWorldPos)
    float currentTime = static_cast<float>(glfwGetTime());
    ChunkPushConstants pc{};
    pc.viewProjection = viewProjection;
    pc.time = currentTime;
    pc.ambientStrength = 0.3f;
    pc.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)), 0.0f);

    vkCmdPushConstants(
        cmd,
        m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(ChunkPushConstants),
        &pc);

    // 11. Indirect draw: vkCmdDrawIndexedIndirectCount
    vkCmdDrawIndexedIndirectCount(
        cmd,
        m_indirectDrawBuffer->getCommandBuffer(), // buffer with VkDrawIndexedIndirectCommand[]
        0,                                         // offset into command buffer
        m_indirectDrawBuffer->getCountBuffer(),    // buffer with uint32_t draw count
        0,                                         // offset into count buffer
        MAX_RENDERABLE_SECTIONS,                   // maxDrawCount (GPU clamps to this)
        sizeof(VkDrawIndexedIndirectCommand));     // stride

    // V1: display highWaterMark as approximate stats — no GPU readback
    m_lastDrawCount = totalSections; // upper bound (actual may be less due to culling)
    m_lastQuadCount = 0;            // unknown without readback
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
    VkExtent2D extent = m_vulkanContext.getSwapchainExtent();

    // Ensure G-Buffer render pass is active (may not be if renderChunksIndirect wasn't called)
    beginRenderPass();

    // ── End G-Buffer geometry pass ──────────────────────────────────────────
    vkCmdEndRendering(cmd);
    m_renderPassActive = false;

    // ── Transition G-Buffer images for lighting read ────────────────────────
    transitionImage(cmd, m_gbuffer->getAlbedoImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transitionImage(cmd, m_gbuffer->getNormalImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transitionImage(
        cmd,
        m_swapchainResources.depthImage,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    // ── Transition swapchain image for lighting output ──────────────────────
    VkImage swapchainImage = m_vulkanContext.getSwapchainImages()[m_currentImageIndex];
    transitionImage(cmd, swapchainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // ── Begin lighting pass (swapchain only, no depth) ──────────────────────
    VkRenderingAttachmentInfo swapAttachment{};
    swapAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapAttachment.imageView = m_vulkanContext.getSwapchainImageViews()[m_currentImageIndex];
    swapAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // lighting overwrites all pixels
    swapAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo lightingRenderInfo{};
    lightingRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    lightingRenderInfo.renderArea.offset = {0, 0};
    lightingRenderInfo.renderArea.extent = extent;
    lightingRenderInfo.layerCount = 1;
    lightingRenderInfo.colorAttachmentCount = 1;
    lightingRenderInfo.pColorAttachments = &swapAttachment;
    lightingRenderInfo.pDepthAttachment = nullptr; // no depth for fullscreen pass

    vkCmdBeginRendering(cmd, &lightingRenderInfo);

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

    // ── Draw fullscreen lighting triangle ───────────────────────────────────
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipelineLayout, 0, 1, &m_lightingDescriptorSet, 0, nullptr);

    LightingPushConstants lightingPC{};
    lightingPC.sunDirection = glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f));
    lightingPC.ambientStrength = 0.3f;

    vkCmdPushConstants(
        cmd, m_lightingPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LightingPushConstants), &lightingPC);

    vkCmdDraw(cmd, 3, 1, 0, 0); // fullscreen triangle

    vkCmdEndRendering(cmd);

    // ── Translucent forward pass (after lighting, before ImGui) ─────────────
    // Transition depth from SHADER_READ_ONLY back to DEPTH_READ_ONLY for translucent depth test
    {
        VkImageMemoryBarrier2 depthBarrier{};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthBarrier.image = m_swapchainResources.depthImage;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        depthBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        depthBarrier.dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        VkDependencyInfo depthDep{};
        depthDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depthDep.imageMemoryBarrierCount = 1;
        depthDep.pImageMemoryBarriers = &depthBarrier;
        vkCmdPipelineBarrier2(cmd, &depthDep);
    }

    // Begin translucent render pass: swapchain color (load existing) + depth (read-only)
    VkRenderingAttachmentInfo transColorAttach{};
    transColorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    transColorAttach.imageView = m_vulkanContext.getSwapchainImageViews()[m_currentImageIndex];
    transColorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    transColorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // preserve lighting output
    transColorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo transDepthAttach{};
    transDepthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    transDepthAttach.imageView = m_swapchainResources.depthImageView;
    transDepthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    transDepthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // read existing depth
    transDepthAttach.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

    VkRenderingInfo transRenderInfo{};
    transRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    transRenderInfo.renderArea.offset = {0, 0};
    transRenderInfo.renderArea.extent = extent;
    transRenderInfo.layerCount = 1;
    transRenderInfo.colorAttachmentCount = 1;
    transRenderInfo.pColorAttachments = &transColorAttach;
    transRenderInfo.pDepthAttachment = &transDepthAttach;

    vkCmdBeginRendering(cmd, &transRenderInfo);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind translucent pipeline and draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_translucentPipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_transDescriptorSet, 0, nullptr);
    m_quadIndexBuffer->bind(cmd);

    ChunkPushConstants transPC{};
    transPC.viewProjection = m_lastViewProjection;
    transPC.time = static_cast<float>(glfwGetTime());
    transPC.ambientStrength = 0.3f;
    transPC.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)), 0.0f);
    vkCmdPushConstants(
        cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(ChunkPushConstants), &transPC);

    vkCmdDrawIndexedIndirectCount(
        cmd,
        m_transIndirectDrawBuffer->getCommandBuffer(),
        0,
        m_transIndirectDrawBuffer->getCountBuffer(),
        0,
        MAX_RENDERABLE_SECTIONS,
        sizeof(VkDrawIndexedIndirectCommand));

    vkCmdEndRendering(cmd);

    // ── Begin final swapchain pass for ImGui ────────────────────────────────
    VkRenderingAttachmentInfo imguiAttach{};
    imguiAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    imguiAttach.imageView = m_vulkanContext.getSwapchainImageViews()[m_currentImageIndex];
    imguiAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imguiAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    imguiAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo imguiRenderInfo{};
    imguiRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    imguiRenderInfo.renderArea.offset = {0, 0};
    imguiRenderInfo.renderArea.extent = extent;
    imguiRenderInfo.layerCount = 1;
    imguiRenderInfo.colorAttachmentCount = 1;
    imguiRenderInfo.pColorAttachments = &imguiAttach;
    imguiRenderInfo.pDepthAttachment = nullptr;

    vkCmdBeginRendering(cmd, &imguiRenderInfo);

    // ── Render ImGui on top of the composited result ────────────────────────
    m_imguiBackend->render(cmd);

    vkCmdEndRendering(cmd);

    // ── Transition swapchain for presentation ───────────────────────────────
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

void Renderer::updateTintPalette(const TintPalette& palette)
{
    if (m_tintPaletteMapped == nullptr)
    {
        return;
    }
    for (uint8_t i = 0; i < TintPalette::MAX_ENTRIES; ++i)
    {
        glm::vec3 color = palette.getColor(i);
        m_tintPaletteMapped[i] = glm::vec4(color, 1.0f);
    }
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
    m_textureArray.reset();

    // Destroy tint palette buffer before descriptor allocator
    if (m_tintPaletteBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_vulkanContext.getAllocator(), m_tintPaletteBuffer, m_tintPaletteAllocation);
        m_tintPaletteBuffer = VK_NULL_HANDLE;
        m_tintPaletteAllocation = VK_NULL_HANDLE;
        m_tintPaletteMapped = nullptr;
    }

    // Destroy indirect/ChunkRenderInfo buffers before DescriptorAllocator (they are referenced by descriptors)
    m_transIndirectDrawBuffer.reset();
    m_indirectDrawBuffer.reset();
    m_chunkRenderInfoBuffer.reset();

    m_quadIndexBuffer.reset();

    m_gbuffer.reset();
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

    if (m_cullTranslucentPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_cullTranslucentPipeline, nullptr);
        m_cullTranslucentPipeline = VK_NULL_HANDLE;
    }

    if (m_cullPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_cullPipeline, nullptr);
        m_cullPipeline = VK_NULL_HANDLE;
    }

    if (m_translucentPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_translucentPipeline, nullptr);
        m_translucentPipeline = VK_NULL_HANDLE;
    }

    if (m_lightingPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_lightingPipeline, nullptr);
        m_lightingPipeline = VK_NULL_HANDLE;
    }

    // Destroy descriptor infrastructure (pools first, then layout, then pipeline layout)
    m_chunkDescriptorSet = VK_NULL_HANDLE;    // owned by pool, destroyed with it
    m_transDescriptorSet = VK_NULL_HANDLE;    // owned by pool, destroyed with it
    m_lightingDescriptorSet = VK_NULL_HANDLE; // owned by pool, destroyed with it
    m_descriptorAllocator.reset();

    if (m_chunkDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_chunkDescriptorSetLayout, nullptr);
        m_chunkDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_lightingDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_lightingDescriptorSetLayout, nullptr);
        m_lightingDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_lightingPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_lightingPipelineLayout, nullptr);
        m_lightingPipelineLayout = VK_NULL_HANDLE;
    }

    if (m_computePipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_computePipelineLayout, nullptr);
        m_computePipelineLayout = VK_NULL_HANDLE;
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
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};

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
