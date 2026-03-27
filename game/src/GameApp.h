#pragma once

#include "voxel/core/ConfigManager.h"
#include "voxel/core/JobSystem.h"
#include "voxel/core/Result.h"
#include "voxel/game/GameLoop.h"
#include "voxel/input/InputManager.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/renderer/Renderer.h"
#include "voxel/world/BlockRegistry.h"
#include "voxel/world/ChunkManager.h"
#include "voxel/world/WorldGenerator.h"

#include <cstdint>
#include <memory>
#include <optional>
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
    GameApp(voxel::game::Window& window, voxel::renderer::VulkanContext& vulkanContext);
    ~GameApp();

    voxel::core::Result<void> init(const std::string& shaderDir, std::optional<int64_t> cliSeed = std::nullopt);

  protected:
    void tick(double dt) override;
    void render(double alpha) override;

  private:
    void handleInputToggles();
    void buildDebugOverlay();
    void drawCrosshair();
    void drawHotbar();
    void toggleFullscreen();
    void captureScreenshot();

    voxel::game::Window& m_window;
    voxel::renderer::Renderer m_renderer;
    voxel::renderer::Camera m_camera;
    voxel::renderer::DebugOverlayState m_overlayState;
    voxel::core::ConfigManager m_config;

    std::unique_ptr<voxel::input::InputManager> m_input;

    // World systems — declaration order matters for destruction:
    // JobSystem must outlive ChunkManager (in-flight tasks reference it).
    // MeshBuilder must outlive ChunkManager (tasks reference it).
    voxel::world::BlockRegistry m_blockRegistry;
    voxel::core::JobSystem m_jobSystem;
    std::unique_ptr<voxel::renderer::MeshBuilder> m_meshBuilder;
    std::unique_ptr<voxel::world::WorldGenerator> m_worldGen;
    voxel::world::ChunkManager m_chunkManager;

    // HUD state
    int m_hotbarSlot = 0;

    // Config file path
    std::string m_configPath;

    // FPS tracking
    double m_lastFrameTime = -1.0;
    int m_fpsCount = 0;
    double m_fpsTimer = 0.0;
    int m_displayFps = 0;
};
