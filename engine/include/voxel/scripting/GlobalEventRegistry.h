#pragma once

#include "voxel/core/Log.h"

#include <sol/forward.hpp>
#include <sol/protected_function.hpp>
#include <sol/protected_function_result.hpp>
#include <sol/types.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace voxel::scripting
{

/// Stores and dispatches Lua callbacks for engine-wide named events.
/// Unlike per-block callbacks (BlockCallbacks / BlockCallbackInvoker),
/// this handles global events any mod can subscribe to via voxel.on().
///
/// Thread model: main-thread only — no mutex.
class GlobalEventRegistry
{
public:
    /// Register a callback for a named event.
    /// Logs a warning if eventName is not in the known event set.
    void registerCallback(const std::string& eventName, sol::protected_function callback);

    /// Fire a non-cancelable event. All callbacks run; errors are logged.
    template <typename... Args>
    void fireEvent(const std::string& eventName, Args&&... args);

    /// Fire a cancelable event. Returns true if any callback returned false.
    /// All callbacks still fire even after cancellation — first false wins.
    template <typename... Args>
    [[nodiscard]] bool fireCancelableEvent(const std::string& eventName, Args&&... args);

    /// Remove all registered callbacks (for hot-reload).
    void clearAll();

    /// Number of callbacks registered for a given event.
    [[nodiscard]] size_t callbackCount(const std::string& eventName) const;

private:
    std::unordered_map<std::string, std::vector<sol::protected_function>> m_callbacks;

    /// Set of valid event names. Log warning on unknown event registration.
    static const std::unordered_set<std::string> VALID_EVENTS;
};

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template <typename... Args>
void GlobalEventRegistry::fireEvent(const std::string& eventName, Args&&... args)
{
    auto it = m_callbacks.find(eventName);
    if (it == m_callbacks.end())
    {
        return;
    }
    for (auto& callback : it->second)
    {
        sol::protected_function_result result = callback(std::forward<Args>(args)...);
        if (!result.valid())
        {
            sol::error err = result;
            VX_LOG_WARN("Lua global event '{}' callback error: {}", eventName, err.what());
        }
    }
}

template <typename... Args>
bool GlobalEventRegistry::fireCancelableEvent(const std::string& eventName, Args&&... args)
{
    bool cancelled = false;
    auto it = m_callbacks.find(eventName);
    if (it == m_callbacks.end())
    {
        return false;
    }
    for (auto& callback : it->second)
    {
        sol::protected_function_result result = callback(std::forward<Args>(args)...);
        if (!result.valid())
        {
            sol::error err = result;
            VX_LOG_WARN("Lua global event '{}' callback error: {}", eventName, err.what());
            continue;
        }
        if (result.get_type() == sol::type::boolean && result.get<bool>() == false)
        {
            cancelled = true;
        }
    }
    return cancelled;
}

} // namespace voxel::scripting
