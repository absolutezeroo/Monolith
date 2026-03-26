#pragma once

#include "voxel/core/Result.h"

#include <volk.h>

#include <memory>

struct GLFWwindow;

namespace voxel::renderer
{

class VulkanContext;

class ImGuiBackend
{
  public:
    static core::Result<std::unique_ptr<ImGuiBackend>> create(VulkanContext& context, GLFWwindow* window);
    ~ImGuiBackend();

    ImGuiBackend(const ImGuiBackend&) = delete;
    ImGuiBackend& operator=(const ImGuiBackend&) = delete;
    ImGuiBackend(ImGuiBackend&&) = delete;
    ImGuiBackend& operator=(ImGuiBackend&&) = delete;

    /// Call at the start of each frame before building ImGui windows.
    void beginFrame();

    /// Call inside the rendering pass to record ImGui draw commands.
    void render(VkCommandBuffer cmd);

  private:
    ImGuiBackend() = default;

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};

} // namespace voxel::renderer
