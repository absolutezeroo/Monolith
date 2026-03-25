#pragma once

#include "voxel/core/Result.h"
#include "voxel/renderer/RendererConstants.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace voxel::renderer
{

class VulkanContext;

/// A pending CPU→GPU copy recorded during the current frame.
struct PendingTransfer
{
    VkDeviceSize srcOffset = 0;
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
};

/**
 * @brief HOST_VISIBLE staging buffer with ring-buffer allocation for CPU→GPU uploads.
 *
 * Batches all uploadToGigabuffer() calls within a frame into a single transfer
 * command buffer, submitted via the transfer queue (dedicated or graphics fallback).
 * Uses per-frame fence synchronization and semaphore signaling for graphics queue sync.
 */
class StagingBuffer
{
public:
    static constexpr VkDeviceSize DEFAULT_STAGING_SIZE = 16 * 1024 * 1024; // 16 MB
    static constexpr uint32_t DEFAULT_MAX_UPLOADS = 8;
    static constexpr VkDeviceSize ALIGNMENT = 16;

    /**
     * @brief Creates a staging buffer with persistent mapping and transfer command resources.
     *
     * @param context The Vulkan context providing device, allocator, and transfer queue.
     * @param capacity Total staging buffer capacity in bytes (default 16 MB).
     * @return The created StagingBuffer, or an error on failure.
     */
    static core::Result<std::unique_ptr<StagingBuffer>> create(
        VulkanContext& context,
        VkDeviceSize capacity = DEFAULT_STAGING_SIZE);

    ~StagingBuffer();

    StagingBuffer(const StagingBuffer&) = delete;
    StagingBuffer& operator=(const StagingBuffer&) = delete;
    StagingBuffer(StagingBuffer&&) = delete;
    StagingBuffer& operator=(StagingBuffer&&) = delete;

    /**
     * @brief Copies data into the staging buffer and enqueues a transfer to the gigabuffer.
     *
     * @param data Source data pointer (must not be null).
     * @param size Number of bytes to upload (must be > 0).
     * @param dstOffset Destination offset in the gigabuffer.
     * @return Success, or InvalidArgument/OutOfMemory on failure.
     */
    [[nodiscard]] core::Result<void> uploadToGigabuffer(
        const void* data,
        size_t size,
        VkDeviceSize dstOffset);

    /**
     * @brief Records and submits all pending transfers as a single vkCmdCopyBuffer.
     *
     * @param gigabuffer The destination GPU buffer handle.
     * @return Success, or VulkanError on submission failure.
     */
    [[nodiscard]] core::Result<void> flushTransfers(VkBuffer gigabuffer);

    /**
     * @brief Advances to the next frame's ring-buffer region.
     *
     * Waits on the per-frame transfer fence, resets write offset and pending state.
     *
     * @param frameIndex Current frame index (0 or 1 for double-buffered).
     */
    void beginFrame(uint32_t frameIndex);

    [[nodiscard]] VkDeviceSize usedBytes() const { return m_usedBytes; }
    [[nodiscard]] VkDeviceSize freeBytes() const { return m_frameRegionSize - m_usedBytes; }
    [[nodiscard]] uint32_t pendingTransferCount() const { return static_cast<uint32_t>(m_pendingTransfers.size()); }
    [[nodiscard]] bool hasActiveTransfer() const { return m_hasActiveTransfer; }
    [[nodiscard]] VkSemaphore getTransferSemaphore() const { return m_transferSemaphore; }

    void setMaxUploadsPerFrame(uint32_t max) { m_maxUploadsPerFrame = max; }
    [[nodiscard]] uint32_t getMaxUploadsPerFrame() const { return m_maxUploadsPerFrame; }

private:
    StagingBuffer() = default;

    VulkanContext* m_context = nullptr;

    // VMA-backed staging buffer
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    void* m_mappedData = nullptr;
    VkDeviceSize m_capacity = 0;
    VkDeviceSize m_frameRegionSize = 0;

    // Ring-buffer state
    VkDeviceSize m_writeOffset = 0;
    VkDeviceSize m_usedBytes = 0;
    uint32_t m_currentFrameIndex = 0;

    // Transfer command resources
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FRAMES_IN_FLIGHT> m_transferCmdBuffers{};
    std::array<VkFence, FRAMES_IN_FLIGHT> m_transferFences{};
    std::array<bool, FRAMES_IN_FLIGHT> m_fenceSubmitted{}; // tracks whether fence was signaled by a queue submit
    VkSemaphore m_transferSemaphore = VK_NULL_HANDLE;

    // Per-frame upload tracking
    std::vector<PendingTransfer> m_pendingTransfers;
    uint32_t m_maxUploadsPerFrame = DEFAULT_MAX_UPLOADS;
    bool m_hasActiveTransfer = false;
};

} // namespace voxel::renderer
