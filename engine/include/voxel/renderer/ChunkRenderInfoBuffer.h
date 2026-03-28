#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/ChunkRenderInfo.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace voxel::renderer
{

class VulkanContext;

/**
 * @brief HOST_VISIBLE SSBO holding GpuChunkRenderInfo[] for compute culling and vertex shader lookups.
 *
 * Uses a free-list for slot allocation. Slots may have gaps (freed but not compacted).
 * The compute shader (Story 6.4) iterates all slots and skips entries where quadCount == 0.
 * Persistently mapped for direct CPU writes without staging.
 */
class ChunkRenderInfoBuffer
{
  public:
    /**
     * @brief Creates the ChunkRenderInfo SSBO.
     *
     * @param context The Vulkan context providing device and allocator.
     * @param maxSections Maximum number of chunk sections the buffer can hold.
     * @return The created ChunkRenderInfoBuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<ChunkRenderInfoBuffer>> create(VulkanContext& context, uint32_t maxSections);

    ~ChunkRenderInfoBuffer();

    ChunkRenderInfoBuffer(const ChunkRenderInfoBuffer&) = delete;
    ChunkRenderInfoBuffer& operator=(const ChunkRenderInfoBuffer&) = delete;
    ChunkRenderInfoBuffer(ChunkRenderInfoBuffer&&) = delete;
    ChunkRenderInfoBuffer& operator=(ChunkRenderInfoBuffer&&) = delete;

    /// Allocates a slot from the free-list. Returns the slot index.
    [[nodiscard]] core::Result<uint32_t> allocateSlot();

    /// Frees a slot, returning it to the free-list and zeroing its quadCount.
    void freeSlot(uint32_t slotIndex);

    /// Updates the GPU data at the given slot index.
    void update(uint32_t slotIndex, const GpuChunkRenderInfo& info);

    /// Returns the number of active (allocated) slots.
    [[nodiscard]] uint32_t getActiveCount() const { return m_maxSections - static_cast<uint32_t>(m_freeSlots.size()); }

    [[nodiscard]] VkBuffer getBuffer() const { return m_buffer; }
    [[nodiscard]] VkDeviceSize getBufferSize() const
    {
        return static_cast<VkDeviceSize>(m_maxSections) * sizeof(GpuChunkRenderInfo);
    }

  private:
    ChunkRenderInfoBuffer() = default;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup

    uint8_t* m_mappedData = nullptr;
    uint32_t m_maxSections = 0;
    std::vector<uint32_t> m_freeSlots;
};

} // namespace voxel::renderer
