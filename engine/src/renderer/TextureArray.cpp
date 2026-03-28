#include "voxel/renderer/TextureArray.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace voxel::renderer
{

namespace
{

/// Generate a 16x16 magenta/black checkerboard pattern as the fallback texture.
std::vector<uint8_t> generateCheckerboard()
{
    constexpr uint32_t SIZE = BLOCK_TEXTURE_SIZE;
    std::vector<uint8_t> pixels(SIZE * SIZE * 4);

    for (uint32_t y = 0; y < SIZE; ++y)
    {
        for (uint32_t x = 0; x < SIZE; ++x)
        {
            uint32_t idx = (y * SIZE + x) * 4;
            bool isMagenta = ((x / 2 + y / 2) % 2) == 0;
            pixels[idx + 0] = isMagenta ? 255 : 0; // R
            pixels[idx + 1] = 0;                    // G
            pixels[idx + 2] = isMagenta ? 255 : 0;  // B
            pixels[idx + 3] = 255;                   // A
        }
    }
    return pixels;
}

/// Load texture names from the textures.json manifest.
core::Result<std::vector<std::string>> loadManifest(const std::string& textureDir)
{
    std::string manifestPath = textureDir + "/textures.json";
    std::ifstream file(manifestPath);
    if (!file.is_open())
    {
        VX_LOG_ERROR("TextureArray: cannot open manifest: {}", manifestPath);
        return std::unexpected(core::EngineError::file(manifestPath));
    }

    nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_array())
    {
        VX_LOG_ERROR("TextureArray: invalid manifest JSON: {}", manifestPath);
        return std::unexpected(core::EngineError(core::ErrorCode::InvalidFormat, "TextureArray: invalid manifest JSON"));
    }

    std::vector<std::string> names;
    names.reserve(json.size());
    for (const auto& entry : json)
    {
        if (entry.is_string())
        {
            names.push_back(entry.get<std::string>());
        }
    }

    if (names.empty())
    {
        VX_LOG_ERROR("TextureArray: manifest is empty: {}", manifestPath);
        return std::unexpected(core::EngineError(core::ErrorCode::InvalidFormat, "TextureArray: empty manifest"));
    }

    if (names.size() > MAX_BLOCK_TEXTURES)
    {
        VX_LOG_WARN("TextureArray: manifest has {} textures, clamping to {}", names.size(), MAX_BLOCK_TEXTURES);
        names.resize(MAX_BLOCK_TEXTURES);
    }

    return names;
}

} // anonymous namespace

