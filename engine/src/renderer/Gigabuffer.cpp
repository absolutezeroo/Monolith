#include "voxel/renderer/Gigabuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

namespace voxel::renderer
{

core::Result<std::unique_ptr<Gigabuffer>> Gigabuffer::create(VulkanContext& context, VkDeviceSize size)
{
    auto gb = std::unique_ptr<Gigabuffer>(new Gigabuffer());
    gb->m_capacity = size;
    gb->m_allocator = context.getAllocator();

    // Step 1: Create GPU buffer via VMA
    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = size;
    bufferCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(gb->m_allocator, &bufferCI, &allocCI, &gb->m_buffer, &gb->m_allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Gigabuffer: vmaCreateBuffer failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Gigabuffer: vmaCreateBuffer failed"));
    }

    // Step 2: Get buffer device address
    VkBufferDeviceAddressInfo bdaInfo{};
    bdaInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bdaInfo.buffer = gb->m_buffer;
    gb->m_bufferAddress = vkGetBufferDeviceAddress(context.getDevice(), &bdaInfo);

    // Step 3: Create VmaVirtualBlock for CPU-side sub-allocation tracking
    VmaVirtualBlockCreateInfo vbCI{};
    vbCI.size = size;
    vbCI.flags = 0; // default TLSF algorithm

    result = vmaCreateVirtualBlock(&vbCI, &gb->m_virtualBlock);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Gigabuffer: vmaCreateVirtualBlock failed: {}", static_cast<int>(result));
        vmaDestroyBuffer(gb->m_allocator, gb->m_buffer, gb->m_allocation);
        gb->m_buffer = VK_NULL_HANDLE;
        gb->m_allocation = VK_NULL_HANDLE;
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "Gigabuffer: vmaCreateVirtualBlock failed"));
    }

    VX_LOG_INFO("Gigabuffer created: {} MB, BDA: 0x{:X}", size / (1024 * 1024), gb->m_bufferAddress);

    return gb;
}

core::Result<GigabufferAllocation> Gigabuffer::allocate(VkDeviceSize size, VkDeviceSize alignment)
{
    VmaVirtualAllocationCreateInfo vaCI{};
    vaCI.size = size;
    vaCI.alignment = alignment;
    vaCI.flags = 0;

    VmaVirtualAllocation allocation;
    VkDeviceSize offset = 0;

    VkResult result = vmaVirtualAllocate(m_virtualBlock, &vaCI, &allocation, &offset);
    if (result != VK_SUCCESS)
    {
        VX_LOG_DEBUG("Gigabuffer: allocation failed for {} bytes (alignment {})", size, alignment);
        return std::unexpected(core::EngineError{core::ErrorCode::OutOfMemory, "Gigabuffer: allocation failed"});
    }

    // VX_LOG_DEBUG("Gigabuffer: allocated {} bytes at offset {}, total used: {}", size, offset, usedBytes());

    return GigabufferAllocation{offset, size, allocation};
}

void Gigabuffer::free(const GigabufferAllocation& allocation)
{
    vmaVirtualFree(m_virtualBlock, allocation.handle);
    VX_LOG_DEBUG(
        "Gigabuffer: freed {} bytes at offset {}, total used: {}", allocation.size, allocation.offset, usedBytes());
}

VkDeviceSize Gigabuffer::usedBytes() const
{
    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(m_virtualBlock, &stats);
    return stats.allocationBytes;
}

VkDeviceSize Gigabuffer::freeBytes() const
{
    return m_capacity - usedBytes();
}

uint32_t Gigabuffer::allocationCount() const
{
    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(m_virtualBlock, &stats);
    return stats.allocationCount;
}

Gigabuffer::~Gigabuffer()
{
    if (m_virtualBlock != VK_NULL_HANDLE)
    {
        vmaClearVirtualBlock(m_virtualBlock);
        vmaDestroyVirtualBlock(m_virtualBlock);
        m_virtualBlock = VK_NULL_HANDLE;
    }

    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    VX_LOG_INFO("Gigabuffer destroyed");
}

} // namespace voxel::renderer
