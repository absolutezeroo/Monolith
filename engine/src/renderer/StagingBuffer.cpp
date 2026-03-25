#include "voxel/renderer/StagingBuffer.h"
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

#include <cstring>

namespace voxel::renderer
{

core::Result<std::unique_ptr<StagingBuffer>> StagingBuffer::create(
    VulkanContext& context,
    VkDeviceSize capacity)
{
    auto staging = std::unique_ptr<StagingBuffer>(new StagingBuffer());
    staging->m_context = &context;
    staging->m_capacity = capacity;
    staging->m_frameRegionSize = capacity / FRAMES_IN_FLIGHT;

    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    // Create HOST_VISIBLE staging buffer with persistent mapping
    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = capacity;
    bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    VkResult result = vmaCreateBuffer(
        allocator,
        &bufferCI,
        &allocCI,
        &staging->m_buffer,
        &staging->m_allocation,
        &allocInfo);

    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create staging buffer: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::VulkanError);
    }

    staging->m_mappedData = allocInfo.pMappedData;
    VX_ASSERT(staging->m_mappedData != nullptr, "Staging buffer must be persistently mapped");

    // Create transfer command pool
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCI.queueFamilyIndex = context.getTransferQueueFamily();

    result = vkCreateCommandPool(device, &poolCI, nullptr, &staging->m_transferCommandPool);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create transfer command pool: {}", static_cast<int>(result));
        vmaDestroyBuffer(allocator, staging->m_buffer, staging->m_allocation);
        return std::unexpected(core::EngineError::VulkanError);
    }

    // Allocate FRAMES_IN_FLIGHT transfer command buffers
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = staging->m_transferCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = FRAMES_IN_FLIGHT;

    result = vkAllocateCommandBuffers(device, &cmdAllocInfo, staging->m_transferCmdBuffers.data());
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to allocate transfer command buffers: {}", static_cast<int>(result));
        vkDestroyCommandPool(device, staging->m_transferCommandPool, nullptr);
        vmaDestroyBuffer(allocator, staging->m_buffer, staging->m_allocation);
        return std::unexpected(core::EngineError::VulkanError);
    }

    // Create per-frame transfer fences (signaled initially)
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
        result = vkCreateFence(device, &fenceCI, nullptr, &staging->m_transferFences[i]);
        if (result != VK_SUCCESS)
        {
            VX_LOG_ERROR("Failed to create transfer fence {}: {}", i, static_cast<int>(result));
            // Cleanup already created fences
            for (uint32_t j = 0; j < i; ++j)
            {
                vkDestroyFence(device, staging->m_transferFences[j], nullptr);
            }
            vkDestroyCommandPool(device, staging->m_transferCommandPool, nullptr);
            vmaDestroyBuffer(allocator, staging->m_buffer, staging->m_allocation);
            return std::unexpected(core::EngineError::VulkanError);
        }
    }

    // Create transfer-complete semaphore
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    result = vkCreateSemaphore(device, &semCI, nullptr, &staging->m_transferSemaphore);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create transfer semaphore: {}", static_cast<int>(result));
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroyFence(device, staging->m_transferFences[i], nullptr);
        }
        vkDestroyCommandPool(device, staging->m_transferCommandPool, nullptr);
        vmaDestroyBuffer(allocator, staging->m_buffer, staging->m_allocation);
        return std::unexpected(core::EngineError::VulkanError);
    }

    VX_LOG_INFO("StagingBuffer created — {} MB, {} frames, transfer queue family {}{}",
        capacity / (1024 * 1024),
        FRAMES_IN_FLIGHT,
        context.getTransferQueueFamily(),
        context.hasDedicatedTransferQueue() ? " (dedicated)" : " (shared with graphics)");

    return staging;
}

StagingBuffer::~StagingBuffer()
{
    if (m_context == nullptr)
    {
        return;
    }

    VkDevice device = m_context->getDevice();
    VmaAllocator allocator = m_context->getAllocator();

    vkDeviceWaitIdle(device);

    if (m_transferSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, m_transferSemaphore, nullptr);
    }

    for (auto& fence : m_transferFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, fence, nullptr);
        }
    }

    if (m_transferCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, m_transferCommandPool, nullptr);
    }

    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, m_buffer, m_allocation);
    }

    VX_LOG_DEBUG("StagingBuffer destroyed");
}

