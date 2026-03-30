#include "GameApp.h"

#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/VulkanContext.h"
#include "voxel/scripting/BlockCallbackInvoker.h"
#include "voxel/scripting/LuaBindings.h"
#include "voxel/scripting/ScriptEngine.h"

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

static constexpr int HOTBAR_SLOTS = 9;
static constexpr float CROSSHAIR_SIZE = 12.0f;
static constexpr float CROSSHAIR_THICKNESS = 2.0f;
static constexpr float HOTBAR_SLOT_SIZE = 48.0f;
static constexpr float HOTBAR_PADDING = 4.0f;
static constexpr const char* HOTBAR_BLOCK_NAMES[HOTBAR_SLOTS] = {
    "Stone", "Dirt", "Grass", "Sand", "Wood", "Leaves", "Glass", "Torch", "Cobble"};

/// Registry string IDs corresponding to HOTBAR_BLOCK_NAMES.
static constexpr const char* HOTBAR_BLOCK_IDS[HOTBAR_SLOTS] = {
    "base:stone",
    "base:dirt",
    "base:grass_block",
    "base:sand",
    "base:oak_log",
    "base:oak_leaves",
    "base:glass",
    "base:torch",
    "base:cobblestone"};

GameApp::GameApp(voxel::game::Window& window, voxel::renderer::VulkanContext& vulkanContext)
    : GameLoop(window), m_window(window), m_renderer(vulkanContext)
{
}

GameApp::~GameApp()
{
    // Wait for all in-flight mesh tasks BEFORE any member destruction.
    // Tasks hold non-owning pointers to MeshBuilder and push to ChunkManager's queue,
    // so all tasks must complete while those objects are still alive.
    m_chunkManager.shutdown();
    m_jobSystem.shutdown();

    // Destroy callback invoker before script engine (it holds a ref to sol::state)
    m_callbackInvoker.reset();
    if (m_scriptEngine)
    {
        m_scriptEngine->shutdown();
    }

    // Save config on exit — sync all persisted settings back before writing
    if (!m_configPath.empty())
    {
        m_config.setFov(m_camera.getFov());
        m_config.setSensitivity(m_camera.getSensitivity());
        m_config.setLastPlayerPosition(m_flyMode ? m_camera.getPosition() : m_player.getEyePosition());

        int w = 0;
        int h = 0;
        m_window.getFramebufferSize(w, h);
        if (w > 0 && h > 0 && !m_config.isFullscreen())
        {
            m_config.setWindowWidth(w);
            m_config.setWindowHeight(h);
        }

        m_config.save(m_configPath);
    }
}

static int64_t generateRandomSeed()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
    auto pid = static_cast<int64_t>(_getpid());
#else
    auto pid = static_cast<int64_t>(getpid());
#endif
    return static_cast<int64_t>(now) ^ (pid << 16);
}

