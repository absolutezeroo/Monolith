#include "voxel/renderer/ChunkRenderInfoBuffer.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

#include <cstring>

namespace voxel::renderer
{

core::Result<std::unique_ptr<ChunkRenderInfoBuffer>> ChunkRenderInfoBuffer::create(
    VulkanContext& context,
    uint32_t maxSections)
{
    auto buf = std::unique_ptr<ChunkRenderInfoBuffer>(new ChunkRenderInfoBuffer());
    buf->m_allocator = context.getAllocator();
    buf->m_maxSections = maxSections;

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxSections) * sizeof(GpuChunkRenderInfo);

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = bufferSize;
    bufferCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    VkResult result =
        vmaCreateBuffer(buf->m_allocator, &bufferCI, &allocCI, &buf->m_buffer, &buf->m_allocation, &allocInfo);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("ChunkRenderInfoBuffer: buffer creation failed: {}", static_cast<int>(result));
        return std::unexpected(
            core::EngineError::vulkan(static_cast<int32_t>(result), "ChunkRenderInfoBuffer: buffer creation failed"));
    }

    buf->m_mappedData = static_cast<uint8_t*>(allocInfo.pMappedData);

    // Zero the entire buffer so all quadCounts start at 0
    std::memset(buf->m_mappedData, 0, bufferSize);

    // Initialize free-list with all indices in reverse order so allocateSlot() returns 0, 1, 2, ...
    buf->m_freeSlots.reserve(maxSections);
    for (uint32_t i = maxSections; i > 0; --i)
    {
        buf->m_freeSlots.push_back(i - 1);
    }

    VX_LOG_INFO(
        "ChunkRenderInfoBuffer created: {} slots, {:.1f} MB",
        maxSections,
        static_cast<double>(bufferSize) / (1024.0 * 1024.0));

    return buf;
}

core::Result<uint32_t> ChunkRenderInfoBuffer::allocateSlot()
{
    if (m_freeSlots.empty())
    {
        VX_LOG_ERROR("ChunkRenderInfoBuffer: no free slots available");
        return std::unexpected(core::EngineError{core::ErrorCode::OutOfMemory, "ChunkRenderInfoBuffer: no free slots"});
    }

    uint32_t slot = m_freeSlots.back();
    m_freeSlots.pop_back();
    return slot;
}

void ChunkRenderInfoBuffer::freeSlot(uint32_t slotIndex)
{
    // Zero the slot's quadCount to mark it as inactive for the compute shader
    auto* entry = reinterpret_cast<GpuChunkRenderInfo*>(m_mappedData + static_cast<size_t>(slotIndex) * sizeof(GpuChunkRenderInfo));
    entry->quadCount = 0;

    m_freeSlots.push_back(slotIndex);
}

void ChunkRenderInfoBuffer::update(uint32_t slotIndex, const GpuChunkRenderInfo& info)
{
    auto* dst = m_mappedData + static_cast<size_t>(slotIndex) * sizeof(GpuChunkRenderInfo);
    std::memcpy(dst, &info, sizeof(GpuChunkRenderInfo));
}

ChunkRenderInfoBuffer::~ChunkRenderInfoBuffer()
{
    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    VX_LOG_INFO("ChunkRenderInfoBuffer destroyed");
}

} // namespace voxel::renderer
