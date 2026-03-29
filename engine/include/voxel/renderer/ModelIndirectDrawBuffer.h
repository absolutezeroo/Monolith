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
 * @brief GPU buffers for non-indexed indirect draw commands (VkDrawIndirectCommand).
 *
 * Same pattern as IndirectDrawBuffer but uses VkDrawIndirectCommand (16 bytes)
 * instead of VkDrawIndexedIndirectCommand (20 bytes). Used for model vertex rendering
 * which uses triangle list topology without an index buffer.
 */
class ModelIndirectDrawBuffer
{
  public:
    static core::Result<std::unique_ptr<ModelIndirectDrawBuffer>> create(VulkanContext& context, uint32_t maxCommands);

    ~ModelIndirectDrawBuffer();

    ModelIndirectDrawBuffer(const ModelIndirectDrawBuffer&) = delete;
    ModelIndirectDrawBuffer& operator=(const ModelIndirectDrawBuffer&) = delete;
    ModelIndirectDrawBuffer(ModelIndirectDrawBuffer&&) = delete;
    ModelIndirectDrawBuffer& operator=(ModelIndirectDrawBuffer&&) = delete;

    /// Records a vkCmdFillBuffer to reset the draw count to 0.
    void recordCountReset(VkCommandBuffer cmd) const;

    [[nodiscard]] VkBuffer getCommandBuffer() const { return m_commandBuffer; }
    [[nodiscard]] VkBuffer getCountBuffer() const { return m_countBuffer; }
    [[nodiscard]] uint32_t getMaxCommands() const { return m_maxCommands; }
    [[nodiscard]] VkDeviceSize getCommandBufferSize() const
    {
        return static_cast<VkDeviceSize>(m_maxCommands) * sizeof(VkDrawIndirectCommand);
    }

  private:
    ModelIndirectDrawBuffer() = default;

    VkBuffer m_commandBuffer = VK_NULL_HANDLE;
    VmaAllocation m_commandAllocation = VK_NULL_HANDLE;

    VkBuffer m_countBuffer = VK_NULL_HANDLE;
    VmaAllocation m_countAllocation = VK_NULL_HANDLE;

    uint32_t m_maxCommands = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup
};

} // namespace voxel::renderer