voxel::core::Result<void> GameApp::init(const std::string& shaderDir, std::optional<int64_t> cliSeed)
{
    // Load config
    m_configPath = "config.json";
    auto loadResult = m_config.load(m_configPath);
    bool hadConfigFile = loadResult.has_value();
    if (!hadConfigFile)
    {
        VX_LOG_WARN("Config load returned error — using defaults");
    }

    // Seed resolution: CLI > config.json > random
    if (cliSeed.has_value())
    {
        m_config.setSeed(*cliSeed);
        VX_LOG_INFO("Seed from CLI: {}", *cliSeed);
    }
    else if (!hadConfigFile)
    {
        int64_t randomSeed = generateRandomSeed();
        m_config.setSeed(randomSeed);
        VX_LOG_INFO("Generated random seed: {}", randomSeed);
    }
    else
    {
        VX_LOG_INFO("Seed from config: {}", m_config.getSeed());
    }

    // Persist seed to config
    m_config.save(m_configPath);

    // Initialize scripting engine
    m_scriptEngine = std::make_unique<voxel::scripting::ScriptEngine>();
    auto scriptInitResult = m_scriptEngine->init();
    if (!scriptInitResult.has_value())
    {
        return std::unexpected(scriptInitResult.error());
    }

    // Bind Lua block registration API
    voxel::scripting::LuaBindings::registerBlockAPI(m_scriptEngine->getLuaState(), m_blockRegistry);

    // Load base block registrations from Lua (replaces blocks.json)
    std::filesystem::path assetsDir(VX_ASSETS_DIR);
    m_scriptEngine->addAllowedPath(assetsDir / "scripts");
    auto loadInitResult = m_scriptEngine->loadScript(assetsDir / "scripts" / "base" / "init.lua");
    if (!loadInitResult.has_value())
    {
        return std::unexpected(loadInitResult.error());
    }

    // Create callback invoker for block lifecycle events
    m_callbackInvoker =
        std::make_unique<voxel::scripting::BlockCallbackInvoker>(m_scriptEngine->getLuaState(), m_blockRegistry);

    // Create WorldGenerator
    m_worldGen =
        std::make_unique<voxel::world::WorldGenerator>(static_cast<uint64_t>(m_config.getSeed()), m_blockRegistry);

    // Inject WorldGenerator and BlockRegistry into ChunkManager
    m_chunkManager.setWorldGenerator(m_worldGen.get());
    m_chunkManager.setBlockRegistry(&m_blockRegistry);

    // Initialize async meshing pipeline
    auto jobResult = m_jobSystem.init();
    if (!jobResult.has_value())
    {
        return std::unexpected(jobResult.error());
    }
    m_meshBuilder = std::make_unique<voxel::renderer::MeshBuilder>(m_blockRegistry);
    m_chunkManager.setJobSystem(&m_jobSystem);
    m_chunkManager.setMeshBuilder(m_meshBuilder.get());

    // Apply config to camera
    m_camera.setFov(m_config.getFov());
    m_camera.setSensitivity(m_config.getSensitivity());
    m_overlayState.fov = m_config.getFov();
    m_overlayState.sensitivity = m_config.getSensitivity();

    // Create InputManager — registers GLFW callbacks.
    // Must happen BEFORE renderer init (ImGui chains on top of existing callbacks).
    m_input = std::make_unique<voxel::input::InputManager>(m_window.getHandle());

    // InputManager now owns the GLFW user pointer, so Window's original framebuffer
    // callback (which casts user pointer to Window*) would crash. Remove it.
    // Swapchain recreation is handled by VK_ERROR_OUT_OF_DATE_KHR in Renderer::draw().
    glfwSetFramebufferSizeCallback(m_window.getHandle(), nullptr);

    auto result = m_renderer.init(shaderDir, VX_ASSETS_DIR, m_window);
    if (!result.has_value())
    {
        return result;
    }

    // Create mesh upload manager — uses Renderer's Gigabuffer, StagingBuffer, and ChunkRenderInfoBuffer
    m_uploadManager = std::make_unique<voxel::renderer::ChunkUploadManager>(
        *m_renderer.getMutableGigabuffer(),
        *m_renderer.getMutableStagingBuffer(),
        *m_renderer.getMutableChunkRenderInfoBuffer());

    // Subscribe to block events for dynamic light updates
    m_eventBus.subscribe<voxel::game::EventType::BlockBroken>(
        [this](const voxel::game::BlockBrokenEvent& e) {
            glm::ivec3 pos{e.position.x, e.position.y, e.position.z};
            const auto& oldDef = m_blockRegistry.getBlockType(e.previousBlockId);
            m_chunkManager.updateLightAfterBlockChange(pos, &oldDef, nullptr);
        });

    m_eventBus.subscribe<voxel::game::EventType::BlockPlaced>(
        [this](const voxel::game::BlockPlacedEvent& e) {
            glm::ivec3 pos{e.position.x, e.position.y, e.position.z};
            const auto& newDef = m_blockRegistry.getBlockType(e.blockId);
            m_chunkManager.updateLightAfterBlockChange(pos, nullptr, &newDef);
        });

    // Start with cursor captured for FPS camera
    m_input->setCursorCaptured(true);
    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(m_window.getHandle(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    return {};
}

void GameApp::handleInputToggles()
{
    if (m_input->wasKeyPressed(GLFW_KEY_F3))
    {
        m_overlayState.showOverlay = !m_overlayState.showOverlay;
    }
    if (m_input->wasKeyPressed(GLFW_KEY_F4))
    {
        m_overlayState.wireframeMode = !m_overlayState.wireframeMode;
    }
    if (m_input->wasKeyPressed(GLFW_KEY_F5))
    {
        m_overlayState.showChunkBorders = !m_overlayState.showChunkBorders;
    }
    if (m_input->wasKeyPressed(GLFW_KEY_F11))
    {
        toggleFullscreen();
    }
    if (m_input->wasKeyPressed(GLFW_KEY_F2))
    {
        captureScreenshot();
    }
    if (m_input->wasKeyPressed(GLFW_KEY_ESCAPE))
    {
        m_input->setCursorCaptured(false);
    }
    if (m_input->wasKeyPressed(GLFW_KEY_F7))
    {
        m_flyMode = !m_flyMode;
        if (!m_flyMode)
        {
            // Entering physics mode: init player at camera position
            m_player.init(m_camera.getPosition(), m_chunkManager, m_blockRegistry);
        }
        else
        {
            // Entering fly mode: snap camera to player eye position
            m_camera.setPosition(m_player.getEyePosition());
        }
        VX_LOG_INFO("[F7] Fly mode: {}", m_flyMode ? "ON" : "OFF");
    }

    // Hotbar: number keys 1-9
    for (int i = 0; i < HOTBAR_SLOTS; ++i)
    {
        if (m_input->wasKeyPressed(GLFW_KEY_1 + i))
        {
            m_hotbarSlot = i;
        }
    }

    // Hotbar: scroll wheel
    float scroll = m_input->getScrollDelta();
    if (scroll != 0.0f && m_input->isCursorCaptured())
    {
        m_hotbarSlot = (m_hotbarSlot - static_cast<int>(scroll) + HOTBAR_SLOTS) % HOTBAR_SLOTS;
    }
}

uint16_t GameApp::resolveHotbarBlockId(int slot) const
{
    if (slot < 0 || slot >= HOTBAR_SLOTS)
    {
        return voxel::world::BLOCK_AIR;
    }
    uint16_t id = m_blockRegistry.getIdByName(HOTBAR_BLOCK_IDS[slot]);
    return id; // Returns BLOCK_AIR (0) if name not found
}

void GameApp::handleBlockInteraction(float dt)
{
    if (!m_input->isCursorCaptured())
    {
        m_player.resetMining();
        return;
    }

    // Mining: LMB hold
    bool lmbDown = m_input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
    bool miningCompleted = m_player.updateMining(dt, m_raycastResult, lmbDown, m_chunkManager, m_blockRegistry);

    if (miningCompleted)
    {
        m_commandQueue.push(voxel::game::GameCommand{
            voxel::game::CommandType::BreakBlock,
            0,
            0,
            voxel::game::BreakBlockPayload{
                voxel::math::IVec3{
                    m_raycastResult.blockPos.x, m_raycastResult.blockPos.y, m_raycastResult.blockPos.z}}});
    }

    // Placement: RMB press (one-shot)
    bool placementAttempted = m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT) && m_raycastResult.hit;
    if (placementAttempted)
    {
        uint16_t blockId = resolveHotbarBlockId(m_hotbarSlot);
        if (blockId != voxel::world::BLOCK_AIR)
        {
            auto& prev = m_raycastResult.previousPos;
            m_commandQueue.push(voxel::game::GameCommand{
                voxel::game::CommandType::PlaceBlock,
                0,
                0,
                voxel::game::PlaceBlockPayload{voxel::math::IVec3{prev.x, prev.y, prev.z}, blockId}});
        }
    }

    // Drain block commands
    m_commandQueue.drain([&](voxel::game::GameCommand cmd) {
        switch (cmd.type)
        {
        case voxel::game::CommandType::BreakBlock:
        {
            auto& payload = std::get<voxel::game::BreakBlockPayload>(cmd.payload);
            glm::ivec3 pos{payload.position.x, payload.position.y, payload.position.z};
            uint16_t previousId = m_chunkManager.getBlock(pos);
            if (previousId == voxel::world::BLOCK_AIR)
            {
                break; // Nothing to break
            }
            const auto& def = m_blockRegistry.getBlockType(previousId);

            // Callback: can_dig → abort if false
            if (m_callbackInvoker && !m_callbackInvoker->invokeCanDig(def, pos, cmd.playerId))
            {
                break;
            }

            // Callback: on_destruct (block still exists)
            if (m_callbackInvoker)
            {
                m_callbackInvoker->invokeOnDestruct(def, pos);
            }

            // Callback: on_dig → abort if returns false
            if (m_callbackInvoker && !m_callbackInvoker->invokeOnDig(def, pos, previousId, cmd.playerId))
            {
                break;
            }

            // Remove block
            m_chunkManager.setBlock(pos, voxel::world::BLOCK_AIR);

            // Callback: after_destruct, after_dig
            if (m_callbackInvoker)
            {
                m_callbackInvoker->invokeAfterDestruct(def, pos, previousId);
                m_callbackInvoker->invokeAfterDig(def, pos, previousId, cmd.playerId);
            }

            // EventBus fires AFTER all per-block callbacks
            m_eventBus.publish<voxel::game::EventType::BlockBroken>(
                voxel::game::BlockBrokenEvent{payload.position, previousId});
            VX_LOG_DEBUG("Block broken at ({},{},{}): id={}", pos.x, pos.y, pos.z, previousId);
            break;
        }
        case voxel::game::CommandType::PlaceBlock:
        {
            auto& payload = std::get<voxel::game::PlaceBlockPayload>(cmd.payload);
            glm::ivec3 pos{payload.position.x, payload.position.y, payload.position.z};

            // Validate: target must be air or isBuildableTo
            uint16_t targetBlock = m_chunkManager.getBlock(pos);
            if (targetBlock != voxel::world::BLOCK_AIR)
            {
                const auto& targetDef = m_blockRegistry.getBlockType(targetBlock);
                if (!targetDef.isBuildableTo)
                {
                    break; // Silently reject
                }
            }

            // Validate: block AABB must not overlap player AABB
            voxel::math::AABB blockBox{
                voxel::math::Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
                voxel::math::Vec3{
                    static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1), static_cast<float>(pos.z + 1)}};
            if (!m_flyMode && blockBox.intersects(m_player.getAABB()))
            {
                break; // Can't place inside yourself
            }

            const auto& newDef = m_blockRegistry.getBlockType(payload.blockId);

            // Callback: can_place → abort if false
            if (m_callbackInvoker && !m_callbackInvoker->invokeCanPlace(newDef, pos, cmd.playerId))
            {
                break;
            }

            // Callback: on_place
            if (m_callbackInvoker)
            {
                m_callbackInvoker->invokeOnPlace(newDef, pos, cmd.playerId);
            }

            // Set block
            m_chunkManager.setBlock(pos, payload.blockId);

            // Callback: on_construct, after_place
            if (m_callbackInvoker)
            {
                m_callbackInvoker->invokeOnConstruct(newDef, pos);
                m_callbackInvoker->invokeAfterPlace(newDef, pos, cmd.playerId);
            }

            // EventBus fires AFTER all per-block callbacks
            m_eventBus.publish<voxel::game::EventType::BlockPlaced>(
                voxel::game::BlockPlacedEvent{payload.position, payload.blockId});
            VX_LOG_DEBUG("Block placed at ({},{},{}): id={}", pos.x, pos.y, pos.z, payload.blockId);
            break;
        }
        default:
            break;
        }
    });

    // Wield animation: advance timer, then resolve next state by priority.
    // Priority: SlotSwitch > Mining > Place > Idle.
    m_wieldAnim.update(dt);

    if (m_hotbarSlot != m_prevHotbarSlot)
    {
        // Highest priority: slot changed — always interrupt with Switch.
        m_wieldAnim.startAnim(voxel::renderer::WieldAnimType::Switch);
        m_prevHotbarSlot = m_hotbarSlot;
    }
    else if (m_player.getMiningState().isMining)
    {
        // Mining active — start Mining anim if not already playing.
        if (m_wieldAnim.animType != voxel::renderer::WieldAnimType::Mining)
        {
            m_wieldAnim.startAnim(voxel::renderer::WieldAnimType::Mining);
        }
    }
    else if (miningCompleted || placementAttempted)
    {
        // Block just broke or placed — kick a thrust animation as feedback.
        m_wieldAnim.startAnim(voxel::renderer::WieldAnimType::Place);
    }
    else if (m_wieldAnim.isComplete())
    {
        // One-shot animation finished (Place or Switch) — return to Idle.
        m_wieldAnim.startAnim(voxel::renderer::WieldAnimType::Idle);
    }
}

