#include "GameApp.h"

#include "voxel/game/Window.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

GameApp::GameApp(voxel::game::Window& window, voxel::renderer::VulkanContext& vulkanContext)
    : GameLoop(window), m_window(window), m_renderer(vulkanContext)
{
}

voxel::core::Result<void> GameApp::init(const std::string& shaderDir)
{
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
    if (m_input->wasKeyPressed(GLFW_KEY_ESCAPE))
    {
        m_input->setCursorCaptured(false);
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

        ImGui::Text("Gigabuffer: N/A");
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
        m_renderer.endFrame();
    }
}
