#include "voxel/renderer/QuadIndexBuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

#include <cstring>
#include <vector>

namespace voxel::renderer
{

core::Result<std::unique_ptr<QuadIndexBuffer>> QuadIndexBuffer::create(VulkanContext& context)
{
    auto qib = std::unique_ptr<QuadIndexBuffer>(new QuadIndexBuffer());
    qib->m_allocator = context.getAllocator();

    VkDevice device = context.getDevice();

    // Step 1: Generate index data CPU-side
    std::vector<uint32_t> indices(static_cast<size_t>(MAX_QUADS) * 6);
    for (uint32_t q = 0; q < MAX_QUADS; ++q)
    {
        uint32_t base = q * 4;
        size_t i = static_cast<size_t>(q) * 6;
        indices[i + 0] = base + 0;
        indices[i + 1] = base + 1;
        indices[i + 2] = base + 2;
        indices[i + 3] = base + 2;
        indices[i + 4] = base + 3;
        indices[i + 5] = base + 0;
    }

    VkDeviceSize dataSize = QUAD_INDEX_BUFFER_SIZE;

    // Step 2: Create DEVICE_LOCAL destination buffer
    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = dataSize;
    bufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(qib->m_allocator, &bufferCI, &allocCI, &qib->m_buffer, &qib->m_allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("QuadIndexBuffer: vmaCreateBuffer failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "QuadIndexBuffer: vmaCreateBuffer failed"));
    }

    // Step 3: Create temporary HOST_VISIBLE staging buffer
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocInfo{};

    VkBufferCreateInfo stagingCI{};
    stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size = dataSize;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI{};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    result = vmaCreateBuffer(qib->m_allocator, &stagingCI, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("QuadIndexBuffer: staging buffer creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(qib->m_allocator, qib->m_buffer, qib->m_allocation);
        qib->m_buffer = VK_NULL_HANDLE;
        qib->m_allocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "QuadIndexBuffer: staging buffer failed"));
    }

    std::memcpy(stagingAllocInfo.pMappedData, indices.data(), dataSize);

    // Step 4: One-time command buffer on graphics queue
    VkCommandPool tempPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolCI.queueFamilyIndex = context.getGraphicsQueueFamily();

    result = vkCreateCommandPool(device, &poolCI, nullptr, &tempPool);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("QuadIndexBuffer: command pool creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(qib->m_allocator, stagingBuffer, stagingAlloc);
        vmaDestroyBuffer(qib->m_allocator, qib->m_buffer, qib->m_allocation);
        qib->m_buffer = VK_NULL_HANDLE;
        qib->m_allocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "QuadIndexBuffer: command pool failed"));
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

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = dataSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, qib->m_buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    // Submit via sync2
    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    result = vkQueueSubmit2(context.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("QuadIndexBuffer: queue submit failed: {}", static_cast<int>(result));
        vkDestroyCommandPool(device, tempPool, nullptr);
        vmaDestroyBuffer(qib->m_allocator, stagingBuffer, stagingAlloc);
        vmaDestroyBuffer(qib->m_allocator, qib->m_buffer, qib->m_allocation);
        qib->m_buffer = VK_NULL_HANDLE;
        qib->m_allocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "QuadIndexBuffer: queue submit failed"));
    }

    vkQueueWaitIdle(context.getGraphicsQueue());

    // Step 5: Cleanup temporaries
    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyBuffer(qib->m_allocator, stagingBuffer, stagingAlloc);

    VX_LOG_INFO(
        "QuadIndexBuffer created: {} MB, {} quads, {} indices",
        dataSize / (1024 * 1024),
        MAX_QUADS,
        MAX_QUADS * 6);

    return qib;
}

void QuadIndexBuffer::bind(VkCommandBuffer cmd) const
{
    vkCmdBindIndexBuffer(cmd, m_buffer, 0, VK_INDEX_TYPE_UINT32);
}

QuadIndexBuffer::~QuadIndexBuffer()
{
    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    VX_LOG_INFO("QuadIndexBuffer destroyed");
}

} // namespace voxel::renderer
