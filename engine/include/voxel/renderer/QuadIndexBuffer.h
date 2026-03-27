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
 * @brief Single shared index buffer for quad tessellation.
 *
 * Pre-generates the pattern {0,1,2, 2,3,0} repeated for MAX_QUADS quads,
 * uploaded once at init to a DEVICE_LOCAL VkBuffer. All chunk draws
 * reference this single index buffer via vkCmdBindIndexBuffer.
 */
class QuadIndexBuffer
{
  public:
    /**
     * @brief Creates and uploads the shared quad index buffer.
     *
     * Generates index data CPU-side, uploads via a one-time staging buffer,
     * and returns the GPU-resident index buffer.
     *
     * @param context The Vulkan context providing device, allocator, and queues.
     * @return The created QuadIndexBuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<QuadIndexBuffer>> create(VulkanContext& context);

    ~QuadIndexBuffer();

    QuadIndexBuffer(const QuadIndexBuffer&) = delete;
    QuadIndexBuffer& operator=(const QuadIndexBuffer&) = delete;
    QuadIndexBuffer(QuadIndexBuffer&&) = delete;
    QuadIndexBuffer& operator=(QuadIndexBuffer&&) = delete;

    /// Binds this index buffer to the command buffer (uint32 indices, offset 0).
    void bind(VkCommandBuffer cmd) const;

    [[nodiscard]] VkBuffer getBuffer() const { return m_buffer; }
    [[nodiscard]] uint32_t getMaxQuads() const { return MAX_QUADS; }
    [[nodiscard]] uint32_t getIndexCount() const { return MAX_QUADS * 6; }

  private:
    QuadIndexBuffer() = default;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup
};

} // namespace voxel::renderer
