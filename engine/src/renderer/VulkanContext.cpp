#include "voxel/renderer/VulkanContext.h"
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/Window.h"

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>

namespace voxel::renderer
{

namespace
{

void logGpuInfo(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    uint32_t apiMajor = VK_API_VERSION_MAJOR(properties.apiVersion);
    uint32_t apiMinor = VK_API_VERSION_MINOR(properties.apiVersion);
    uint32_t apiPatch = VK_API_VERSION_PATCH(properties.apiVersion);

    VX_LOG_INFO("GPU: {}", properties.deviceName);
    VX_LOG_INFO("Vulkan API version: {}.{}.{}", apiMajor, apiMinor, apiPatch);
}

void logQueueFamilies(uint32_t graphicsFamily, uint32_t transferFamily)
{
    if (graphicsFamily == transferFamily)
    {
        VX_LOG_INFO("Graphics queue family: {} (shared with transfer)", graphicsFamily);
    }
    else
    {
        VX_LOG_INFO("Graphics queue family: {}", graphicsFamily);
        VX_LOG_INFO("Transfer queue family: {} (dedicated)", transferFamily);
    }
}

void logMemoryHeaps(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
    {
        const auto& heap = memProps.memoryHeaps[i];
        bool isDeviceLocal = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        VX_LOG_INFO("Memory heap {}: {} MB {}",
            i,
            heap.size / (1024 * 1024),
            isDeviceLocal ? "(DEVICE_LOCAL)" : "(HOST)");
    }
}

} // anonymous namespace

core::Result<std::unique_ptr<VulkanContext>> VulkanContext::create(game::Window& window)
{
    auto ctx = std::unique_ptr<VulkanContext>(new VulkanContext());

    // Step 1: volkInitialize — load Vulkan loader function pointers
    VkResult volkResult = volkInitialize();
    if (volkResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("volkInitialize failed: {}", static_cast<int>(volkResult));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(volkResult), "volkInitialize failed"));
    }

    // Sanity check: GLFW must support Vulkan
    if (glfwVulkanSupported() == GLFW_FALSE)
    {
        VX_FATAL("GLFW reports no Vulkan support — cannot proceed");
    }

    // Step 2: vk-bootstrap Instance — validation layers in debug only
    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder
        .set_app_name("VoxelForge")
        .require_api_version(1, 3, 0);

#ifndef NDEBUG
    instanceBuilder
        .request_validation_layers()
        .use_default_debug_messenger();