void GameApp::tick(double dt)
{
    auto fdt = static_cast<float>(dt);

    // Read edge flags set by GLFW callbacks (fired during pollEvents, before tick)
    handleInputToggles();

    ImGuiIO& io = ImGui::GetIO();

    // Recapture cursor on left-click when released and ImGui doesn't want mouse
    if (!m_input->isCursorCaptured() && m_input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) && !io.WantCaptureMouse)
    {
        m_input->setCursorCaptured(true);
    }

    // Camera mouse look — only when cursor is captured and ImGui doesn't want input
    if (m_input->isCursorCaptured() && !io.WantCaptureMouse)
    {
        glm::vec2 delta = m_input->getMouseDelta();
        m_camera.processMouseDelta(delta.x, delta.y);
    }

    // Movement — only when ImGui doesn't want keyboard
    if (!io.WantCaptureKeyboard)
    {
        if (m_flyMode)
        {
            // Fly mode: direct camera movement (existing behavior)
            m_camera.update(
                fdt,
                m_input->isKeyDown(GLFW_KEY_W),
                m_input->isKeyDown(GLFW_KEY_S),
                m_input->isKeyDown(GLFW_KEY_A),
                m_input->isKeyDown(GLFW_KEY_D),
                m_input->isKeyDown(GLFW_KEY_SPACE),
                m_input->isKeyDown(GLFW_KEY_LEFT_SHIFT));
        }
        else
        {
            // Physics mode: build movement commands from input, then run physics
            glm::vec3 forward = m_camera.getForward();
            glm::vec3 right = m_camera.getRight();
            forward.y = 0.0f;
            right.y = 0.0f;
            if (glm::length(forward) > 0.0f)
            {
                forward = glm::normalize(forward);
            }
            if (glm::length(right) > 0.0f)
            {
                right = glm::normalize(right);
            }

            glm::vec3 dir{0.0f};
            if (m_input->isKeyDown(GLFW_KEY_W))
            {
                dir += forward;
            }
            if (m_input->isKeyDown(GLFW_KEY_S))
            {
                dir -= forward;
            }
            if (m_input->isKeyDown(GLFW_KEY_D))
            {
                dir += right;
            }
            if (m_input->isKeyDown(GLFW_KEY_A))
            {
                dir -= right;
            }
            if (glm::length(dir) > 0.001f)
            {
                dir = glm::normalize(dir);
            }

            bool sneak = m_input->isKeyDown(GLFW_KEY_LEFT_SHIFT);
            if (m_input->wasKeyPressed(GLFW_KEY_LEFT_CONTROL))
            {
                m_isSprinting = !m_isSprinting;
            }
            if (sneak)
            {
                m_isSprinting = false;
            }

            bool jump = m_input->isKeyDown(GLFW_KEY_SPACE);

            // Push commands to queue
            m_commandQueue.push(voxel::game::GameCommand{
                voxel::game::CommandType::MovePlayer,
                0,
                0,
                voxel::game::MovePlayerPayload{
                    voxel::math::Vec3{dir.x, dir.y, dir.z}, m_isSprinting, sneak}});

            if (jump)
            {
                m_commandQueue.push(voxel::game::GameCommand{
                    voxel::game::CommandType::Jump, 0, 0, voxel::game::JumpPayload{}});
            }

            // Drain command queue into movement state
            voxel::game::MovementInput moveInput;
            m_commandQueue.drain([&](voxel::game::GameCommand cmd) {
                switch (cmd.type)
                {
                case voxel::game::CommandType::MovePlayer:
                {
                    auto& payload = std::get<voxel::game::MovePlayerPayload>(cmd.payload);
                    moveInput.wishDir = glm::vec3{payload.direction.x, payload.direction.y, payload.direction.z};
                    moveInput.sprint = payload.isSprinting;
                    moveInput.sneak = payload.isSneaking;
                    break;
                }
                case voxel::game::CommandType::Jump:
                    moveInput.jump = true;
                    break;
                default:
                    break;
                }
            });

            m_player.tickPhysics(fdt, moveInput, m_chunkManager, m_blockRegistry);
            m_camera.setPosition(m_player.getEyePosition());
        }
    }

    // Block interaction: mining (LMB hold) and placement (RMB press).
    // Called after physics so player position is current for distance checks.
    // Works in both fly and physics mode; guarded by cursor capture internally.
    if (!io.WantCaptureMouse)
    {
        handleBlockInteraction(fdt);
    }
    else
    {
        m_player.resetMining();
    }

    // Sync overlay sliders back to camera
    m_camera.setFov(m_overlayState.fov);
    m_camera.setSensitivity(m_overlayState.sensitivity);

    // Update aspect ratio from window
    int w = 0;
    int h = 0;
    m_window.getFramebufferSize(w, h);
    if (h > 0)
    {
        m_camera.setAspectRatio(static_cast<float>(w) / static_cast<float>(h));
    }

    // Sync camera state to overlay for display
    m_overlayState.fov = m_camera.getFov();
    m_overlayState.sensitivity = m_camera.getSensitivity();

    // Async meshing: poll results and dispatch dirty sections
    m_chunkManager.update(m_camera.getPosition());

    // NOTE: mesh uploads moved to render() — must happen AFTER beginFrame()
    // calls StagingBuffer::beginFrame() which resets the pending transfer list.

    // Clear edge flags and update hold timers at end of tick.
    // Edge flags were set by pollEvents callbacks, read above, and now cleared
    // so subsequent ticks in the same frame won't see stale presses.
    m_input->update(fdt);
}

