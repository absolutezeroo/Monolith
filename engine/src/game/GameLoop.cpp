#include "voxel/game/GameLoop.h"

#include "voxel/core/Log.h"
#include "voxel/game/Window.h"

#include <GLFW/glfw3.h>

namespace voxel::game
{

/// Fixed simulation rate: 20 ticks per second (50ms per tick).
static constexpr double TICK_RATE = 1.0 / 20.0;

/// Maximum frame time to prevent spiral of death (e.g., after debugger breakpoint).
static constexpr double MAX_FRAME_TIME = 0.25;

GameLoop::GameLoop(Window& window)
    : m_window(window)
{
}

void GameLoop::run()
{
    double accumulator = 0.0;
    double previousTime = glfwGetTime();
    m_fpsTimer = 0.0;
    m_frameCount = 0;

    VX_LOG_INFO("Game loop starting (tick rate: {} TPS)", static_cast<int>(1.0 / TICK_RATE));

    while (!m_window.shouldClose())
    {
        m_window.pollEvents();

        // When minimized, block until the window is restored — no CPU burn.
        if (m_window.isMinimized())
        {
            glfwWaitEvents();
            previousTime = glfwGetTime();
            continue;
        }

        double currentTime = glfwGetTime();
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        // Clamp frame time to prevent spiral of death.
        if (frameTime > MAX_FRAME_TIME)
        {
            frameTime = MAX_FRAME_TIME;
        }

        accumulator += frameTime;

        // Fixed-step simulation.
        while (accumulator >= TICK_RATE)
        {
            tick(TICK_RATE);
            accumulator -= TICK_RATE;
        }

        // Render with interpolation alpha.
        double alpha = accumulator / TICK_RATE;
        render(alpha);

        // FPS counter — log every second.
        ++m_frameCount;
        m_fpsTimer += frameTime;
        if (m_fpsTimer >= 1.0)
        {
            VX_LOG_INFO("FPS: {}", m_frameCount);
            m_frameCount = 0;
            m_fpsTimer -= 1.0;
        }
    }

    VX_LOG_INFO("Game loop ended");
}

void GameLoop::tick(double /*dt*/)
{
    // No-op placeholder — simulation systems will be added in later stories.
}

void GameLoop::render(double /*alpha*/)
{
    // No-op placeholder — Vulkan rendering will be added in Story 2.3.
}

} // namespace voxel::game
