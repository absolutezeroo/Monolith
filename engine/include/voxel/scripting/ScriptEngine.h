#pragma once

#include "voxel/core/Result.h"

#include <sol/forward.hpp>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace voxel::scripting
{

/// Embedded Lua VM with sandboxed execution via sol2 + LuaJIT.
/// Manages script loading, function calls, and filesystem restrictions.
class ScriptEngine
{
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;
    ScriptEngine(ScriptEngine&&) = delete;
    ScriptEngine& operator=(ScriptEngine&&) = delete;

    /// Initialize the Lua VM with safe libraries and sandbox.
    [[nodiscard]] core::Result<void> init();

    /// Shutdown and release all Lua resources.
    void shutdown();

    /// Load and execute a Lua script file. Path must be within allowed directories.
    [[nodiscard]] core::Result<void> loadScript(const std::filesystem::path& scriptPath);

    /// Call a global Lua function by name (zero-arg, V1).
    [[nodiscard]] core::Result<void> callFunction(std::string_view functionName);

    /// Get the raw sol::state (for future stories to bind APIs and for tests).
    /// Caller must include <sol/sol.hpp> to use this.
    [[nodiscard]] sol::state& getLuaState();

    /// Add an allowed script directory.
    void addAllowedPath(const std::filesystem::path& path);

    /// Check if the engine has been initialized.
    [[nodiscard]] bool isInitialized() const;

private:
    /// Validate that a script path is within the sandbox.
    [[nodiscard]] bool isPathAllowed(const std::filesystem::path& canonicalPath) const;

    /// Open only safe standard libraries.
    void openSafeLibraries();

    /// Remove dangerous globals from the environment.
    void removeDangerousGlobals();

    std::unique_ptr<sol::state> m_lua;
    std::vector<std::filesystem::path> m_allowedPaths;
    bool m_isInitialized = false;
};

} // namespace voxel::scripting
