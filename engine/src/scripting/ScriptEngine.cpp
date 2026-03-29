#include "voxel/scripting/ScriptEngine.h"

#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"

#include <sol/sol.hpp>

#include <algorithm>

namespace voxel::scripting
{

// Custom panic handler — called when an unrecoverable Lua error occurs.
// Without exceptions, a Lua panic would call abort() with no context.
// This handler logs the error message before aborting via VX_FATAL.
static int customPanic(lua_State* L)
{
    const char* msg = lua_tostring(L, -1);
    VX_LOG_CRITICAL("Lua PANIC: {}", msg ? msg : "unknown error");
    VX_FATAL("Unrecoverable Lua error");
    return 0; // Never reached
}

ScriptEngine::ScriptEngine() = default;

ScriptEngine::~ScriptEngine()
{
    if (m_isInitialized)
    {
        shutdown();
    }
}

core::Result<void> ScriptEngine::init()
{
    if (m_isInitialized)
    {
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidArgument, "ScriptEngine already initialized"});
    }

    m_lua = std::make_unique<sol::state>();

    // Set custom panic handler before anything else
    lua_atpanic(m_lua->lua_state(), customPanic);

    openSafeLibraries();
    removeDangerousGlobals();

    // Create empty voxel API table for future stories (9.2+) to populate
    m_lua->create_named_table("voxel");

    m_isInitialized = true;
    VX_LOG_INFO("ScriptEngine initialized (sol2 + LuaJIT)");

    return {};
}

void ScriptEngine::shutdown()
{
    if (!m_isInitialized)
    {
        return;
    }

    m_lua.reset();
    m_allowedPaths.clear();
    m_isInitialized = false;
    VX_LOG_INFO("ScriptEngine shut down");
}

void ScriptEngine::openSafeLibraries()
{
    VX_ASSERT(m_lua != nullptr, "Lua state must exist before opening libraries");

    m_lua->open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::coroutine);
}

void ScriptEngine::removeDangerousGlobals()
{
    VX_ASSERT(m_lua != nullptr, "Lua state must exist before removing globals");

    auto& lua = *m_lua;
    lua["os"] = sol::lua_nil;
    lua["io"] = sol::lua_nil;
    lua["debug"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["dofile"] = sol::lua_nil;
    lua["load"] = sol::lua_nil;
    lua["package"] = sol::lua_nil;
    lua["require"] = sol::lua_nil;
    lua["collectgarbage"] = sol::lua_nil;
}

core::Result<void> ScriptEngine::loadScript(const std::filesystem::path& scriptPath)
{
    if (!m_isInitialized)
    {
        return std::unexpected(core::EngineError{core::ErrorCode::InvalidArgument, "ScriptEngine not initialized"});
    }

    // Resolve path (handles .. and symlinks without requiring existence of intermediate dirs)
    std::error_code ec;
    auto canonicalPath = std::filesystem::weakly_canonical(scriptPath, ec);
    if (ec)
    {
        return std::unexpected(core::EngineError{
            core::ErrorCode::InvalidArgument,
            "Failed to resolve script path: " + scriptPath.string() + " (" + ec.message() + ")"});
    }

    // Filesystem sandbox: verify path is within an allowed directory
    if (!m_allowedPaths.empty() && !isPathAllowed(canonicalPath))
    {
        VX_LOG_WARN("Script path outside sandbox: {}", scriptPath.string());
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidArgument, "Script path outside sandbox: " + scriptPath.string()});
    }

    // Check file existence
    if (!std::filesystem::exists(canonicalPath, ec))
    {
        return std::unexpected(core::EngineError::file(scriptPath.string()));
    }

    // Load and execute the script with error handling
    auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
        return pfr;
    };

    sol::protected_function_result result = m_lua->safe_script_file(canonicalPath.string(), errorHandler);
    if (!result.valid())
    {
        sol::error err = result;
        std::string errorMsg = "Failed to load '" + scriptPath.string() + "': " + err.what();
        VX_LOG_ERROR("{}", errorMsg);
        return std::unexpected(core::EngineError{core::ErrorCode::ScriptError, std::move(errorMsg)});
    }

    VX_LOG_DEBUG("Loaded script: {}", scriptPath.string());
    return {};
}

core::Result<void> ScriptEngine::callFunction(std::string_view functionName)
{
    if (!m_isInitialized)
    {
        return std::unexpected(
            core::EngineError{core::ErrorCode::InvalidArgument, "ScriptEngine not initialized"});
    }

    sol::protected_function func = (*m_lua)[std::string(functionName)];
    if (!func.valid())
    {
        std::string errorMsg = "Function not found: " + std::string(functionName);
        VX_LOG_ERROR("{}", errorMsg);
        return std::unexpected(core::EngineError{core::ErrorCode::ScriptError, std::move(errorMsg)});
    }

    sol::protected_function_result result = func();
    if (!result.valid())
    {
        sol::error err = result;
        std::string errorMsg =
            "Error calling '" + std::string(functionName) + "': " + err.what();
        VX_LOG_ERROR("{}", errorMsg);
        return std::unexpected(core::EngineError{core::ErrorCode::ScriptError, std::move(errorMsg)});
    }

    return {};
}

sol::state& ScriptEngine::getLuaState()
{
    VX_ASSERT(m_lua != nullptr, "Cannot get Lua state before initialization");
    return *m_lua;
}

void ScriptEngine::addAllowedPath(const std::filesystem::path& path)
{
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec)
    {
        m_allowedPaths.push_back(std::move(canonical));
    }
    else
    {
        // If canonicalization fails, store the path as-is (best effort)
        m_allowedPaths.push_back(path);
    }
}

bool ScriptEngine::isInitialized() const
{
    return m_isInitialized;
}

bool ScriptEngine::isPathAllowed(const std::filesystem::path& canonicalPath) const
{
    for (const auto& allowed : m_allowedPaths)
    {
        // Check if canonicalPath starts with the allowed prefix
        auto mismatch = std::mismatch(allowed.begin(), allowed.end(), canonicalPath.begin(), canonicalPath.end());
        if (mismatch.first == allowed.end())
        {
            return true;
        }
    }
    return false;
}

} // namespace voxel::scripting
