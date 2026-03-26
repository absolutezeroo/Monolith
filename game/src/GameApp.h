#pragma once

#include "voxel/core/Result.h"
#include "voxel/game/GameLoop.h"
#include "voxel/input/InputManager.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/Renderer.h"

#include <memory>
#include <string>

struct GLFWwindow;

namespace voxel::game
{
class Window;
}

namespace voxel::renderer
{
class VulkanContext;
}

class GameApp : public voxel::game::GameLoop
{
public:
    GameApp(
        voxel::game::Window& window,
        voxel::renderer::VulkanContext& vulkanContext);

    voxel::core::Result<void> init(const std::string& shaderDir);

protected:
    void tick(double dt) override;
    void render(double alpha) override;

private:
    void handleInputToggles();
    void buildDebugOverlay();

    voxel::game::Window& m_window;
    voxel::renderer::Renderer m_renderer;
    voxel::renderer::Camera m_camera;
    voxel::renderer::DebugOverlayState m_overlayState;

    std::unique_ptr<voxel::input::InputManager> m_input;

    // FPS tracking
    double m_lastFrameTime = -1.0;
    int m_fpsCount = 0;
    double m_fpsTimer = 0.0;
    int m_displayFps = 0;
};
