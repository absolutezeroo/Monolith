#include "voxel/scripting/ScriptEngine.h"

#include "voxel/core/Log.h"

#include <sol/sol.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace voxel::scripting;

// Resolve path to test scripts directory using the VOXELFORGE_ASSETS_DIR compile definition.
// Test scripts are in tests/scripting/test_scripts/ (relative to project root).
// VOXELFORGE_ASSETS_DIR points to <project-root>/assets, so we go up one level.
static std::filesystem::path getTestScriptsDir()
{
    std::filesystem::path assetsDir(VOXELFORGE_ASSETS_DIR);
    return assetsDir.parent_path() / "tests" / "scripting" / "test_scripts";
}

TEST_CASE("ScriptEngine", "[scripting]")
{
    voxel::core::Log::init(); // Logger must be initialized before ScriptEngine (uses VX_LOG)
    ScriptEngine engine;

    SECTION("initialization succeeds")
    {
        auto result = engine.init();
        REQUIRE(result.has_value());
        REQUIRE(engine.isInitialized());
    }

    SECTION("double initialization fails")
    {
        auto r1 = engine.init();
        REQUIRE(r1.has_value());

        auto r2 = engine.init();
        REQUIRE_FALSE(r2.has_value());
        REQUIRE(r2.error().code == voxel::core::ErrorCode::InvalidArgument);
    }

    SECTION("load and execute Lua string setting global")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto& lua = engine.getLuaState();

        // Use safe_script with error handler (no exceptions mode)
        auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
            return pfr;
        };
        auto result = lua.safe_script("test_var = 42", errorHandler);
        REQUIRE(result.valid());

        int val = lua["test_var"];
        REQUIRE(val == 42);
    }

    SECTION("load Lua file that sets a global")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto scriptsDir = getTestScriptsDir();
        engine.addAllowedPath(scriptsDir);

        auto loadResult = engine.loadScript(scriptsDir / "set_global.lua");
        REQUIRE(loadResult.has_value());

        auto& lua = engine.getLuaState();
        int val = lua["test_var"];
        REQUIRE(val == 42);
    }

    SECTION("call a Lua function from C++ and verify it was called")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto scriptsDir = getTestScriptsDir();
        engine.addAllowedPath(scriptsDir);

        auto loadResult = engine.loadScript(scriptsDir / "add_function.lua");
        REQUIRE(loadResult.has_value());

        // Use sol state directly to call with args and verify return value
        auto& lua = engine.getLuaState();
        sol::protected_function addFunc = lua["add"];
        REQUIRE(addFunc.valid());

        sol::protected_function_result result = addFunc(3, 4);
        REQUIRE(result.valid());

        int sum = result;
        REQUIRE(sum == 7);
    }

    SECTION("callFunction succeeds for defined function")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto& lua = engine.getLuaState();
        auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
            return pfr;
        };
        lua.safe_script("function greet() greeting = 'hello' end", errorHandler);

        auto callResult = engine.callFunction("greet");
        REQUIRE(callResult.has_value());

        std::string greeting = lua["greeting"];
        REQUIRE(greeting == "hello");
    }

    SECTION("sandbox blocks os.exit")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto& lua = engine.getLuaState();

        // os should be nil after sandbox
        sol::object osObj = lua["os"];
        REQUIRE(osObj.get_type() == sol::type::lua_nil);

        // Attempting to use os in script should fail
        auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
            return pfr;
        };
        auto result = lua.safe_script("os.exit(1)", errorHandler);
        REQUIRE_FALSE(result.valid());
    }

    SECTION("sandbox blocks io.open")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto& lua = engine.getLuaState();

        sol::object ioObj = lua["io"];
        REQUIRE(ioObj.get_type() == sol::type::lua_nil);

        auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
            return pfr;
        };
        auto result = lua.safe_script("io.open('test.txt')", errorHandler);
        REQUIRE_FALSE(result.valid());
    }

    SECTION("load nonexistent file returns FileNotFound")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto scriptsDir = getTestScriptsDir();
        engine.addAllowedPath(scriptsDir);

        auto result = engine.loadScript(scriptsDir / "nonexistent_script_that_does_not_exist.lua");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == voxel::core::ErrorCode::FileNotFound);
    }

    SECTION("load script with syntax error returns error with info")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto scriptsDir = getTestScriptsDir();
        engine.addAllowedPath(scriptsDir);

        auto result = engine.loadScript(scriptsDir / "syntax_error.lua");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == voxel::core::ErrorCode::ScriptError);
        // Error message should contain some reference to the script
        REQUIRE_FALSE(result.error().message.empty());
    }

    SECTION("path traversal attack rejected")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto scriptsDir = getTestScriptsDir();
        engine.addAllowedPath(scriptsDir);

        // Attempt to escape the sandbox via path traversal
        auto result = engine.loadScript(scriptsDir / ".." / ".." / ".." / "etc" / "passwd");
        REQUIRE_FALSE(result.has_value());
        // Should be rejected by sandbox (InvalidArgument) or not found (FileNotFound)
        bool isSecurityRejection = (result.error().code == voxel::core::ErrorCode::InvalidArgument)
                                   || (result.error().code == voxel::core::ErrorCode::FileNotFound);
        REQUIRE(isSecurityRejection);
    }

    SECTION("call nonexistent function returns error")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto result = engine.callFunction("this_function_does_not_exist");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == voxel::core::ErrorCode::ScriptError);
    }

    SECTION("voxel API table exists")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());

        auto& lua = engine.getLuaState();
        sol::object voxelTable = lua["voxel"];
        REQUIRE(voxelTable.get_type() == sol::type::table);
    }

    SECTION("shutdown and re-init works")
    {
        auto r1 = engine.init();
        REQUIRE(r1.has_value());
        REQUIRE(engine.isInitialized());

        engine.shutdown();
        REQUIRE_FALSE(engine.isInitialized());

        auto r2 = engine.init();
        REQUIRE(r2.has_value());
        REQUIRE(engine.isInitialized());
    }
}
