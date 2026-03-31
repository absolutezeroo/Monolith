#pragma once

#include <sol/protected_function.hpp>

#include <string>
#include <vector>

namespace voxel::scripting
{

/// A registered key combo: sequence of key names with a time window.
struct ComboRegistration
{
    std::string name;
    std::vector<std::string> keys; // Sequence of key names (e.g., {"w", "w"})
    float windowSeconds;           // Max time for entire sequence
    sol::protected_function callback;
};

/// Detects multi-key sequences within a time window.
/// Tracks a ring buffer of recent key presses with timestamps;
/// on each key press, checks all registered combos for match.
class ComboDetector
{
public:
    /// Register a named combo.
    void registerCombo(
        const std::string& name,
        std::vector<std::string> keys,
        float windowSec,
        sol::protected_function callback);

    /// Called on each key press. Checks all combos for completion.
    /// @param keyNameStr The name of the key pressed.
    /// @param currentTime Elapsed game time in seconds.
    /// @param playerHandle The player entity handle to pass to callbacks.
    void onKeyPress(const std::string& keyNameStr, float currentTime, sol::object playerHandle);

    /// Remove all registered combos (for hot-reload).
    void clearAll();

    /// Number of registered combos.
    [[nodiscard]] size_t comboCount() const;

private:
    struct KeyEvent
    {
        std::string key;
        float timestamp;
    };

    std::vector<ComboRegistration> m_combos;
    std::vector<KeyEvent> m_recentKeys;
    static constexpr size_t MAX_RECENT = 16;
};

} // namespace voxel::scripting