void GameApp::buildDebugOverlay()
{
    if (!m_overlayState.showOverlay)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##DebugOverlay", nullptr, flags))
    {
        ImGui::Text("VoxelForge v0.1.0");
        ImGui::Separator();

        ImGui::Text(
            "FPS: %d (%.1f ms)", m_displayFps, m_displayFps > 0 ? 1000.0f / static_cast<float>(m_displayFps) : 0.0f);

        const auto& pos = m_camera.getPosition();
        ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_camera.getYaw(), m_camera.getPitch());

        float yaw = m_camera.getYaw();
        float normalizedYaw = yaw - 360.0f * std::floor(yaw / 360.0f);
        const char* facing = "North (+Z)";
        if (normalizedYaw >= 45.0f && normalizedYaw < 135.0f)
            facing = "East (+X)";
        else if (normalizedYaw >= 135.0f && normalizedYaw < 225.0f)
            facing = "South (-Z)";
        else if (normalizedYaw >= 225.0f && normalizedYaw < 315.0f)
            facing = "West (-X)";
        ImGui::Text("Facing: %s", facing);

        if (m_raycastResult.hit)
        {
            auto& bp = m_raycastResult.blockPos;
            uint16_t blockId = m_chunkManager.getBlock(bp);
            const auto& def = m_blockRegistry.getBlockType(blockId);
            static constexpr const char* FACE_NAMES[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            int faceIdx = static_cast<int>(m_raycastResult.face);
            const char* faceName = (faceIdx >= 0 && faceIdx < 6) ? FACE_NAMES[faceIdx] : "?";
            ImGui::Text(
                "Target: %d, %d, %d (%s) face=%s d=%.1f",
                bp.x,
                bp.y,
                bp.z,
                def.stringId.c_str(),
                faceName,
                m_raycastResult.distance);
        }

        ImGui::Separator();

        const auto* giga = m_renderer.getGigabuffer();
        if (giga != nullptr)
        {
            auto usedMb = static_cast<double>(giga->usedBytes()) / (1024.0 * 1024.0);
            auto capMb = static_cast<double>(giga->getCapacity()) / (1024.0 * 1024.0);
            double pct = capMb > 0.0 ? 100.0 * usedMb / capMb : 0.0;
            ImGui::Text("Gigabuffer: %.1f / %.0f MB (%.0f%%)", usedMb, capMb, pct);
        }
        else
        {
            ImGui::Text("Gigabuffer: N/A");
        }
        ImGui::Text(
            "Chunks: %zu loaded, %zu dirty", m_chunkManager.loadedChunkCount(), m_chunkManager.dirtyChunkCount());
        if (m_uploadManager)
        {
            ImGui::Text(
                "GPU Sections: %u resident, %u pending, %u deferred-free",
                m_uploadManager->residentCount(),
                m_uploadManager->pendingUploadCount(),
                m_uploadManager->deferredFreeCount());
        }
        ImGui::Text("Draw: indirect  Sections: %u", m_renderer.getLastDrawCount());
        ImGui::Text("Seed: %lld", static_cast<long long>(m_config.getSeed()));

        ImGui::Separator();

        ImGui::Text("[F4] Wireframe: %s", m_overlayState.wireframeMode ? "ON" : "OFF");
        ImGui::Text("[F5] Chunk borders: %s", m_overlayState.showChunkBorders ? "ON" : "OFF");
        ImGui::Text("[F7] Fly mode: %s", m_flyMode ? "ON" : "OFF");
        if (!m_flyMode)
        {
            ImGui::Text("OnGround: %s", m_player.isOnGround() ? "YES" : "NO");
            auto vel = m_player.getVelocity();
            ImGui::Text("Velocity: %.2f, %.2f, %.2f", vel.x, vel.y, vel.z);
            ImGui::Text("Sprint: %s", m_player.isSprinting() ? "YES" : "NO");
            ImGui::Text("Sneak: %s", m_player.isSneaking() ? "YES" : "NO");
            if (m_player.isInClimbable())
            {
                ImGui::Text("Climbing: YES");
            }
            if (m_player.getMaxResistance() > 0)
            {
                ImGui::Text("Resistance: %u", m_player.getMaxResistance());
            }
        }

        ImGui::Separator();

        // Day/night cycle display
        {
            float tod = m_renderer.getTimeOfDay();
            int hours = static_cast<int>(tod * 24.0f) % 24;
            int minutes = static_cast<int>(tod * 24.0f * 60.0f) % 60;
            const char* phase = "Night";
            if (tod >= 0.20f && tod < 0.30f)
                phase = "Dawn";
            else if (tod >= 0.30f && tod < 0.70f)
                phase = "Day";
            else if (tod >= 0.70f && tod < 0.80f)
                phase = "Dusk";
            ImGui::Text("Time: %02d:%02d (%s)", hours, minutes, phase);
            ImGui::Text("DayNight: %.2f", m_renderer.getDayNightFactor());
        }

        // Time-of-day override slider
        {
            float tod = m_renderer.getTimeOfDay();
            if (ImGui::SliderFloat("Time of Day", &tod, 0.0f, 1.0f, "%.3f"))
            {
                m_renderer.setTimeOfDay(tod);
            }
            float cycleMins = m_renderer.getCycleDuration() / 60.0f;
            if (ImGui::SliderFloat("Cycle (min)", &cycleMins, 1.0f, 60.0f, "%.0f"))
            {
                m_renderer.setCycleDuration(cycleMins * 60.0f);
            }
        }

        ImGui::Separator();

        ImGui::SliderFloat("FOV", &m_overlayState.fov, 50.0f, 110.0f, "%.0f");
        ImGui::SliderFloat("Sensitivity", &m_overlayState.sensitivity, 0.01f, 0.5f, "%.3f");
    }
    ImGui::End();
}

