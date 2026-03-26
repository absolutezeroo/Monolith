#pragma once

#include "voxel/core/Result.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <memory>

namespace voxel::renderer
{

class VulkanContext;

/// Sub-allocation handle returned by Gigabuffer::allocate().
struct GigabufferAllocation
{
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    VmaVirtualAllocation handle = {};
};

/**
 * @brief Single large GPU buffer with CPU-side sub-allocation via VmaVirtualBlock.
 *
 * All chunk meshes live in one buffer for indirect rendering.
 * The VmaVirtualBlock tracks free/used regions without GPU involvement.
 */
class Gigabuffer
{
  public:
    static constexpr VkDeviceSize DEFAULT_SIZE = 256 * 1024 * 1024; // 256 MB

    /**
     * @brief Creates a Gigabuffer backed by a GPU VkBuffer and a CPU-side VmaVirtualBlock.
     *
     * @param context The Vulkan context providing device and allocator.
     * @param size Buffer capacity in bytes (default 256 MB).
     * @return The created Gigabuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<Gigabuffer>> create(VulkanContext& context, VkDeviceSize size = DEFAULT_SIZE);

    ~Gigabuffer();

    Gigabuffer(const Gigabuffer&) = delete;
    Gigabuffer& operator=(const Gigabuffer&) = delete;
    Gigabuffer(Gigabuffer&&) = delete;
    Gigabuffer& operator=(Gigabuffer&&) = delete;

    /**
     * @brief Allocates a sub-region from the gigabuffer.
     *
     * @param size Number of bytes to allocate.
     * @param alignment Required alignment in bytes (default 16 for vec4).
     * @return Allocation with offset and handle, or OutOfMemory error.
     */
    [[nodiscard]] core::Result<GigabufferAllocation> allocate(VkDeviceSize size, VkDeviceSize alignment = 16);

    /// Frees a previously allocated sub-region.
    void free(const GigabufferAllocation& allocation);

    [[nodiscard]] VkDeviceSize usedBytes() const;
    [[nodiscard]] VkDeviceSize freeBytes() const;
    [[nodiscard]] uint32_t allocationCount() const;

    [[nodiscard]] VkBuffer getBuffer() const { return m_buffer; }
    [[nodiscard]] VkDeviceAddress getBufferAddress() const { return m_bufferAddress; }
    [[nodiscard]] VkDeviceSize getCapacity() const { return m_capacity; }

  private:
    Gigabuffer() = default;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaVirtualBlock m_virtualBlock = VK_NULL_HANDLE;
    VkDeviceAddress m_bufferAddress = 0;
    VkDeviceSize m_capacity = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE; // non-owning, for cleanup
};

} // namespace voxel::renderer
