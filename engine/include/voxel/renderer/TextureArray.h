#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace voxel::renderer
{

class VulkanContext;

/**
 * @brief GPU-resident 2D array texture for block textures.
 *
 * Loads all block textures from a manifest file (textures.json) into a single
 * VkImage with VK_IMAGE_VIEW_TYPE_2D_ARRAY. Generates mipmaps via blit chain.
 * Missing textures fall back to a magenta/black checkerboard.
 *
 * RAII: private ctor, static create() factory, deleted copy/move.
 */
class TextureArray
{
  public:
    /**
     * @brief Loads block textures and creates a GPU texture array with mipmaps.
     *
     * @param context The Vulkan context providing device, allocator, and queues.
     * @param textureDir Path to directory containing textures.json manifest and .png files.
     * @return The created TextureArray, or an error on failure.
     */
    static core::Result<std::unique_ptr<TextureArray>> create(VulkanContext& context, const std::string& textureDir);

    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;
    TextureArray(TextureArray&&) = delete;
    TextureArray& operator=(TextureArray&&) = delete;

    [[nodiscard]] VkImageView getImageView() const { return m_imageView; }
    [[nodiscard]] VkSampler getSampler() const { return m_sampler; }
    [[nodiscard]] uint32_t getLayerCount() const { return m_layerCount; }

    /// Returns the texture array layer index for the given texture name.
    /// Returns 0 (fallback) if the name is not found.
    [[nodiscard]] uint16_t getLayerIndex(const std::string& name) const;

  private:
    TextureArray() = default;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for RAII cleanup
    VkDevice m_device = VK_NULL_HANDLE;        // non-owning, for RAII cleanup

    uint32_t m_layerCount = 0;
    std::unordered_map<std::string, uint16_t> m_nameToLayer;
};

} // namespace voxel::renderer