void GameApp::drawCrosshair()
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    auto col = IM_COL32(255, 255, 255, 200);

    // Horizontal line
    draw->AddLine(
        ImVec2(center.x - CROSSHAIR_SIZE, center.y),
        ImVec2(center.x + CROSSHAIR_SIZE, center.y),
        col,
        CROSSHAIR_THICKNESS);
    // Vertical line
    draw->AddLine(
        ImVec2(center.x, center.y - CROSSHAIR_SIZE),
        ImVec2(center.x, center.y + CROSSHAIR_SIZE),
        col,
        CROSSHAIR_THICKNESS);
}

void GameApp::drawBlockHighlight()
{
    if (!m_raycastResult.hit)
    {
        return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;

    glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();

    // Block AABB: integer position -> (pos, pos+1)
    glm::vec3 bmin = glm::vec3(m_raycastResult.blockPos);
    glm::vec3 bmax = bmin + glm::vec3(1.0f);

    // Slight expansion to prevent z-fighting with block faces
    constexpr float OFFSET = 0.002f;
    bmin -= glm::vec3(OFFSET);
    bmax += glm::vec3(OFFSET);

    // 8 cube corners
    glm::vec3 corners[8] = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z}, {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z}, {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z},
    };

    // Project corners to screen space
    ImVec2 screenPts[8];
    bool behindCamera[8];
    for (int i = 0; i < 8; ++i)
    {
        glm::vec4 clip = vp * glm::vec4(corners[i], 1.0f);
        behindCamera[i] = (clip.w <= 0.0f);
        if (!behindCamera[i])
        {
            float invW = 1.0f / clip.w;
            float ndcX = clip.x * invW;
            float ndcY = clip.y * invW;
            // Projection matrix already flips Y for Vulkan (proj[1][1] *= -1),
            // so NDC Y goes -1=top to +1=bottom — map directly to screen coords.
            screenPts[i].x = (ndcX * 0.5f + 0.5f) * vpSize.x;
            screenPts[i].y = (ndcY * 0.5f + 0.5f) * vpSize.y;
        }
    }

    // 12 edges of a cube (index pairs)
    constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // front face
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // back face
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // connecting edges
    };

    auto lineCol = IM_COL32(255, 255, 255, 180);
    constexpr float LINE_THICKNESS = 2.0f;

    for (const auto& edge : edges)
    {
        int a = edge[0];
        int b = edge[1];
        // Skip any edge where either endpoint is behind the camera
        if (behindCamera[a] || behindCamera[b])
        {
            continue;
        }
        draw->AddLine(screenPts[a], screenPts[b], lineCol, LINE_THICKNESS);
    }
}