#endif

    auto instanceResult = instanceBuilder.build();
    if (!instanceResult)
    {
        VX_LOG_ERROR("Failed to create Vulkan instance: {}", instanceResult.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to create Vulkan instance: " + instanceResult.error().message()});
    }

    vkb::Instance vkbInstance = instanceResult.value();
    ctx->m_instance = vkbInstance.instance;
    ctx->m_debugMessenger = vkbInstance.debug_messenger;

    // Step 3: volkLoadInstance — load instance-level function pointers
    volkLoadInstance(ctx->m_instance);

    // Step 4: Create window surface via GLFW
    VkResult surfaceResult = glfwCreateWindowSurface(
        ctx->m_instance,
        window.getHandle(),
        nullptr,
        &ctx->m_surface);

    if (surfaceResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create window surface: {}", static_cast<int>(surfaceResult));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(surfaceResult), "Failed to create window surface"));
    }

    // Step 5: Physical device selection — require Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;

    VkPhysicalDeviceFeatures features10{};
    features10.fillModeNonSolid = VK_TRUE; // Required for wireframe (VK_POLYGON_MODE_LINE)

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physResult = selector
        .set_surface(ctx->m_surface)
        .set_minimum_version(1, 3)
        .set_required_features(features10)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .select();

    if (!physResult)
    {
        VX_LOG_ERROR("Failed to select physical device: {}", physResult.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to select physical device: " + physResult.error().message()});
    }

    vkb::PhysicalDevice vkbPhysicalDevice = physResult.value();
    ctx->m_physicalDevice = vkbPhysicalDevice.physical_device;

    // Step 6: Logical device + queues
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    auto deviceResult = deviceBuilder.build();
    if (!deviceResult)
    {
        VX_LOG_ERROR("Failed to create logical device: {}", deviceResult.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to create logical device: " + deviceResult.error().message()});
    }

    vkb::Device vkbDevice = deviceResult.value();
    ctx->m_device = vkbDevice.device;

    // Step 7: volkLoadDevice — load device-level function pointers
    volkLoadDevice(ctx->m_device);

    // Get graphics queue (required)
    auto graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto graphicsIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    if (!graphicsQueue.has_value() || !graphicsIndex.has_value())
    {
        VX_LOG_ERROR("Failed to get graphics queue");
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to get graphics queue"});
    }

    ctx->m_graphicsQueue = graphicsQueue.value();
    ctx->m_graphicsQueueFamily = graphicsIndex.value();

    // Try dedicated transfer queue; fallback to graphics
    auto transferQueue = vkbDevice.get_dedicated_queue(vkb::QueueType::transfer);
    auto transferIndex = vkbDevice.get_dedicated_queue_index(vkb::QueueType::transfer);

    if (transferQueue.has_value() && transferIndex.has_value())
    {
        ctx->m_transferQueue = transferQueue.value();
        ctx->m_transferQueueFamily = transferIndex.value();
    }
    else
    {
        ctx->m_transferQueue = ctx->m_graphicsQueue;
        ctx->m_transferQueueFamily = ctx->m_graphicsQueueFamily;
        VX_LOG_WARN("No dedicated transfer queue — using graphics queue");
    }

    // Step 8: VMA allocator with volk function import
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = ctx->m_instance;
    allocatorInfo.physicalDevice = ctx->m_physicalDevice;
    allocatorInfo.device = ctx->m_device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaVulkanFunctions vulkanFunctions{};
    VkResult importResult = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
    if (importResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to import Vulkan functions from volk for VMA: {}", static_cast<int>(importResult));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(importResult), "Failed to import Vulkan functions from volk for VMA"));
    }
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult vmaResult = vmaCreateAllocator(&allocatorInfo, &ctx->m_allocator);
    if (vmaResult != VK_SUCCESS)
    {
        VX_LOG_ERROR("Failed to create VMA allocator: {}", static_cast<int>(vmaResult));
        return std::unexpected(core::EngineError::vulkan(static_cast<int32_t>(vmaResult), "Failed to create VMA allocator"));
    }

    // Step 9: Swapchain via vk-bootstrap
    int fbWidth = 0;
    int fbHeight = 0;
    window.getFramebufferSize(fbWidth, fbHeight);

    vkb::SwapchainBuilder swapchainBuilder{ctx->m_physicalDevice, ctx->m_device, ctx->m_surface};
    auto swapResult = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight))
        .build();

    if (!swapResult)
    {
        VX_LOG_ERROR("Failed to create swapchain: {}", swapResult.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to create swapchain: " + swapResult.error().message()});
    }

    vkb::Swapchain vkbSwapchain = swapResult.value();
    ctx->m_swapchain = vkbSwapchain.swapchain;
    ctx->m_swapchainFormat = vkbSwapchain.image_format;
    ctx->m_swapchainExtent = vkbSwapchain.extent;

    auto swapImages = vkbSwapchain.get_images();
    if (!swapImages)
    {
        VX_LOG_ERROR("Failed to get swapchain images: {}", swapImages.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to get swapchain images: " + swapImages.error().message()});
    }
    ctx->m_swapchainImages = swapImages.value();

    auto swapImageViews = vkbSwapchain.get_image_views();
    if (!swapImageViews)
    {
        VX_LOG_ERROR("Failed to get swapchain image views: {}", swapImageViews.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to get swapchain image views: " + swapImageViews.error().message()});
    }
    ctx->m_swapchainImageViews = swapImageViews.value();

    // Log GPU info
    logGpuInfo(ctx->m_physicalDevice);
    logQueueFamilies(ctx->m_graphicsQueueFamily, ctx->m_transferQueueFamily);
    logMemoryHeaps(ctx->m_physicalDevice);

    VX_LOG_INFO("Vulkan initialization complete — swapchain {}x{}, {} images",
        ctx->m_swapchainExtent.width,
        ctx->m_swapchainExtent.height,
        ctx->m_swapchainImages.size());

    return ctx;
}

core::Result<void> VulkanContext::recreateSwapchain(game::Window& window)
{
    vkDeviceWaitIdle(m_device);

    // Handle minimized window: wait until framebuffer is non-zero
    int fbWidth = 0;
    int fbHeight = 0;
    window.getFramebufferSize(fbWidth, fbHeight);
    while (fbWidth == 0 || fbHeight == 0)
    {
        glfwWaitEvents();
        window.getFramebufferSize(fbWidth, fbHeight);
    }

    // Destroy old image views
    for (auto view : m_swapchainImageViews)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_swapchainImageViews.clear();

    // Rebuild swapchain via vk-bootstrap, passing old swapchain for reuse
    vkb::SwapchainBuilder builder{m_physicalDevice, m_device, m_surface};
    auto result = builder
        .set_old_swapchain(m_swapchain)
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight))
        .build();

    // Destroy old swapchain AFTER building new one (builder may reuse internal resources)
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

    if (!result)
    {
        VX_LOG_ERROR("Failed to recreate swapchain: {}", result.error().message());
        m_swapchain = VK_NULL_HANDLE;
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to recreate swapchain: " + result.error().message()});
    }

    vkb::Swapchain vkbSwapchain = result.value();
    m_swapchain = vkbSwapchain.swapchain;
    m_swapchainFormat = vkbSwapchain.image_format;
    m_swapchainExtent = vkbSwapchain.extent;

    auto swapImages = vkbSwapchain.get_images();
    if (!swapImages)
    {
        VX_LOG_ERROR("Failed to get new swapchain images: {}", swapImages.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to get new swapchain images: " + swapImages.error().message()});
    }
    m_swapchainImages = swapImages.value();

    auto swapImageViews = vkbSwapchain.get_image_views();
    if (!swapImageViews)
    {
        VX_LOG_ERROR("Failed to get new swapchain image views: {}", swapImageViews.error().message());
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to get new swapchain image views: " + swapImageViews.error().message()});
    }
    m_swapchainImageViews = swapImageViews.value();

    VX_LOG_INFO("Swapchain recreated — {}x{}, {} images",
        m_swapchainExtent.width,
        m_swapchainExtent.height,
        m_swapchainImages.size());

    return {};
}

VulkanContext::~VulkanContext()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);

        for (auto view : m_swapchainImageViews)
        {
            vkDestroyImageView(m_device, view, nullptr);
        }
        m_swapchainImageViews.clear();

        if (m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }

        if (m_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE)
    {
        if (m_surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_debugMessenger != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }

        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

} // namespace voxel::renderer
