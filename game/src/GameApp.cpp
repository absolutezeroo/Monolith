#include "GameApp.h"

#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/VulkanContext.h"

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

    // Register basic terrain blocks
    using voxel::world::BlockDefinition;
    std::string blocksPath = std::string(VX_ASSETS_DIR) + "/scripts/base/blocks.json";
    auto loadBlocksResult = m_blockRegistry.loadFromJson(blocksPath);
    if (!loadBlocksResult.has_value())
    {
        return std::unexpected(loadBlocksResult.error());
    }

    // Create WorldGenerator
    m_worldGen =
        std::make_unique<voxel::world::WorldGenerator>(static_cast<uint64_t>(m_config.getSeed()), m_blockRegistry);

    // Inject WorldGenerator into ChunkManager
    m_chunkManager.setWorldGenerator(m_worldGen.get());

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
                case voxel::game::CommandType::ToggleSprint:
                {
                    auto& payload = std::get<voxel::game::ToggleSprintPayload>(cmd.payload);
                    moveInput.sprint = payload.enabled;
                    break;
                }
                default:
                    break;
                }
            });

            m_player.tickPhysics(fdt, moveInput, m_chunkManager, m_blockRegistry);
            m_camera.setPosition(m_player.getEyePosition());
        }
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

        // Render chunk sections via GPU-driven compute culling + indirect draw
        {
            glm::mat4 vp = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();
            auto frustumPlanes = m_camera.extractFrustumPlanes();
            m_renderer.renderChunksIndirect(vp, frustumPlanes);
        }

        buildDebugOverlay();
        if (m_input->isCursorCaptured())
        {
            drawCrosshair();
        }
        drawHotbar();
        m_renderer.endFrame();
    }
}
