#include "voxel/input/InputManager.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace voxel::input
{

InputManager::InputManager(GLFWwindow* window) : m_window(window)
{
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, &InputManager::keyCallback);
    glfwSetCursorPosCallback(window, &InputManager::cursorPosCallback);
    glfwSetMouseButtonCallback(window, &InputManager::mouseButtonCallback);
}

void InputManager::update(float dt)
{
    m_totalTime += dt;

    // Clear edge flags
    m_keyPressed.fill(false);
    m_keyReleased.fill(false);
    m_keyDoubleTapped.fill(false);
    m_mousePressed.fill(false);
    m_mouseReleased.fill(false);

    // Update hold timers for held keys
    for (int i = 0; i < MAX_KEYS; ++i)
    {
        if (m_keyDown[static_cast<size_t>(i)])
        {
            m_keyHoldTime[static_cast<size_t>(i)] += dt;
        }
    }

    for (int i = 0; i < MAX_MOUSE_BUTTONS; ++i)
    {
        if (m_mouseDown[static_cast<size_t>(i)])
        {
            m_mouseHoldTime[static_cast<size_t>(i)] += dt;
        }
    }

    // Clear mouse delta (accumulated from callbacks since last update)
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
}

// --- Key queries ---

bool InputManager::isKeyDown(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_keyDown[static_cast<size_t>(key)];
}

bool InputManager::wasKeyPressed(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_keyPressed[static_cast<size_t>(key)];
}

bool InputManager::wasKeyReleased(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_keyReleased[static_cast<size_t>(key)];
}

float InputManager::getKeyHoldDuration(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return 0.0f;
    return m_keyHoldTime[static_cast<size_t>(key)];
}

bool InputManager::wasKeyDoubleTapped(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_keyDoubleTapped[static_cast<size_t>(key)];
}

// --- Mouse button queries ---

glm::vec2 InputManager::getMouseDelta() const
{
    return {m_mouseDeltaX, m_mouseDeltaY};
}

bool InputManager::isMouseButtonDown(int button) const
{
    if (button < 0 || button >= MAX_MOUSE_BUTTONS)
        return false;
    return m_mouseDown[static_cast<size_t>(button)];
}

bool InputManager::wasMouseButtonPressed(int button) const
{
    if (button < 0 || button >= MAX_MOUSE_BUTTONS)
        return false;
    return m_mousePressed[static_cast<size_t>(button)];
}

bool InputManager::wasMouseButtonReleased(int button) const
{
    if (button < 0 || button >= MAX_MOUSE_BUTTONS)
        return false;
    return m_mouseReleased[static_cast<size_t>(button)];
}

float InputManager::getMouseButtonHoldDuration(int button) const
{
    if (button < 0 || button >= MAX_MOUSE_BUTTONS)
        return 0.0f;
    return m_mouseHoldTime[static_cast<size_t>(button)];
}

// --- Cursor capture ---

void InputManager::setCursorCaptured(bool captured)
{
    m_cursorCaptured = captured;
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured)
    {
        m_firstMouse = true;
    }
}

bool InputManager::isCursorCaptured() const
{
    return m_cursorCaptured;
}

// --- GLFW Callbacks ---

void InputManager::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/)
{
    auto* mgr = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (!mgr)
        return;
    if (key < 0 || key >= MAX_KEYS)
        return;

    auto idx = static_cast<size_t>(key);

    if (action == GLFW_PRESS)
    {
        mgr->m_keyDown[idx] = true;
        mgr->m_keyPressed[idx] = true;
        mgr->m_keyHoldTime[idx] = 0.0f;

        // Double-tap detection
        float timeSinceLast = mgr->m_totalTime - mgr->m_keyLastPressTime[idx];
        if (timeSinceLast < DOUBLE_TAP_INTERVAL)
        {
            mgr->m_keyDoubleTapped[idx] = true;
        }
        mgr->m_keyLastPressTime[idx] = mgr->m_totalTime;
    }
    else if (action == GLFW_RELEASE)
    {
        mgr->m_keyDown[idx] = false;
        mgr->m_keyReleased[idx] = true;
        mgr->m_keyHoldTime[idx] = 0.0f;
    }
}

void InputManager::cursorPosCallback(GLFWwindow* w, double xpos, double ypos)
{
    auto* mgr = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (!mgr)
        return;

    if (mgr->m_firstMouse)
    {
        mgr->m_lastCursorX = xpos;
        mgr->m_lastCursorY = ypos;
        mgr->m_firstMouse = false;
        return;
    }

    if (mgr->m_cursorCaptured)
    {
        mgr->m_mouseDeltaX += static_cast<float>(xpos - mgr->m_lastCursorX);
        mgr->m_mouseDeltaY += static_cast<float>(ypos - mgr->m_lastCursorY);
    }

    mgr->m_lastCursorX = xpos;
    mgr->m_lastCursorY = ypos;
}

void InputManager::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/)
{
    auto* mgr = static_cast<InputManager*>(glfwGetWindowUserPointer(w));
    if (!mgr)
        return;
    if (button < 0 || button >= MAX_MOUSE_BUTTONS)
        return;

    auto idx = static_cast<size_t>(button);

    if (action == GLFW_PRESS)
    {
        mgr->m_mouseDown[idx] = true;
        mgr->m_mousePressed[idx] = true;
        mgr->m_mouseHoldTime[idx] = 0.0f;
    }
    else if (action == GLFW_RELEASE)
    {
        mgr->m_mouseDown[idx] = false;
        mgr->m_mouseReleased[idx] = true;
        mgr->m_mouseHoldTime[idx] = 0.0f;
    }
}

} // namespace voxel::input
