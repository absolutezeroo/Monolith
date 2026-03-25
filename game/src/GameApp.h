#pragma once

#include "voxel/game/GameLoop.h"
#include "voxel/renderer/Renderer.h"

namespace voxel::game
{
class Window;
}

namespace voxel::renderer
{
class VulkanContext;
}

/**
 * @brief Game application — subclass of GameLoop that owns the Renderer.
 *
 * Overrides render() to drive the Vulkan frame loop.
 * Lives in the game layer (not engine), keeping engine dependencies clean.
 */
class GameApp : public voxel::game::GameLoop
{
public:
    GameApp(
        voxel::game::Window& window,
        voxel::renderer::VulkanContext& vulkanContext);

    /**
     * @brief Initializes the renderer (frame resources, shaders, pipeline).
     * @param shaderDir Path to compiled .spv shader files.
     */
    voxel::core::Result<void> init(const std::string& shaderDir);

protected:
    void render(double alpha) override;

private:
    voxel::game::Window& m_window;
    voxel::renderer::Renderer m_renderer;
};
