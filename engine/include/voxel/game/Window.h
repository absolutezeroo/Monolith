#pragma once

#include "voxel/core/Result.h"

#include <memory>

struct GLFWwindow;

namespace voxel::game
{

/**
 * @brief RAII wrapper around a GLFW window configured for Vulkan (no OpenGL context).
 *
 * Initializes GLFW on construction, terminates on destruction.
 * Only one Window should exist at a time (GLFW is process-global).
 */
class Window
{
  public:
    /**
     * @brief Creates a resizable GLFW window with no OpenGL context.
     *
     * @param width  Initial framebuffer width in pixels.
     * @param height Initial framebuffer height in pixels.
     * @param title  Window title string.
     * @return The created Window, or EngineError (ErrorCode::VulkanError) on failure.
     */
    static core::Result<std::unique_ptr<Window>> create(int width, int height, const char* title);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// Returns true when the user has requested the window to close.
    bool shouldClose() const;

    /// Polls for pending GLFW events (keyboard, mouse, resize, etc.).
    void pollEvents();

    /// Returns the underlying GLFW window handle (non-owning).
    GLFWwindow* getHandle() const;

    /// Queries the current framebuffer size in pixels.
    void getFramebufferSize(int& width, int& height) const;

    /// Returns true if the window is minimized (framebuffer size is 0x0).
    bool isMinimized() const;

    /// Returns true if the framebuffer was resized since last checked, then resets the flag.
    bool wasResized();

    /// Sets the resize flag (called from external framebuffer callbacks).
    void setResized() { m_framebufferResized = true; }

  private:
    explicit Window(GLFWwindow* window);

    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace voxel::game
