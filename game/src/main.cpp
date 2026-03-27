#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/VulkanContext.h"

#include "GameApp.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

/// Scan argv for --seed <value> and return it if found.
static std::optional<int64_t> parseSeedArg(int argc, char* argv[])
{
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::string_view(argv[i]) == "--seed")
        {
            char* end = nullptr;
            int64_t seed = std::strtoll(argv[i + 1], &end, 10);
            if (end != argv[i + 1])
            {
                return seed;
            }
        }
    }
    return std::nullopt;
}

int main(int argc, char* argv[])
{
    voxel::core::Log::init();

    std::optional<int64_t> cliSeed = parseSeedArg(argc, argv);

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

        auto initResult = app.init(VX_SHADER_DIR, cliSeed);
        if (!initResult.has_value())
        {
            VX_LOG_CRITICAL("Init failed: {}", initResult.error().message);
            VX_FATAL("Failed to initialize");
        }

        app.run();
    }

    vulkanResult.value().reset();
    windowResult.value().reset();

    voxel::core::Log::shutdown();
    return 0;
}
