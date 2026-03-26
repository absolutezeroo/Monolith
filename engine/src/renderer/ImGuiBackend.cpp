#include "voxel/renderer/ImGuiBackend.h"
#include "voxel/core/Log.h"
#include "voxel/renderer/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace voxel::renderer
{

core::Result<std::unique_ptr<ImGuiBackend>> ImGuiBackend::create(VulkanContext& context, GLFWwindow* window)
{
    auto backend = std::unique_ptr<ImGuiBackend>(new ImGuiBackend());
    backend->m_device = context.getDevice();

    // Descriptor pool for ImGui — generous limits, FREE_DESCRIPTOR_SET_BIT required
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}};

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets = 100;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(backend->m_device, &poolCI, nullptr, &backend->m_descriptorPool);
    if (result != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create ImGui descriptor pool: {}", static_cast<int>(result));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(result), "Failed to create ImGui descriptor pool"));
    }

    // ImGui context
    ImGui::CreateContext();

    // GLFW backend — install_callbacks=true chains with existing callbacks
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Load Vulkan functions for ImGui — required when VK_NO_PROTOTYPES is defined.
    // volk loads Vulkan dynamically, so ImGui needs a loader callback to resolve them.
    VkInstance instance = context.getInstance();
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
        [](const char* functionName, void* userData) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(userData), functionName);
        }, instance);

    // Vulkan backend — dynamic rendering, no VkRenderPass
    // ImGui 1.92+: MSAASamples and PipelineRenderingCreateInfo moved to PipelineInfoMain.
    VkFormat swapchainFormat = context.getSwapchainFormat();

    VkPipelineRenderingCreateInfoKHR renderingCI{};
    renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount = 1;
    renderingCI.pColorAttachmentFormats = &swapchainFormat;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = context.getInstance();
    initInfo.PhysicalDevice = context.getPhysicalDevice();
    initInfo.Device = context.getDevice();
    initInfo.QueueFamily = context.getGraphicsQueueFamily();
    initInfo.Queue = context.getGraphicsQueue();
    initInfo.DescriptorPool = backend->m_descriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(context.getSwapchainImages().size());
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingCI;

    ImGui_ImplVulkan_Init(&initInfo);

    ImGui::StyleColorsDark();

    VX_LOG_INFO("ImGui initialized (dynamic rendering, Vulkan 1.3)");
    return backend;
}

ImGuiBackend::~ImGuiBackend()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_descriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        }
    }
}

void ImGuiBackend::beginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::render(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace voxel::renderer
