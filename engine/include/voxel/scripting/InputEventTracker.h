#pragma once

#include <string_view>

namespace voxel::input
{
class InputManager;
}

namespace voxel::scripting
{

class GlobalEventRegistry;
class EntityHandle;

/// Bridges InputManager state to Lua global events via GlobalEventRegistry.
/// Each tick, polls InputManager for edge flags (pressed/released/held/double-tap)
/// and fires the corresponding Lua events.
///
/// Does NOT track state itself — reads from InputManager which already handles
/// hold durations, double-tap detection, and edge flags.
class InputEventTracker
{
public:
    /// Poll InputManager and fire input events via registry.
    /// @param input The input manager to read from.
    /// @param registry The global event registry to fire events through.
    /// @param entity The player entity handle for callbacks.
    void update(
        const input::InputManager& input,
        GlobalEventRegistry& registry,
        EntityHandle& entity);

    /// Translate GLFW key code to Lua-friendly name.
    [[nodiscard]] static std::string_view keyName(int glfwKey);

    /// Translate GLFW mouse button to Lua-friendly name.
    [[nodiscard]] static std::string_view buttonName(int glfwButton);

private:
    static constexpr int MAX_TRACKED_KEYS = 512;
    static constexpr int MAX_TRACKED_BUTTONS = 8;
};

} // namespace voxel::scripting
