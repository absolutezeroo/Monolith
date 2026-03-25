#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"
#include "voxel/game/GameLoop.h"
#include "voxel/game/Window.h"

int main()
{
    voxel::core::Log::init();

    auto windowResult = voxel::game::Window::create(1280, 720, "VoxelForge");
    if (!windowResult.has_value())
    {
        VX_FATAL("Failed to create window");
    }

    auto& window = *windowResult.value();

    voxel::game::GameLoop loop(window);
    loop.run();

    // Window destroyed via unique_ptr RAII before Log::shutdown
    windowResult.value().reset();

    voxel::core::Log::shutdown();
    return 0;
}
