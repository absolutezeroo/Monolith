#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <memory>

namespace voxel::renderer
{

class VulkanContext;

/**
 * @brief G-Buffer for deferred rendering: three color render targets (albedo+AO, normal, light).
 *
 * Does NOT own the depth buffer — that is shared with SwapchainResources in Renderer.
 * Follows RAII factory pattern: private ctor, static create(), deleted copy/move.
 */
class GBuffer
{
  public:
    /**
     * @brief Creates G-Buffer images, views, and a shared sampler at the given resolution.
     *
     * @param context The Vulkan context providing device and allocator.
     * @param extent The G-Buffer resolution (must match swapchain extent).
     * @return The created GBuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<GBuffer>> create(VulkanContext& context, VkExtent2D extent);

    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;
    GBuffer(GBuffer&&) = delete;
    GBuffer& operator=(GBuffer&&) = delete;

    [[nodiscard]] VkImage getAlbedoImage() const { return m_albedoImage; }
    [[nodiscard]] VkImageView getAlbedoView() const { return m_albedoView; }
    [[nodiscard]] VkImage getNormalImage() const { return m_normalImage; }
    [[nodiscard]] VkImageView getNormalView() const { return m_normalView; }
    [[nodiscard]] VkImage getLightImage() const { return m_lightImage; }
    [[nodiscard]] VkImageView getLightView() const { return m_lightView; }
    [[nodiscard]] VkSampler getSampler() const { return m_sampler; }
    [[nodiscard]] VkExtent2D getExtent() const { return m_extent; }

  private:
    GBuffer() = default;

    VkImage m_albedoImage = VK_NULL_HANDLE;
    VmaAllocation m_albedoAllocation = VK_NULL_HANDLE;
    VkImageView m_albedoView = VK_NULL_HANDLE;

    VkImage m_normalImage = VK_NULL_HANDLE;
    VmaAllocation m_normalAllocation = VK_NULL_HANDLE;
    VkImageView m_normalView = VK_NULL_HANDLE;

    VkImage m_lightImage = VK_NULL_HANDLE;
    VmaAllocation m_lightAllocation = VK_NULL_HANDLE;
    VkImageView m_lightView = VK_NULL_HANDLE;

    VkSampler m_sampler = VK_NULL_HANDLE;

    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for RAII cleanup
    VkDevice m_device = VK_NULL_HANDLE;        // non-owning, for RAII cleanup
    VkExtent2D m_extent{};
};

} // namespace voxel::renderer
