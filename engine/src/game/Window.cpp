#include "voxel/game/Window.h"

#include "voxel/core/Log.h"

#include <GLFW/glfw3.h>

namespace voxel::game
{

Window::Window(GLFWwindow* window)
    : m_window(window)
{
}

Window::~Window()
{
    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

core::Result<std::unique_ptr<Window>> Window::create(int width, int height, const char* title)
{
    glfwSetErrorCallback([](int code, const char* description) {
        VX_LOG_ERROR("GLFW error {}: {}", code, description);
    });

    if (glfwInit() == GLFW_FALSE)
    {
        VX_LOG_ERROR("Failed to initialize GLFW");
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to initialize GLFW"});
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (handle == nullptr)
    {
        VX_LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return std::unexpected(core::EngineError{core::ErrorCode::VulkanError, "Failed to create GLFW window"});
    }

    auto window = std::unique_ptr<Window>(new Window(handle));

    // Set user pointer to Window for the default framebuffer resize callback.
    // GameApp may override this with its own user pointer — that's fine,
    // as long as it calls Window::setResized() from its own callback.
    glfwSetWindowUserPointer(handle, window.get());
    glfwSetFramebufferSizeCallback(handle, framebufferSizeCallback);

    VX_LOG_INFO("Window created: {}x{} \"{}\"", width, height, title);

    return window;
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_window) != 0;
}

void Window::pollEvents()
{
    glfwPollEvents();
}

GLFWwindow* Window::getHandle() const
{
    return m_window;
}

void Window::getFramebufferSize(int& width, int& height) const
{
    glfwGetFramebufferSize(m_window, &width, &height);
}

bool Window::isMinimized() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return width == 0 || height == 0;
}

bool Window::wasResized()
{
    bool resized = m_framebufferResized;
    m_framebufferResized = false;
    return resized;
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->m_framebufferResized = true;
        VX_LOG_DEBUG("Framebuffer resized: {}x{}", width, height);
    }
}

} // namespace voxel::game