void GameApp::drawHotbar()
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 viewport = ImGui::GetMainViewport()->Size;

    float totalWidth = HOTBAR_SLOTS * HOTBAR_SLOT_SIZE + (HOTBAR_SLOTS - 1) * HOTBAR_PADDING;
    float startX = (viewport.x - totalWidth) * 0.5f;
    float startY = viewport.y - HOTBAR_SLOT_SIZE - 20.0f;

    for (int i = 0; i < HOTBAR_SLOTS; ++i)
    {
        float x = startX + static_cast<float>(i) * (HOTBAR_SLOT_SIZE + HOTBAR_PADDING);
        ImVec2 p0(x, startY);
        ImVec2 p1(x + HOTBAR_SLOT_SIZE, startY + HOTBAR_SLOT_SIZE);

        // Background (60% alpha black per UX spec)
        draw->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 153), 4.0f);

        // Border
        auto borderCol = (i == m_hotbarSlot) ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 120, 120, 180);
        float borderThickness = (i == m_hotbarSlot) ? 2.0f : 1.0f;
        draw->AddRect(p0, p1, borderCol, 4.0f, 0, borderThickness);

        // Block name label
        ImVec2 textSize = ImGui::CalcTextSize(HOTBAR_BLOCK_NAMES[i]);
        float textX = x + (HOTBAR_SLOT_SIZE - textSize.x) * 0.5f;
        float textY = startY + (HOTBAR_SLOT_SIZE - textSize.y) * 0.5f;
        draw->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 220), HOTBAR_BLOCK_NAMES[i]);
    }
}

