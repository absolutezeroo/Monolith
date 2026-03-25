#include "GameApp.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/VulkanContext.h"

int main()
{
    voxel::core::Log::init();

    auto windowResult = voxel::game::Window::create(1280, 720, "VoxelForge");
    if (!windowResult.has_value())
    {
        VX_FATAL("Failed to create window");
    }

    auto& window = *windowResult.value();

    auto vulkanResult = voxel::renderer::VulkanContext::create(window);
    if (!vulkanResult.has_value())
    {
        VX_FATAL("Failed to initialize Vulkan");
    }

    auto& vulkanContext = *vulkanResult.value();

    // Scope block ensures GameApp (Renderer, StagingBuffer) is destroyed
    // before VulkanContext — VMA allocations must be freed before VMA itself.
    {
        GameApp app(window, vulkanContext);

        auto initResult = app.init(VX_SHADER_DIR);
        if (!initResult.has_value())
        {
            VX_FATAL("Failed to initialize renderer");
        }

        app.run();
    }

    vulkanResult.value().reset();
    windowResult.value().reset();

    voxel::core::Log::shutdown();
    return 0;
}
