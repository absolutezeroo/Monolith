#include "GameApp.h"

#include "voxel/core/Log.h"
#include "voxel/game/Window.h"
#include "voxel/renderer/VulkanContext.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

static constexpr int HOTBAR_SLOTS = 9;
static constexpr float CROSSHAIR_SIZE = 12.0f;
static constexpr float CROSSHAIR_THICKNESS = 2.0f;
static constexpr float HOTBAR_SLOT_SIZE = 48.0f;
static constexpr float HOTBAR_PADDING = 4.0f;

GameApp::GameApp(voxel::game::Window& window, voxel::renderer::VulkanContext& vulkanContext)
    : GameLoop(window), m_window(window), m_renderer(vulkanContext)
{
}

GameApp::~GameApp()
{
    // Save config on exit
    if (!m_configPath.empty())
    {
        m_config.setFov(m_camera.getFov());
        m_config.setSensitivity(m_camera.getSensitivity());
        m_config.save(m_configPath);
    }
}

voxel::core::Result<void> GameApp::init(const std::string& shaderDir)
{
    // Load config
    m_configPath = "config.json";
    auto loadResult = m_config.load(m_configPath);
    if (!loadResult.has_value())
    {
        VX_LOG_WARN("Config load returned error — using defaults");
    }

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

    auto result = m_renderer.init(shaderDir, m_window);
    if (!result.has_value())
    {
        return result;
    }

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

    // Camera movement — only when ImGui doesn't want keyboard
    if (!io.WantCaptureKeyboard)
    {
        m_camera.update(
            fdt,
            m_input->isKeyDown(GLFW_KEY_W),
            m_input->isKeyDown(GLFW_KEY_S),
            m_input->isKeyDown(GLFW_KEY_A),
            m_input->isKeyDown(GLFW_KEY_D),
            m_input->isKeyDown(GLFW_KEY_SPACE),
            m_input->isKeyDown(GLFW_KEY_LEFT_SHIFT));
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
        ImGui::Text("Chunks: No ChunkManager active");

        ImGui::Separator();

        ImGui::Text("[F4] Wireframe: %s", m_overlayState.wireframeMode ? "ON" : "OFF");
        ImGui::Text("[F5] Chunk borders: %s", m_overlayState.showChunkBorders ? "ON" : "OFF");

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

        // Background
        draw->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 180), 4.0f);

        // Border
        auto borderCol = (i == m_hotbarSlot) ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 120, 120, 180);
        float borderThickness = (i == m_hotbarSlot) ? 2.0f : 1.0f;
        draw->AddRect(p0, p1, borderCol, 4.0f, 0, borderThickness);

        // Slot number
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        draw->AddText(ImVec2(x + 4.0f, startY + 2.0f), IM_COL32(200, 200, 200, 180), label);
    }
}

void GameApp::toggleFullscreen()
{
    GLFWwindow* handle = m_window.getHandle();
    GLFWmonitor* monitor = glfwGetWindowMonitor(handle);

    if (monitor != nullptr)
    {
        // Currently fullscreen → go windowed
        int w = m_config.getWindowWidth();
        int h = m_config.getWindowHeight();
        glfwSetWindowMonitor(handle, nullptr, 100, 100, w, h, GLFW_DONT_CARE);
        m_config.setFullscreen(false);
    }
    else
    {
        // Currently windowed → go fullscreen on primary monitor
        int w = 0;
        int h = 0;
        m_window.getFramebufferSize(w, h);
        m_config.setWindowWidth(w);
        m_config.setWindowHeight(h);

        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        glfwSetWindowMonitor(handle, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
        m_config.setFullscreen(true);
    }
}

void GameApp::captureScreenshot()
{
    // Screenshot capturing requires reading back from a VkImage with TRANSFER_SRC.
    // This will be implemented when swapchain images have TRANSFER_SRC usage flag.
    // For now, just log that the key was pressed.
    VX_LOG_INFO("[F2] Screenshot requested — not yet implemented (requires TRANSFER_SRC on swapchain)");
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
        buildDebugOverlay();
        drawCrosshair();
        drawHotbar();
        m_renderer.endFrame();
    }
}