void GameApp::drawCrackOverlay()
{
    const auto& miningState = m_player.getMiningState();
    if (miningState.crackStage < 0 || !m_raycastResult.hit)
    {
        return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();

    // Get the face normal to compute the 4 corners of the crack quad
    glm::vec3 bpos = glm::vec3(m_raycastResult.blockPos);
    constexpr float OFFSET = 0.001f; // Slight offset to prevent z-fighting

    // Face quad corners based on hit face
    glm::vec3 corners[4];
    switch (m_raycastResult.face)
    {
    case voxel::renderer::BlockFace::PosX: // +X face
        corners[0] = bpos + glm::vec3(1.0f + OFFSET, 0.0f, 0.0f);
        corners[1] = bpos + glm::vec3(1.0f + OFFSET, 0.0f, 1.0f);
        corners[2] = bpos + glm::vec3(1.0f + OFFSET, 1.0f, 1.0f);
        corners[3] = bpos + glm::vec3(1.0f + OFFSET, 1.0f, 0.0f);
        break;
    case voxel::renderer::BlockFace::NegX: // -X face
        corners[0] = bpos + glm::vec3(-OFFSET, 0.0f, 1.0f);
        corners[1] = bpos + glm::vec3(-OFFSET, 0.0f, 0.0f);
        corners[2] = bpos + glm::vec3(-OFFSET, 1.0f, 0.0f);
        corners[3] = bpos + glm::vec3(-OFFSET, 1.0f, 1.0f);
        break;
    case voxel::renderer::BlockFace::PosY: // +Y face (top)
        corners[0] = bpos + glm::vec3(0.0f, 1.0f + OFFSET, 0.0f);
        corners[1] = bpos + glm::vec3(1.0f, 1.0f + OFFSET, 0.0f);
        corners[2] = bpos + glm::vec3(1.0f, 1.0f + OFFSET, 1.0f);
        corners[3] = bpos + glm::vec3(0.0f, 1.0f + OFFSET, 1.0f);
        break;
    case voxel::renderer::BlockFace::NegY: // -Y face (bottom)
        corners[0] = bpos + glm::vec3(0.0f, -OFFSET, 1.0f);
        corners[1] = bpos + glm::vec3(1.0f, -OFFSET, 1.0f);
        corners[2] = bpos + glm::vec3(1.0f, -OFFSET, 0.0f);
        corners[3] = bpos + glm::vec3(0.0f, -OFFSET, 0.0f);
        break;
    case voxel::renderer::BlockFace::PosZ: // +Z face
        corners[0] = bpos + glm::vec3(1.0f, 0.0f, 1.0f + OFFSET);
        corners[1] = bpos + glm::vec3(0.0f, 0.0f, 1.0f + OFFSET);
        corners[2] = bpos + glm::vec3(0.0f, 1.0f, 1.0f + OFFSET);
        corners[3] = bpos + glm::vec3(1.0f, 1.0f, 1.0f + OFFSET);
        break;
    case voxel::renderer::BlockFace::NegZ: // -Z face
        corners[0] = bpos + glm::vec3(0.0f, 0.0f, -OFFSET);
        corners[1] = bpos + glm::vec3(1.0f, 0.0f, -OFFSET);
        corners[2] = bpos + glm::vec3(1.0f, 1.0f, -OFFSET);
        corners[3] = bpos + glm::vec3(0.0f, 1.0f, -OFFSET);
        break;
    }

    // Project to screen space
    ImVec2 screenPts[4];
    for (int i = 0; i < 4; ++i)
    {
        glm::vec4 clip = vp * glm::vec4(corners[i], 1.0f);
        if (clip.w <= 0.0f)
        {
            return; // Face is behind camera
        }
        float invW = 1.0f / clip.w;
        float ndcX = clip.x * invW;
        float ndcY = clip.y * invW;
        screenPts[i].x = (ndcX * 0.5f + 0.5f) * vpSize.x;
        screenPts[i].y = (ndcY * 0.5f + 0.5f) * vpSize.y;
    }

    // Crack alpha increases with stage: stage 0 = light, stage 9 = almost opaque
    int stage = miningState.crackStage;
    uint8_t alpha = static_cast<uint8_t>(40 + stage * 20); // 40..220
    auto crackCol = IM_COL32(0, 0, 0, alpha);

    // Draw filled quad using two triangles
    draw->AddQuadFilled(screenPts[0], screenPts[1], screenPts[2], screenPts[3], crackCol);

    // Draw crack pattern lines (cross pattern that gets denser with higher stages)
    auto lineCol = IM_COL32(30, 30, 30, alpha);
    if (stage >= 2)
    {
        // Diagonal crack
        draw->AddLine(screenPts[0], screenPts[2], lineCol, 1.5f);
    }
    if (stage >= 4)
    {
        // Counter-diagonal
        draw->AddLine(screenPts[1], screenPts[3], lineCol, 1.5f);
    }
    if (stage >= 6)
    {
        // Horizontal midline
        ImVec2 mid01{(screenPts[0].x + screenPts[1].x) * 0.5f, (screenPts[0].y + screenPts[1].y) * 0.5f};
        ImVec2 mid23{(screenPts[2].x + screenPts[3].x) * 0.5f, (screenPts[2].y + screenPts[3].y) * 0.5f};
        draw->AddLine(mid01, mid23, lineCol, 1.5f);
    }
    if (stage >= 8)
    {
        // Vertical midline
        ImVec2 mid03{(screenPts[0].x + screenPts[3].x) * 0.5f, (screenPts[0].y + screenPts[3].y) * 0.5f};
        ImVec2 mid12{(screenPts[1].x + screenPts[2].x) * 0.5f, (screenPts[1].y + screenPts[2].y) * 0.5f};
        draw->AddLine(mid03, mid12, lineCol, 1.5f);
    }
}

void GameApp::drawPostEffectTint()
{
    // Check block at camera eye position
    glm::dvec3 eyePos = m_flyMode ? glm::dvec3(m_camera.getPosition()) : m_player.getEyePosition();
    glm::ivec3 blockPos = glm::ivec3(
        static_cast<int>(std::floor(eyePos.x)),
        static_cast<int>(std::floor(eyePos.y)),
        static_cast<int>(std::floor(eyePos.z)));

    uint16_t blockId = m_chunkManager.getBlock(blockPos);
    if (blockId == voxel::world::BLOCK_AIR)
    {
        return;
    }

    const auto& def = m_blockRegistry.getBlockType(blockId);
    if (def.postEffectColor == 0)
    {
        return;
    }

    // Extract RGBA from packed uint32_t (RRGGBBAA)
    uint8_t r = static_cast<uint8_t>((def.postEffectColor >> 24) & 0xFF);
    uint8_t g = static_cast<uint8_t>((def.postEffectColor >> 16) & 0xFF);
    uint8_t b = static_cast<uint8_t>((def.postEffectColor >> 8) & 0xFF);
    uint8_t a = static_cast<uint8_t>(def.postEffectColor & 0xFF);

    // If alpha is 0, use a default semi-transparent value
    if (a == 0)
    {
        a = 100;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), vpSize, IM_COL32(r, g, b, a));
}

