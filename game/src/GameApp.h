#pragma once

#include "voxel/core/Result.h"
#include "voxel/game/GameLoop.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/Renderer.h"

#include <array>
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
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);

    void setupInputCallbacks();

    voxel::game::Window& m_window;
    voxel::renderer::Renderer m_renderer;
    voxel::renderer::Camera m_camera;
    voxel::renderer::DebugOverlayState m_overlayState;

    // Input state
    std::array<bool, 512> m_keyStates{};
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    bool m_cursorCaptured = true;
    bool m_firstMouse = true;
};
