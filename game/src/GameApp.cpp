#include "GameApp.h"

GameApp::GameApp(
    voxel::game::Window& window,
    voxel::renderer::VulkanContext& vulkanContext)
    : GameLoop(window)
    , m_window(window)
    , m_renderer(vulkanContext)
{
}

voxel::core::Result<void> GameApp::init(const std::string& shaderDir)
{
    return m_renderer.init(shaderDir);
}

void GameApp::render(double /*alpha*/)
{
    m_renderer.draw(m_window);
}
