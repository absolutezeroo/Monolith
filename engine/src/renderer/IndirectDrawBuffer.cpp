#include "voxel/renderer/IndirectDrawBuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

namespace voxel::renderer
{

core::Result<std::unique_ptr<IndirectDrawBuffer>> IndirectDrawBuffer::create(
    VulkanContext& context,
    uint32_t maxCommands)
{
    auto idb = std::unique_ptr<IndirectDrawBuffer>(new IndirectDrawBuffer());
    idb->m_allocator = context.getAllocator();
    idb->m_maxCommands = maxCommands;

    constexpr VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkDeviceSize commandSize = static_cast<VkDeviceSize>(maxCommands) * sizeof(VkDrawIndexedIndirectCommand);

    VkBufferCreateInfo commandCI{};
    commandCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    commandCI.size = commandSize;
    commandCI.usage = usageFlags;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(
        idb->m_allocator, &commandCI, &allocCI, &idb->m_commandBuffer, &idb->m_commandAllocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("IndirectDrawBuffer: command buffer creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "IndirectDrawBuffer: command buffer failed"));
    }

    // Draw count buffer: 4 bytes
    VkBufferCreateInfo countCI{};
    countCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    countCI.size = sizeof(uint32_t);
    countCI.usage = usageFlags;

    result = vmaCreateBuffer(
        idb->m_allocator, &countCI, &allocCI, &idb->m_countBuffer, &idb->m_countAllocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("IndirectDrawBuffer: count buffer creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(idb->m_allocator, idb->m_commandBuffer, idb->m_commandAllocation);
        idb->m_commandBuffer = VK_NULL_HANDLE;
        idb->m_commandAllocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "IndirectDrawBuffer: count buffer failed"));
    }

    VX_LOG_INFO(
        "IndirectDrawBuffer created: {} commands, {:.1f} KB command buffer + 4B count",
        maxCommands,
        static_cast<double>(commandSize) / 1024.0);

    return idb;
}

void IndirectDrawBuffer::recordCountReset(VkCommandBuffer cmd) const
{
    vkCmdFillBuffer(cmd, m_countBuffer, 0, sizeof(uint32_t), 0);
}

IndirectDrawBuffer::~IndirectDrawBuffer()
{
    if (m_countBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_countBuffer, m_countAllocation);
        m_countBuffer = VK_NULL_HANDLE;
        m_countAllocation = VK_NULL_HANDLE;
    }

    if (m_commandBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_commandBuffer, m_commandAllocation);
        m_commandBuffer = VK_NULL_HANDLE;
        m_commandAllocation = VK_NULL_HANDLE;
    }

    VX_LOG_INFO("IndirectDrawBuffer destroyed");
}

} // namespace voxel::renderer
