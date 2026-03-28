#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <memory>

namespace voxel::renderer
{

class VulkanContext;

/**
 * @brief GPU buffers for indirect draw commands and draw count.
 *
 * Owns two VkBuffers:
 *   - Command array: VkDrawIndexedIndirectCommand[] (INDIRECT + STORAGE + TRANSFER_DST)
 *   - Draw count: uint32_t (INDIRECT + STORAGE + TRANSFER_DST)
 *
 * The compute culling shader (Story 6.4) writes commands and atomically increments
 * the count. The draw count is reset to 0 each frame via vkCmdFillBuffer.
 */
class IndirectDrawBuffer
{
  public:
    /**
     * @brief Creates the indirect draw buffer pair.
     *
     * @param context The Vulkan context providing device and allocator.
     * @param maxCommands Maximum number of draw commands the buffer can hold.
     * @return The created IndirectDrawBuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<IndirectDrawBuffer>> create(VulkanContext& context, uint32_t maxCommands);

    ~IndirectDrawBuffer();

    IndirectDrawBuffer(const IndirectDrawBuffer&) = delete;
    IndirectDrawBuffer& operator=(const IndirectDrawBuffer&) = delete;
    IndirectDrawBuffer(IndirectDrawBuffer&&) = delete;
    IndirectDrawBuffer& operator=(IndirectDrawBuffer&&) = delete;

    /// Records a vkCmdFillBuffer to reset the draw count to 0.
    void recordCountReset(VkCommandBuffer cmd) const;

    [[nodiscard]] VkBuffer getCommandBuffer() const { return m_commandBuffer; }
    [[nodiscard]] VkBuffer getCountBuffer() const { return m_countBuffer; }
    [[nodiscard]] uint32_t getMaxCommands() const { return m_maxCommands; }
    [[nodiscard]] VkDeviceSize getCommandBufferSize() const
    {
        return static_cast<VkDeviceSize>(m_maxCommands) * 20; // sizeof(VkDrawIndexedIndirectCommand)
    }

  private:
    IndirectDrawBuffer() = default;

    VkBuffer m_commandBuffer = VK_NULL_HANDLE;
    VmaAllocation m_commandAllocation = VK_NULL_HANDLE;

    VkBuffer m_countBuffer = VK_NULL_HANDLE;
    VmaAllocation m_countAllocation = VK_NULL_HANDLE;

    uint32_t m_maxCommands = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup
};

} // namespace voxel::renderer