core::Result<void> StagingBuffer::uploadToGigabuffer(
    const void* data,
    size_t size,
    VkDeviceSize dstOffset)
{
    // Validate inputs
    if (data == nullptr || size == 0)
    {
        VX_LOG_ERROR("uploadToGigabuffer: invalid arguments (data={}, size={})",
            data != nullptr ? "valid" : "null", size);
        return std::unexpected(core::EngineError::InvalidArgument);
    }

    // Rate limiting
    if (m_pendingTransfers.size() >= m_maxUploadsPerFrame)
    {
        VX_LOG_WARN("uploadToGigabuffer: rate limited ({}/{} uploads this frame), deferring",
            m_pendingTransfers.size(), m_maxUploadsPerFrame);
        return {};
    }

    // Align size up to ALIGNMENT boundary
    VkDeviceSize alignedSize = (static_cast<VkDeviceSize>(size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    // Check ring-buffer has space in current frame's region
    if (m_usedBytes + alignedSize > m_frameRegionSize)
    {
        VX_LOG_ERROR("uploadToGigabuffer: out of staging memory (need {} bytes, {} available)",
            alignedSize, m_frameRegionSize - m_usedBytes);
        return std::unexpected(core::EngineError::OutOfMemory);
    }

    // Copy data into staging buffer
    auto* dst = static_cast<uint8_t*>(m_mappedData) + m_writeOffset;
    std::memcpy(dst, data, size);

    // Record pending transfer
    m_pendingTransfers.push_back(PendingTransfer{
        .srcOffset = m_writeOffset,
        .dstOffset = dstOffset,
        .size = static_cast<VkDeviceSize>(size)
    });

    // Advance write offset (aligned)
    m_writeOffset += alignedSize;
    m_usedBytes += alignedSize;

    return {};
}

core::Result<void> StagingBuffer::flushTransfers(VkBuffer gigabuffer)
{
    // No-op if nothing to transfer
    if (m_pendingTransfers.empty())
    {
        return {};
    }

    VkDevice device = m_context->getDevice();
    VkCommandBuffer cmd = m_transferCmdBuffers[m_currentFrameIndex];

    // Reset and begin command buffer
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(cmd, &beginInfo);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to begin transfer command buffer: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::VulkanError);
    }

    // Build copy regions from all pending transfers
    std::vector<VkBufferCopy> regions;
    regions.reserve(m_pendingTransfers.size());

    for (const auto& transfer : m_pendingTransfers)
    {
        VkBufferCopy region{};
        region.srcOffset = transfer.srcOffset;
        region.dstOffset = transfer.dstOffset;
        region.size = transfer.size;
        regions.push_back(region);
    }

    // Single vkCmdCopyBuffer with all regions
    vkCmdCopyBuffer(cmd, m_buffer, gigabuffer, static_cast<uint32_t>(regions.size()), regions.data());

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to end transfer command buffer: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::VulkanError);
    }

    // Submit via vkQueueSubmit2 — signal transfer semaphore
    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_transferSemaphore;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    result = vkQueueSubmit2(
        m_context->getTransferQueue(),
        1,
        &submitInfo,
        m_transferFences[m_currentFrameIndex]);

    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to submit transfer commands: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::VulkanError);
    }

    m_hasActiveTransfer = true;

    VX_LOG_DEBUG("Flushed {} transfers ({} bytes) via transfer queue",
        m_pendingTransfers.size(), m_usedBytes);

    m_pendingTransfers.clear();

    return {};
}

void StagingBuffer::beginFrame(uint32_t frameIndex)
{
    VkDevice device = m_context->getDevice();
    m_currentFrameIndex = frameIndex;

    // Wait on per-frame transfer fence — ensures previous transfers using this slot are complete
    vkWaitForFences(device, 1, &m_transferFences[frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &m_transferFences[frameIndex]);

    // Reset write offset to this frame's ring-buffer region
    m_writeOffset = static_cast<VkDeviceSize>(frameIndex) * m_frameRegionSize;
    m_usedBytes = 0;
    m_pendingTransfers.clear();
    m_hasActiveTransfer = false;
}

} // namespace voxel::renderer
