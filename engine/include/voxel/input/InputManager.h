#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace voxel::input
{

/**
 * @brief Centralized input state manager wrapping GLFW callbacks.
 *
 * Registers GLFW key, cursor, and mouse button callbacks in the constructor.
 * Call update(dt) once per frame before reading input state.
 * Provides edge detection (pressed/released this frame) and hold duration tracking.
 */
class InputManager
{
  public:
    explicit InputManager(GLFWwindow* window);

    /// Call once per frame at the end of tick (after reading input). Clears edge flags and updates hold timers.
    void update(float dt);

    // --- Key state ---
    [[nodiscard]] bool isKeyDown(int key) const;
    [[nodiscard]] bool wasKeyPressed(int key) const;
    [[nodiscard]] bool wasKeyReleased(int key) const;
    [[nodiscard]] float getKeyHoldDuration(int key) const;
    [[nodiscard]] bool wasKeyDoubleTapped(int key) const;

    // --- Mouse state ---
    [[nodiscard]] glm::vec2 getMouseDelta() const;
    [[nodiscard]] bool isMouseButtonDown(int button) const;
    [[nodiscard]] bool wasMouseButtonPressed(int button) const;
    [[nodiscard]] bool wasMouseButtonReleased(int button) const;
    [[nodiscard]] float getMouseButtonHoldDuration(int button) const;

    // --- Cursor capture ---
    void setCursorCaptured(bool captured);
    [[nodiscard]] bool isCursorCaptured() const;

  private:
    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    static constexpr float DOUBLE_TAP_INTERVAL = 0.3f;

    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);

    GLFWwindow* m_window = nullptr;

    // Key state
    std::array<bool, MAX_KEYS> m_keyDown{};
    std::array<bool, MAX_KEYS> m_keyPressed{};
    std::array<bool, MAX_KEYS> m_keyReleased{};
    std::array<float, MAX_KEYS> m_keyHoldTime{};
    std::array<float, MAX_KEYS> m_keyLastPressTime{};
    std::array<bool, MAX_KEYS> m_keyDoubleTapped{};

    // Mouse button state
    std::array<bool, MAX_MOUSE_BUTTONS> m_mouseDown{};
    std::array<bool, MAX_MOUSE_BUTTONS> m_mousePressed{};
    std::array<bool, MAX_MOUSE_BUTTONS> m_mouseReleased{};
    std::array<float, MAX_MOUSE_BUTTONS> m_mouseHoldTime{};

    // Mouse cursor
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    bool m_cursorCaptured = true;
    bool m_firstMouse = true;

    // Timing
    float m_totalTime = 0.0f;
};

} // namespace voxel::input
