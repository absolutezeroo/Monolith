#pragma once

#include "voxel/core/ConfigManager.h"
#include "voxel/core/JobSystem.h"
#include "voxel/core/Result.h"
#include "voxel/game/CommandQueue.h"
#include "voxel/game/EventBus.h"
#include "voxel/game/GameLoop.h"
#include "voxel/game/PlayerController.h"
#include "voxel/input/InputManager.h"
#include "voxel/physics/Raycast.h"
#include "voxel/scripting/WorldQueryAPI.h"
#include "voxel/renderer/Camera.h"
#include "voxel/renderer/ChunkUploadManager.h"
#include "voxel/renderer/MeshBuilder.h"
#include "voxel/renderer/Renderer.h"
#include "voxel/renderer/WieldMeshRenderer.h"
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

namespace voxel::scripting
{
class ScriptEngine;
class BlockCallbackInvoker;
class BlockTimerManager;
class ABMRegistry;
class LBMRegistry;
class NeighborNotifier;
class ShapeCache;
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
    void handleBlockInteraction(float dt);
    uint16_t resolveHotbarBlockId(int slot) const;
    void buildDebugOverlay();
    void drawCrosshair();
    void drawBlockHighlight();
    void drawCrackOverlay();
    void drawPostEffectTint();
    void drawWieldMesh();
    void drawHotbar();
    void toggleFullscreen();
    void captureScreenshot();

    voxel::game::Window& m_window;
    voxel::renderer::Renderer m_renderer;
    std::unique_ptr<voxel::renderer::ChunkUploadManager> m_uploadManager; // destroyed before Renderer
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

    // Raycast result (updated each frame in render())
    voxel::physics::RaycastResult m_raycastResult{};

    // Player physics controller
    voxel::game::PlayerController m_player;
    voxel::game::CommandQueue m_commandQueue;
    voxel::game::EventBus m_eventBus;
    bool m_flyMode = true; // Start in fly mode — toggled with F7
    bool m_isSprinting = false; // Persistent sprint toggle state

    // Scripting
    std::unique_ptr<voxel::scripting::ScriptEngine> m_scriptEngine;
    std::unique_ptr<voxel::scripting::BlockCallbackInvoker> m_callbackInvoker;
    std::unique_ptr<voxel::scripting::BlockTimerManager> m_timerManager;
    std::unique_ptr<voxel::scripting::ABMRegistry> m_abmRegistry;
    std::unique_ptr<voxel::scripting::LBMRegistry> m_lbmRegistry;
    std::unique_ptr<voxel::scripting::NeighborNotifier> m_neighborNotifier;
    std::unique_ptr<voxel::scripting::ShapeCache> m_shapeCache;
    voxel::scripting::RateLimiter m_rateLimiter;

    // HUD state
    int m_hotbarSlot = 0;
    int m_prevHotbarSlot = 0; // For wield slot-switch animation
    voxel::renderer::WieldAnimState m_wieldAnim;

    // Config file path
    std::string m_configPath;

    // FPS tracking
    double m_lastFrameTime = -1.0;
    int m_fpsCount = 0;
    double m_fpsTimer = 0.0;
    int m_displayFps = 0;
};
