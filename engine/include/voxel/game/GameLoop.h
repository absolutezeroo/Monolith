#pragma once

namespace voxel::game
{

class Window;

/**
 * @brief Fixed-timestep game loop with uncapped rendering.
 *
 * Simulation runs at 20 ticks/sec (50ms per tick).
 * Rendering runs as fast as possible, receiving an interpolation alpha [0,1).
 * Includes spiral-of-death protection (max 0.25s per frame).
 */
class GameLoop
{
public:
    /// Constructs a game loop bound to the given window (non-owning).
    explicit GameLoop(Window& window);

    /// Runs the main loop until the window is closed.
    void run();

protected:
    /**
     * @brief Called at a fixed rate (20 times/sec).
     * @param dt Fixed timestep in seconds (always TICK_RATE).
     */
    virtual void tick(double dt);

    /**
     * @brief Called once per frame after all ticks.
     * @param alpha Interpolation factor [0,1) between the last and next tick.
     */
    virtual void render(double alpha);

private:
    Window& m_window;

    int m_frameCount = 0;
    double m_fpsTimer = 0.0;
};

} // namespace voxel::game
