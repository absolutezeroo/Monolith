#include "GameApp.h"

#include "voxel/game/Window.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

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
    // Set up GLFW callbacks BEFORE ImGui init (ImGui chains on top)
    setupInputCallbacks();

    auto result = m_renderer.init(shaderDir, m_window);
    if (!result.has_value())
    {
        return result;
    }

    // Start with cursor captured for FPS camera
    glfwSetInputMode(m_window.getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(m_window.getHandle(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    return {};
}

void GameApp::setupInputCallbacks()
{
    // Override Window's user pointer with GameApp — we handle all callbacks.
    glfwSetWindowUserPointer(m_window.getHandle(), this);
    glfwSetKeyCallback(m_window.getHandle(), &GameApp::keyCallback);
    glfwSetCursorPosCallback(m_window.getHandle(), &GameApp::cursorPosCallback);
    glfwSetMouseButtonCallback(m_window.getHandle(), &GameApp::mouseButtonCallback);

    // Re-register framebuffer resize callback to forward to our Window reference
    glfwSetFramebufferSizeCallback(m_window.getHandle(), [](GLFWwindow* w, int /*width*/, int /*height*/) {
        auto* app = static_cast<GameApp*>(glfwGetWindowUserPointer(w));
        if (app)
        {
            app->m_window.setResized();
        }
    });
}

void GameApp::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/)
{
    auto* app = static_cast<GameApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    if (key >= 0 && key < static_cast<int>(app->m_keyStates.size()))
    {
        if (action == GLFW_PRESS)
        {
            app->m_keyStates[static_cast<size_t>(key)] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            app->m_keyStates[static_cast<size_t>(key)] = false;
        }
    }

    // Toggle keys on press only
    if (action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_F3:
            app->m_overlayState.showOverlay = !app->m_overlayState.showOverlay;
            break;
        case GLFW_KEY_F4:
            app->m_overlayState.wireframeMode = !app->m_overlayState.wireframeMode;
            break;
        case GLFW_KEY_F5:
            app->m_overlayState.showChunkBorders = !app->m_overlayState.showChunkBorders;
            break;
        case GLFW_KEY_ESCAPE:
            app->m_cursorCaptured = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        default:
            break;
        }
    }
}

void GameApp::cursorPosCallback(GLFWwindow* w, double xpos, double ypos)
{
    auto* app = static_cast<GameApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    if (app->m_firstMouse)
    {
        app->m_lastCursorX = xpos;
        app->m_lastCursorY = ypos;
        app->m_firstMouse = false;
        return;
    }

    if (app->m_cursorCaptured)
    {
        app->m_mouseDeltaX += static_cast<float>(xpos - app->m_lastCursorX);
        app->m_mouseDeltaY += static_cast<float>(ypos - app->m_lastCursorY);
    }

    app->m_lastCursorX = xpos;
    app->m_lastCursorY = ypos;
}

void GameApp::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/)
{
    // Let ImGui handle clicks when it wants mouse
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    auto* app = static_cast<GameApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !app->m_cursorCaptured)
    {
        app->m_cursorCaptured = true;
        app->m_firstMouse = true;
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void GameApp::tick(double dt)
{
    ImGuiIO& io = ImGui::GetIO();

    // Camera mouse look — only when cursor is captured and ImGui doesn't want input
    if (m_cursorCaptured && !io.WantCaptureMouse)
    {
        m_camera.processMouseDelta(m_mouseDeltaX, m_mouseDeltaY);
    }
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;

    // Camera movement — only when ImGui doesn't want keyboard
    if (!io.WantCaptureKeyboard)
    {
        m_camera.update(
            static_cast<float>(dt),
            m_keyStates[GLFW_KEY_W],
            m_keyStates[GLFW_KEY_S],
            m_keyStates[GLFW_KEY_A],
            m_keyStates[GLFW_KEY_D],
            m_keyStates[GLFW_KEY_SPACE],
            m_keyStates[GLFW_KEY_LEFT_SHIFT]);
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
}

void GameApp::render(double /*alpha*/)
{
    m_renderer.draw(m_window, m_camera, m_overlayState);
}