void GameApp::drawWieldMesh()
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;

    // Wield position: bottom-right of screen
    constexpr float WIELD_SIZE = 64.0f;
    constexpr float WIELD_MARGIN = 30.0f;
    float baseX = vpSize.x - WIELD_SIZE - WIELD_MARGIN;
    float baseY = vpSize.y - WIELD_SIZE - 80.0f; // Above hotbar

    // Apply animation offsets
    float totalTime = static_cast<float>(glfwGetTime());
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    switch (m_wieldAnim.animType)
    {
    case voxel::renderer::WieldAnimType::Idle:
        offsetY = m_wieldAnim.getIdleBobY(totalTime);
        break;
    case voxel::renderer::WieldAnimType::Mining:
    {
        // Swing rotation synced with mining progress
        const auto& ms = m_player.getMiningState();
        float swing = std::sin(ms.progress * 3.14159f * 6.0f) * 8.0f;
        offsetX = swing;
        offsetY = std::abs(swing) * 0.5f;
        break;
    }
    case voxel::renderer::WieldAnimType::Place:
    {
        // Forward thrust: move up and slightly forward
        float progress = m_wieldAnim.getProgress();
        float thrust = std::sin(progress * 3.14159f) * 15.0f;
        offsetY = -thrust;
        break;
    }
    case voxel::renderer::WieldAnimType::Switch:
    {
        // Drop-down then rise-up
        float progress = m_wieldAnim.getProgress();
        if (progress < 0.5f)
        {
            offsetY = progress * 2.0f * WIELD_SIZE; // Drop down
        }
        else
        {
            offsetY = (1.0f - progress) * 2.0f * WIELD_SIZE; // Rise up
        }
        break;
    }
    }

    float x = baseX + offsetX;
    float y = baseY + offsetY;

    // Draw a 3D-ish block representation
    ImVec2 p0(x, y);
    ImVec2 p1(x + WIELD_SIZE, y + WIELD_SIZE);

    // Block face (front)
    draw->AddRectFilled(p0, p1, IM_COL32(140, 140, 140, 220), 2.0f);

    // Top face (lighter, perspective illusion)
    float topH = WIELD_SIZE * 0.3f;
    ImVec2 topPts[4] = {
        ImVec2(x, y),
        ImVec2(x + WIELD_SIZE * 0.2f, y - topH),
        ImVec2(x + WIELD_SIZE + WIELD_SIZE * 0.2f, y - topH),
        ImVec2(x + WIELD_SIZE, y),
    };
    draw->AddQuadFilled(topPts[0], topPts[1], topPts[2], topPts[3], IM_COL32(180, 180, 180, 220));

    // Right face (darker)
    ImVec2 rightPts[4] = {
        ImVec2(x + WIELD_SIZE, y),
        ImVec2(x + WIELD_SIZE + WIELD_SIZE * 0.2f, y - topH),
        ImVec2(x + WIELD_SIZE + WIELD_SIZE * 0.2f, y + WIELD_SIZE - topH),
        ImVec2(x + WIELD_SIZE, y + WIELD_SIZE),
    };
    draw->AddQuadFilled(rightPts[0], rightPts[1], rightPts[2], rightPts[3], IM_COL32(100, 100, 100, 220));

    // Block name label
    const char* blockName = HOTBAR_BLOCK_NAMES[m_hotbarSlot];
    ImVec2 textSize = ImGui::CalcTextSize(blockName);
    float textX = x + (WIELD_SIZE - textSize.x) * 0.5f;
    float textY = y + (WIELD_SIZE - textSize.y) * 0.5f;
    draw->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 200), blockName);

    // Outline
    draw->AddRect(p0, p1, IM_COL32(60, 60, 60, 200), 2.0f, 0, 1.0f);
}

void GameApp::toggleFullscreen()
{
    GLFWwindow* handle = m_window.getHandle();

    if (m_config.isFullscreen())
    {
        // Currently borderless fullscreen → go windowed
        int w = m_config.getWindowWidth();
        int h = m_config.getWindowHeight();
        glfwSetWindowAttrib(handle, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(handle, nullptr, 100, 100, w, h, GLFW_DONT_CARE);
        m_config.setFullscreen(false);
    }
    else
    {
        // Currently windowed → go borderless fullscreen
        int w = 0;
        int h = 0;
        m_window.getFramebufferSize(w, h);
        if (w > 0 && h > 0)
        {
            m_config.setWindowWidth(w);
            m_config.setWindowHeight(h);
        }

        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        glfwSetWindowAttrib(handle, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(handle, nullptr, 0, 0, mode->width, mode->height, GLFW_DONT_CARE);
        m_config.setFullscreen(true);
    }
}

void GameApp::captureScreenshot()
{
    std::filesystem::create_directories("screenshots");

    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "screenshots/screenshot_%Y-%m-%d_%H-%M-%S.png", &tm);

    m_renderer.requestScreenshot(std::string(buf));
    VX_LOG_INFO("[F2] Screenshot queued: {}", buf);
}

void GameApp::render(double /*alpha*/)
{
    // FPS tracking
    double now = glfwGetTime();
    if (m_lastFrameTime < 0.0)
    {
        m_lastFrameTime = now;
    }
    m_fpsTimer += now - m_lastFrameTime;
    m_lastFrameTime = now;
    ++m_fpsCount;
    if (m_fpsTimer >= 1.0)
    {
        m_displayFps = m_fpsCount;
        m_fpsCount = 0;
        m_fpsTimer -= 1.0;
    }

    if (m_renderer.beginFrame(m_window, m_overlayState))
    {
        // Upload completed meshes to gigabuffer via staging buffer.
        // MUST happen after beginFrame() (which resets staging state) and before
        // renderChunks() (which draws from gigabuffer). endFrame() flushes the
        // staged transfers before submitting the graphics command buffer.
        if (m_uploadManager)
        {
            m_uploadManager->processUploads(m_chunkManager, m_camera.getPosition());
            m_uploadManager->processDeferredFrees();
        }

        // Raycast from camera every frame for responsive targeting
        {
            glm::vec3 origin = glm::vec3(m_camera.getPosition()); // dvec3 -> vec3 is fine for 6-block range
            glm::vec3 dir = m_camera.getForward();
            m_raycastResult =
                voxel::physics::raycast(origin, dir, voxel::physics::MAX_REACH, m_chunkManager, m_blockRegistry);
        }

        // Render chunk sections via GPU-driven compute culling + indirect draw
        {
            glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();
            auto frustumPlanes = m_camera.extractFrustumPlanes();
            m_renderer.renderChunksIndirect(vp, frustumPlanes);
        }

        // Post-effect tint (head inside water/lava)
        drawPostEffectTint();

        buildDebugOverlay();
        if (m_input->isCursorCaptured())
        {
            drawCrosshair();
            drawBlockHighlight();
            drawCrackOverlay();
        }
        drawHotbar();
        drawWieldMesh();
        m_renderer.endFrame();
    }
}
