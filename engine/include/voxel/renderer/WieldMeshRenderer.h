#pragma once

#include <cmath>
#include <cstdint>

namespace voxel::renderer
{

/// Animation types for the wield (held item) mesh.
enum class WieldAnimType : uint8_t
{
    Idle,
    Mining,
    Place,
    Switch
};

/// Animation state for the wield mesh display.
struct WieldAnimState
{
    WieldAnimType animType = WieldAnimType::Idle;
    float timer = 0.0f;
    int prevSlot = 0;

    /// Update animation timers. Returns true if animation is complete.
    bool update(float dt)
    {
        timer += dt;
        switch (animType)
        {
        case WieldAnimType::Idle:
            return false; // Idle loops forever
        case WieldAnimType::Mining:
            return false; // Mining loops while active
        case WieldAnimType::Place:
            return timer >= 0.2f; // 0.2s forward thrust
        case WieldAnimType::Switch:
            return timer >= 0.3f; // 0.15s drop + 0.15s rise
        }
        return true;
    }

    /// Start a new animation (resets timer).
    void startAnim(WieldAnimType type)
    {
        animType = type;
        timer = 0.0f;
    }

    /// Check if the current one-shot animation has finished (without advancing time).
    [[nodiscard]] bool isComplete() const
    {
        switch (animType)
        {
        case WieldAnimType::Idle:
        case WieldAnimType::Mining:
            return false; // Looping animations never "complete"
        case WieldAnimType::Place:
            return timer >= 0.2f;
        case WieldAnimType::Switch:
            return timer >= 0.3f;
        }
        return true;
    }

    /// Get Y offset for idle bob animation.
    [[nodiscard]] float getIdleBobY(float totalTime) const
    {
        // Sinusoidal bob at 0.5Hz, 3 pixel amplitude
        return 3.0f * std::sin(totalTime * 3.14159f);
    }

    /// Get animation progress [0, 1] for current animation.
    [[nodiscard]] float getProgress() const
    {
        switch (animType)
        {
        case WieldAnimType::Place:
            return timer / 0.2f;
        case WieldAnimType::Switch:
            return timer / 0.3f;
        default:
            return 0.0f;
        }
    }
};

} // namespace voxel::renderer
