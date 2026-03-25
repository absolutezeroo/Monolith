#pragma once

#include "voxel/core/Result.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace voxel::game
{
class Window;
}

namespace voxel::renderer
{

class VulkanContext
{
public:
    static core::Result<std::unique_ptr<VulkanContext>> create(game::Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] VkInstance getInstance() const { return m_instance; }
    [[nodiscard]] VkDevice getDevice() const { return m_device; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] VmaAllocator getAllocator() const { return m_allocator; }
    [[nodiscard]] VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    [[nodiscard]] VkQueue getTransferQueue() const { return m_transferQueue; }
    [[nodiscard]] uint32_t getTransferQueueFamily() const { return m_transferQueueFamily; }
    [[nodiscard]] VkSurfaceKHR getSurface() const { return m_surface; }
    [[nodiscard]] VkSwapchainKHR getSwapchain() const { return m_swapchain; }
    [[nodiscard]] VkFormat getSwapchainFormat() const { return m_swapchainFormat; }
    [[nodiscard]] VkExtent2D getSwapchainExtent() const { return m_swapchainExtent; }
    [[nodiscard]] const std::vector<VkImage>& getSwapchainImages() const { return m_swapchainImages; }
    [[nodiscard]] const std::vector<VkImageView>& getSwapchainImageViews() const { return m_swapchainImageViews; }

    /**
     * @brief Rebuilds the swapchain after a resize or out-of-date event.
     *
     * Waits for GPU idle, destroys old image views and swapchain, and creates new ones.
     * Handles minimized windows by blocking until the framebuffer is non-zero.
     */
    core::Result<void> recreateSwapchain(game::Window& window);

private:
    VulkanContext() = default;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    uint32_t m_transferQueueFamily = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent = {0, 0};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
};

} // namespace voxel::renderer