core::Result<std::unique_ptr<TextureArray>> TextureArray::create(
    VulkanContext& context,
    const std::string& textureDir)
{
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    // Step 1: Load manifest
    auto manifestResult = loadManifest(textureDir);
    if (!manifestResult.has_value())
    {
        return std::unexpected(manifestResult.error());
    }
    const auto& names = manifestResult.value();
    uint32_t layerCount = static_cast<uint32_t>(names.size());

    VX_LOG_INFO("TextureArray: loading {} textures from {}", layerCount, textureDir);

    // Step 2: Load all texture data into CPU memory
    constexpr uint32_t TEX_SIZE = BLOCK_TEXTURE_SIZE;
    constexpr uint32_t BYTES_PER_LAYER = TEX_SIZE * TEX_SIZE * 4;
    std::vector<uint8_t> allPixels(static_cast<size_t>(layerCount) * BYTES_PER_LAYER);
    std::vector<uint8_t> checkerboard = generateCheckerboard();

    for (uint32_t i = 0; i < layerCount; ++i)
    {
        std::string path = textureDir + "/" + names[i] + ".png";
        int w = 0;
        int h = 0;
        int channels = 0;
        uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);

        uint8_t* dest = allPixels.data() + static_cast<size_t>(i) * BYTES_PER_LAYER;

        if (pixels != nullptr && static_cast<uint32_t>(w) == TEX_SIZE && static_cast<uint32_t>(h) == TEX_SIZE)
        {
            std::memcpy(dest, pixels, BYTES_PER_LAYER);
            stbi_image_free(pixels);
        }
        else
        {
            if (pixels != nullptr)
            {
                VX_LOG_WARN("TextureArray: '{}' has wrong size ({}x{}, expected {}x{}), using fallback",
                    names[i], w, h, TEX_SIZE, TEX_SIZE);
                stbi_image_free(pixels);
            }
            else
            {
                VX_LOG_WARN("TextureArray: '{}' not found, using fallback", names[i]);
            }
            std::memcpy(dest, checkerboard.data(), BYTES_PER_LAYER);
        }
    }

    // Step 3: Create staging buffer
    VkDeviceSize stagingSize = static_cast<VkDeviceSize>(allPixels.size());

    VkBufferCreateInfo stagingBufCI{};
    stagingBufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufCI.size = stagingSize;
    stagingBufCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI{};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocInfo{};

    VkResult result = vmaCreateBuffer(allocator, &stagingBufCI, &stagingAllocCI,
        &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: staging buffer creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: staging buffer failed"));
    }

    std::memcpy(stagingAllocInfo.pMappedData, allPixels.data(), allPixels.size());

    // Step 4: Create VkImage
    auto ta = std::unique_ptr<TextureArray>(new TextureArray());
    ta->m_allocator = allocator;
    ta->m_device = device;
    ta->m_layerCount = layerCount;

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageCI.extent = {TEX_SIZE, TEX_SIZE, 1};
    imageCI.mipLevels = BLOCK_TEXTURE_MIP_LEVELS;
    imageCI.arrayLayers = layerCount;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocCI{};
    imageAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    imageAllocCI.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    result = vmaCreateImage(allocator, &imageCI, &imageAllocCI, &ta->m_image, &ta->m_allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: image creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: image creation failed"));
    }

    // Step 5: One-time command buffer for upload + mipmap generation
    VkCommandPool tempPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolCI.queueFamilyIndex = context.getGraphicsQueueFamily();

    result = vkCreateCommandPool(device, &poolCI, nullptr, &tempPool);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: command pool creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: command pool failed"));
    }

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

    // Step 5a: Transition mip 0, all layers: UNDEFINED -> TRANSFER_DST
    VkImageMemoryBarrier2 toDstBarrier{};
    toDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toDstBarrier.image = ta->m_image;
    toDstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toDstBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    toDstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toDstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDstBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount};

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &toDstBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Step 5b: Copy staging buffer to image mip 0, all layers
    std::vector<VkBufferImageCopy> copyRegions(layerCount);
    for (uint32_t i = 0; i < layerCount; ++i)
    {
        copyRegions[i] = {};
        copyRegions[i].bufferOffset = static_cast<VkDeviceSize>(i) * BYTES_PER_LAYER;
        copyRegions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1};
        copyRegions[i].imageExtent = {TEX_SIZE, TEX_SIZE, 1};
    }

    vkCmdCopyBufferToImage(cmd, stagingBuffer, ta->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

    // Step 5c: Generate mipmaps via blit chain
    uint32_t mipWidth = TEX_SIZE;
    uint32_t mipHeight = TEX_SIZE;

    for (uint32_t mip = 1; mip < BLOCK_TEXTURE_MIP_LEVELS; ++mip)
    {
        // Transition mip-1: TRANSFER_DST -> TRANSFER_SRC
        VkImageMemoryBarrier2 toSrcBarrier{};
        toSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toSrcBarrier.image = ta->m_image;
        toSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSrcBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toSrcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSrcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrcBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, layerCount};

        // Transition mip: UNDEFINED -> TRANSFER_DST (first time this mip is used)
        VkImageMemoryBarrier2 mipToDstBarrier{};
        mipToDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        mipToDstBarrier.image = ta->m_image;
        mipToDstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        mipToDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipToDstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        mipToDstBarrier.srcAccessMask = VK_ACCESS_2_NONE;
        mipToDstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        mipToDstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mipToDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mipToDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mipToDstBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layerCount};

        std::array<VkImageMemoryBarrier2, 2> mipBarriers = {toSrcBarrier, mipToDstBarrier};
        VkDependencyInfo mipDep{};
        mipDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        mipDep.imageMemoryBarrierCount = static_cast<uint32_t>(mipBarriers.size());
        mipDep.pImageMemoryBarriers = mipBarriers.data();
        vkCmdPipelineBarrier2(cmd, &mipDep);

        uint32_t nextWidth = std::max(mipWidth / 2, 1u);
        uint32_t nextHeight = std::max(mipHeight / 2, 1u);

        // Blit from mip-1 to mip (all layers at once)
        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, layerCount};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight), 1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layerCount};
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(nextWidth), static_cast<int32_t>(nextHeight), 1};

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = ta->m_image;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = ta->m_image;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;
        vkCmdBlitImage2(cmd, &blitInfo);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // Step 5d: Transition all mip levels to SHADER_READ_ONLY_OPTIMAL
    std::vector<VkImageMemoryBarrier2> finalBarriers(BLOCK_TEXTURE_MIP_LEVELS);
    for (uint32_t mip = 0; mip < BLOCK_TEXTURE_MIP_LEVELS; ++mip)
    {
        finalBarriers[mip] = {};
        finalBarriers[mip].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        finalBarriers[mip].image = ta->m_image;
        // Last mip is still TRANSFER_DST; others have been transitioned to TRANSFER_SRC
        finalBarriers[mip].oldLayout = (mip == BLOCK_TEXTURE_MIP_LEVELS - 1)
            ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        finalBarriers[mip].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        finalBarriers[mip].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        finalBarriers[mip].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        finalBarriers[mip].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        finalBarriers[mip].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        finalBarriers[mip].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarriers[mip].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        finalBarriers[mip].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layerCount};
    }

    VkDependencyInfo finalDep{};
    finalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    finalDep.imageMemoryBarrierCount = static_cast<uint32_t>(finalBarriers.size());
    finalDep.pImageMemoryBarriers = finalBarriers.data();
    vkCmdPipelineBarrier2(cmd, &finalDep);

    vkEndCommandBuffer(cmd);

    // Step 6: Submit and wait
    VkFence uploadFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fenceCI, nullptr, &uploadFence);

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    result = vkQueueSubmit2(context.getGraphicsQueue(), 1, &submitInfo, uploadFence);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: queue submit failed: {}", static_cast<int>(result));
        vkDestroyFence(device, uploadFence, nullptr);
        vkDestroyCommandPool(device, tempPool, nullptr);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: queue submit failed"));
    }

    vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);

    // Cleanup temporary resources
    vkDestroyFence(device, uploadFence, nullptr);
    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // Step 7: Create image view
    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = ta->m_image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewCI.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = BLOCK_TEXTURE_MIP_LEVELS;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = layerCount;

    result = vkCreateImageView(device, &viewCI, nullptr, &ta->m_imageView);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: image view creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: image view failed"));
    }

    // Step 8: Create sampler
    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter = VK_FILTER_NEAREST;                      // Pixel-art up close
    samplerCI.minFilter = VK_FILTER_LINEAR;                       // Smooth at distance
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;      // Tiling for greedy-meshed quads
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = static_cast<float>(BLOCK_TEXTURE_MIP_LEVELS - 1);
    samplerCI.anisotropyEnable = VK_FALSE;

    result = vkCreateSampler(device, &samplerCI, nullptr, &ta->m_sampler);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("TextureArray: sampler creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "TextureArray: sampler failed"));
    }

    // Step 9: Populate name-to-layer map
    for (uint32_t i = 0; i < layerCount; ++i)
    {
        ta->m_nameToLayer[names[i]] = static_cast<uint16_t>(i);
    }

    VX_LOG_INFO("TextureArray: created {} layers, {} mip levels, format R8G8B8A8_SRGB",
        layerCount, BLOCK_TEXTURE_MIP_LEVELS);

    return ta;
}

TextureArray::~TextureArray()
{
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_sampler, nullptr);
    }
    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_imageView, nullptr);
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
}

uint16_t TextureArray::getLayerIndex(const std::string& name) const
{
    auto it = m_nameToLayer.find(name);
    if (it != m_nameToLayer.end())
    {
        return it->second;
    }
    return 0; // fallback
}

} // namespace voxel::renderer
