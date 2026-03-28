#include "voxel/renderer/GBuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

namespace voxel::renderer
{

core::Result<std::unique_ptr<GBuffer>> GBuffer::create(VulkanContext& context, VkExtent2D extent)
{
    auto gbuffer = std::unique_ptr<GBuffer>(new GBuffer());
    gbuffer->m_allocator = context.getAllocator();
    gbuffer->m_device = context.getDevice();
    gbuffer->m_extent = extent;

    // ── RT0: albedo.rgb + AO.a ──────────────────────────────────────────────
    VkImageCreateInfo albedoImageInfo{};
    albedoImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    albedoImageInfo.imageType = VK_IMAGE_TYPE_2D;
    albedoImageInfo.format = GBUFFER_RT0_FORMAT;
    albedoImageInfo.extent = {extent.width, extent.height, 1};
    albedoImageInfo.mipLevels = 1;
    albedoImageInfo.arrayLayers = 1;
    albedoImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    albedoImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    albedoImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkResult result = vmaCreateImage(
        gbuffer->m_allocator,
        &albedoImageInfo,
        &allocInfo,
        &gbuffer->m_albedoImage,
        &gbuffer->m_albedoAllocation,
        nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create G-Buffer RT0 image: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create G-Buffer RT0 image"));
    }

    // ── RT1: octahedral normal.xy ───────────────────────────────────────────
    VkImageCreateInfo normalImageInfo = albedoImageInfo;
    normalImageInfo.format = GBUFFER_RT1_FORMAT;

    result = vmaCreateImage(
        gbuffer->m_allocator,
        &normalImageInfo,
        &allocInfo,
        &gbuffer->m_normalImage,
        &gbuffer->m_normalAllocation,
        nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create G-Buffer RT1 image: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create G-Buffer RT1 image"));
    }

    // ── Image views ─────────────────────────────────────────────────────────
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    viewInfo.image = gbuffer->m_albedoImage;
    viewInfo.format = GBUFFER_RT0_FORMAT;
    result = vkCreateImageView(gbuffer->m_device, &viewInfo, nullptr, &gbuffer->m_albedoView);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create G-Buffer RT0 image view: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create G-Buffer RT0 image view"));
    }

    viewInfo.image = gbuffer->m_normalImage;
    viewInfo.format = GBUFFER_RT1_FORMAT;
    result = vkCreateImageView(gbuffer->m_device, &viewInfo, nullptr, &gbuffer->m_normalView);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create G-Buffer RT1 image view: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create G-Buffer RT1 image view"));
    }

    // ── Shared sampler (nearest, clamp-to-edge) ─────────────────────────────
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    result = vkCreateSampler(gbuffer->m_device, &samplerInfo, nullptr, &gbuffer->m_sampler);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create G-Buffer sampler: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create G-Buffer sampler"));
    }

    VX_LOG_INFO("G-Buffer created {}x{} (RT0={}, RT1={})", extent.width, extent.height, "R8G8B8A8_SRGB", "R16G16_SFLOAT");
    return gbuffer;
}

GBuffer::~GBuffer()
{
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_sampler, nullptr);
    }
    if (m_normalView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_normalView, nullptr);
    }
    if (m_normalImage != VK_NULL_HANDLE)
    {
        vmaDestroyImage(m_allocator, m_normalImage, m_normalAllocation);
    }
    if (m_albedoView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_albedoView, nullptr);
    }
    if (m_albedoImage != VK_NULL_HANDLE)
    {
        vmaDestroyImage(m_allocator, m_albedoImage, m_albedoAllocation);
    }
}

} // namespace voxel::renderer
