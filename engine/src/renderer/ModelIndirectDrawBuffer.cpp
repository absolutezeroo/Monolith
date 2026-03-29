#include "voxel/renderer/ModelIndirectDrawBuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

namespace voxel::renderer
{

core::Result<std::unique_ptr<ModelIndirectDrawBuffer>> ModelIndirectDrawBuffer::create(
    VulkanContext& context,
    uint32_t maxCommands)
{
    auto buf = std::unique_ptr<ModelIndirectDrawBuffer>(new ModelIndirectDrawBuffer());
    buf->m_allocator = context.getAllocator();
    buf->m_maxCommands = maxCommands;

    constexpr VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkDeviceSize commandSize = static_cast<VkDeviceSize>(maxCommands) * sizeof(VkDrawIndirectCommand);

    VkBufferCreateInfo commandCI{};
    commandCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    commandCI.size = commandSize;
    commandCI.usage = usageFlags;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(
        buf->m_allocator, &commandCI, &allocCI, &buf->m_commandBuffer, &buf->m_commandAllocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("ModelIndirectDrawBuffer: command buffer creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "ModelIndirectDrawBuffer: command buffer failed"));
    }

    // Draw count buffer: 4 bytes
    VkBufferCreateInfo countCI{};
    countCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    countCI.size = sizeof(uint32_t);
    countCI.usage = usageFlags;

    result = vmaCreateBuffer(
        buf->m_allocator, &countCI, &allocCI, &buf->m_countBuffer, &buf->m_countAllocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("ModelIndirectDrawBuffer: count buffer creation failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(buf->m_allocator, buf->m_commandBuffer, buf->m_commandAllocation);
        buf->m_commandBuffer = VK_NULL_HANDLE;
        buf->m_commandAllocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "ModelIndirectDrawBuffer: count buffer failed"));
    }

    VX_LOG_INFO(
        "ModelIndirectDrawBuffer created: {} commands, {:.1f} KB command buffer + 4B count",
        maxCommands,
        static_cast<double>(commandSize) / 1024.0);

    return buf;
}

void ModelIndirectDrawBuffer::recordCountReset(VkCommandBuffer cmd) const
{
    vkCmdFillBuffer(cmd, m_countBuffer, 0, sizeof(uint32_t), 0);
}

ModelIndirectDrawBuffer::~ModelIndirectDrawBuffer()
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

    VX_LOG_INFO("ModelIndirectDrawBuffer destroyed");
}

} // namespace voxel::renderer
