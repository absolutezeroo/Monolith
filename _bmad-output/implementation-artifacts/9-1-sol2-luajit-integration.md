# Story 9.1: sol2 + LuaJIT Integration

Status: done

## Story

As a developer,
I want a Lua VM embedded in the engine via sol2,
so that scripts can be loaded and executed safely with proper sandboxing.

## Acceptance Criteria

1. `ScriptEngine` class owns a `sol::state`, initializes with safe libraries only (`base`, `math`, `string`, `table`, `coroutine`).
2. `os`, `io`, `debug`, `loadfile`, `dofile`, `load` explicitly removed from the environment.
3. LuaJIT configured as the Lua runtime (linked via vcpkg).
4. `ScriptEngine::loadScript(path) -> Result<void>` — executes a Lua file, returns error on failure.
5. `ScriptEngine::callFunction(name) -> Result<void>` — calls a zero-arg global Lua function by name (V1; variadic args + `Result<sol::object>` deferred to Story 9.2 when callers exist, since `sol::object` requires full sol2 includes incompatible with PIMPL header).
6. Error handling: Lua errors caught by sol2 `protected_function_result`, converted to `EngineError::ScriptError`, logged with file+line.
7. Filesystem access restricted: scripts can only read from their own mod directory (path validation in `loadScript`).
8. Integration test: load a Lua file that sets a global variable, verify from C++.

## Tasks / Subtasks

- [x] Task 1: CMake integration — link sol2 + LuaJIT (AC: 3)
  - [x] 1.1 Add `find_package(PkgConfig REQUIRED)` and `pkg_check_modules(LuaJIT ...)` or manual LuaJIT discovery to `engine/CMakeLists.txt`
  - [x] 1.2 Add `find_package(sol2 CONFIG REQUIRED)` to `engine/CMakeLists.txt`
  - [x] 1.3 Add `target_link_libraries(VoxelEngine ... sol2::sol2 ...)` and LuaJIT link target
  - [x] 1.4 Add `target_compile_definitions(VoxelEngine PUBLIC SOL_ALL_SAFETIES_ON=1 SOL_LUAJIT=1 SOL_NO_EXCEPTIONS=1 SOL_USING_CXX_LUA=0)`
  - [x] 1.5 Verify LuaJIT include path resolves correctly (vcpkg may place headers at `luajit/lua.h` — may need manual include dir adjustment)
  - [x] 1.6 Build and confirm zero link errors

- [x] Task 2: Create ScriptEngine class (AC: 1, 2, 6)
  - [x] 2.1 Create `engine/include/voxel/scripting/ScriptEngine.h`
  - [x] 2.2 Create `engine/src/scripting/ScriptEngine.cpp`
  - [x] 2.3 Implement constructor: create `sol::state`, open safe libraries, set custom `at_panic` handler
  - [x] 2.4 Implement `init() -> Result<void>`: open safe libs, remove dangerous globals, create `voxel` API table (empty for now)
  - [x] 2.5 Implement `shutdown()`: clear Lua state, release resources

- [x] Task 3: Sandbox — remove dangerous globals (AC: 2)
  - [x] 3.1 After opening safe libs, explicitly set `os`, `io`, `debug`, `loadfile`, `dofile`, `load`, `package`, `require` to `sol::lua_nil`
  - [x] 3.2 Verify sandbox by attempting to call removed functions and confirming errors

- [x] Task 4: loadScript + callFunction API (AC: 4, 5, 6)
  - [x] 4.1 Implement `loadScript(const std::filesystem::path& path) -> Result<void>`
  - [x] 4.2 Validate path exists and is within allowed directory before loading
  - [x] 4.3 Use `sol::state::safe_script_file()` with error handler callback
  - [x] 4.4 Convert `sol::protected_function_result` errors to `EngineError{ErrorCode::ScriptError, message}`
  - [x] 4.5 Log errors with file path and line number from Lua error info
  - [x] 4.6 Implement `callFunction(std::string_view name, ...) -> Result<sol::object>` using `sol::protected_function`
  - [x] 4.7 Check `.valid()` on result and convert failures to `EngineError::ScriptError`

- [x] Task 5: Filesystem restriction (AC: 7)
  - [x] 5.1 Store allowed base paths in ScriptEngine (initially `assets/scripts/`)
  - [x] 5.2 In `loadScript()`, canonicalize path and verify it starts with an allowed prefix
  - [x] 5.3 Return `EngineError{ErrorCode::InvalidArgument, "Script path outside sandbox"}` on violation

- [x] Task 6: Integration tests (AC: 8)
  - [x] 6.1 Create `tests/scripting/TestScriptEngine.cpp`
  - [x] 6.2 Test: init ScriptEngine, verify `sol::state` is valid
  - [x] 6.3 Test: load Lua string that sets `test_var = 42`, read it back from C++ and verify value
  - [x] 6.4 Test: load Lua file from `tests/scripting/test_scripts/` that sets a global, verify from C++
  - [x] 6.5 Test: call a Lua function from C++ with args, verify return value
  - [x] 6.6 Test: sandbox — attempt `os.exit()`, verify error returned (not crash)
  - [x] 6.7 Test: sandbox — attempt `io.open()`, verify error returned
  - [x] 6.8 Test: load nonexistent file, verify `Result` contains `ScriptError`
  - [x] 6.9 Test: load script with syntax error, verify `Result` contains error with line info
  - [x] 6.10 Test: path traversal attack (`../../../etc/passwd`), verify rejection
  - [x] 6.11 Test: call nonexistent function, verify `Result` error

- [x] Task 7: CMakeLists + build validation (AC: all)
  - [x] 7.1 Add `ScriptEngine.cpp` to `engine/CMakeLists.txt` source list
  - [x] 7.2 Add `TestScriptEngine.cpp` to `tests/CMakeLists.txt`
  - [x] 7.3 Create `tests/scripting/test_scripts/` directory with test Lua files
  - [x] 7.4 Build entire project, verify zero warnings under `/W4 /WX`
  - [x] 7.5 Run all tests (existing + new), verify zero regressions

## Dev Notes

### CMake: sol2 + LuaJIT Linking (Critical)

vcpkg's sol2 port officially depends on `lua`, not `luajit`. Since sol2 is header-only, the actual Lua implementation is linked in the consumer project. The recommended approach:

**Option A — PkgConfig (preferred if available):**
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LuaJIT REQUIRED IMPORTED_TARGET luajit)
find_package(sol2 CONFIG REQUIRED)

target_link_libraries(VoxelEngine
    PUBLIC sol2::sol2
    PRIVATE PkgConfig::LuaJIT
)
```

**Option B — Manual find (fallback):**
```cmake
find_package(sol2 CONFIG REQUIRED)
find_path(LUAJIT_INCLUDE_DIR luajit.h PATH_SUFFIXES luajit)
find_library(LUAJIT_LIBRARY NAMES luajit-5.1 lua51)

target_include_directories(VoxelEngine PRIVATE ${LUAJIT_INCLUDE_DIR})
target_link_libraries(VoxelEngine
    PUBLIC sol2::sol2
    PRIVATE ${LUAJIT_LIBRARY}
)
```

**Known issue**: vcpkg may place LuaJIT headers in `include/luajit/` instead of `include/`. sol2 expects `#include <lua.h>` at the top level. You may need to add the LuaJIT include directory explicitly so that `lua.h` resolves. Check the actual path at `build/msvc-debug/vcpkg_installed/x64-windows/include/` after vcpkg install.

**Required compile definitions** — add to the VoxelEngine target:
```cmake
target_compile_definitions(VoxelEngine PUBLIC
    SOL_ALL_SAFETIES_ON=1
    SOL_LUAJIT=1
    SOL_NO_EXCEPTIONS=1
    SOL_USING_CXX_LUA=0
)
```

- `SOL_ALL_SAFETIES_ON=1` — enables all safety checks (type verification, argument count checks)
- `SOL_LUAJIT=1` — tells sol2 to expect LuaJIT-specific features and Lua 5.1 semantics
- `SOL_NO_EXCEPTIONS=1` — **MANDATORY** since VoxelForge compiles with `/EHsc-` (exceptions disabled). Without this, sol2 will try to throw and crash
- `SOL_USING_CXX_LUA=0` — LuaJIT is built as C, not C++

### Exception-Free Error Handling Pattern (Critical)

Since exceptions are disabled, sol2's error handling changes significantly:

1. **Custom `at_panic` handler** — MUST be set on the Lua state. Without exceptions, a Lua panic calls `abort()`. Set a handler that logs the error and calls `VX_FATAL`:

```cpp
int customPanic(lua_State* L)
{
    const char* msg = lua_tostring(L, -1);
    VX_LOG_CRITICAL("Lua PANIC: {}", msg ? msg : "unknown error");
    VX_FATAL("Unrecoverable Lua error");
    return 0; // Never reached
}
```

2. **Use `sol::protected_function` for ALL calls** — never use raw `sol::function` (it assumes success). Always check `.valid()`:

```cpp
sol::protected_function_result result = protectedFunc(args...);
if (!result.valid())
{
    sol::error err = result;
    return core::makeError(core::ErrorCode::ScriptError, err.what());
}
```

3. **Use `safe_script_file` / `safe_script`** with an error handler callback:

```cpp
auto errorHandler = [](lua_State*, sol::protected_function_result pfr) -> sol::protected_function_result {
    // pfr already contains the error info
    return pfr;
};
sol::protected_function_result result = m_lua.safe_script_file(path.string(), errorHandler);
if (!result.valid())
{
    sol::error err = result;
    return core::makeError(core::ErrorCode::ScriptError,
        fmt::format("Failed to load '{}': {}", path.string(), err.what()));
}
```

4. **Never use `script_file()` or `script()` without error handlers** — these will call the panic function on error, which means `abort()`.

### ScriptEngine Class Design

```cpp
// engine/include/voxel/scripting/ScriptEngine.h
#pragma once

#include "voxel/core/Result.h"

#include <sol/forward.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace voxel::scripting
{

class ScriptEngine
{
public:
    ScriptEngine();
    ~ScriptEngine();

    // Non-copyable, non-movable (owns Lua state)
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

    /// Call a global Lua function by name.
    [[nodiscard]] core::Result<sol::object> callFunction(std::string_view functionName);

    /// Get the raw sol::state (for future stories to bind APIs).
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
```

**Key design decisions:**
- `sol::state` held via `unique_ptr` to allow forward-declaration of sol types in the header (avoiding sol2 includes in the header — massive compile time improvement)
- `sol/forward.hpp` is the only sol include in the header — lightweight forward declarations
- Full sol2 includes only in the `.cpp` file
- `callFunction` is NOT templated in this story — variadic arg support deferred to Story 9.2 when actual APIs need it. V1 just calls zero-arg global functions
- `getLuaState()` exposes raw access for future stories (9.2+) that bind the `voxel.*` API table

### Sandbox Implementation

Remove these globals AFTER opening safe libraries:

```cpp
void ScriptEngine::removeDangerousGlobals()
{
    auto& lua = *m_lua;
    lua["os"] = sol::lua_nil;
    lua["io"] = sol::lua_nil;
    lua["debug"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["dofile"] = sol::lua_nil;
    lua["load"] = sol::lua_nil;
    lua["package"] = sol::lua_nil;
    lua["require"] = sol::lua_nil;
    lua["collectgarbage"] = sol::lua_nil;  // Prevent mods from triggering GC pauses
}
```

**Safe libraries to open** (explicit whitelist, not `sol::lib::*`):
- `sol::lib::base` — `print`, `type`, `tostring`, `tonumber`, `pcall`, `xpcall`, `error`, `assert`, `select`, `ipairs`, `pairs`, `next`, `rawget`, `rawset`, `rawequal`, `rawlen`, `unpack`
- `sol::lib::math` — all math functions
- `sol::lib::string` — string manipulation
- `sol::lib::table` — `table.insert`, `table.remove`, `table.sort`, `table.concat`
- `sol::lib::coroutine` — coroutine creation and management (used by advanced mods)

**DO NOT open**: `sol::lib::io`, `sol::lib::os`, `sol::lib::debug`, `sol::lib::package`, `sol::lib::ffi` (LuaJIT FFI — CRITICAL security risk), `sol::lib::jit` (LuaJIT internals — not needed by mods)

### Path Validation

```cpp
bool ScriptEngine::isPathAllowed(const std::filesystem::path& canonicalPath) const
{
    for (const auto& allowed : m_allowedPaths)
    {
        auto it = std::search(
            canonicalPath.begin(), canonicalPath.end(),
            allowed.begin(), allowed.end());
        if (it == canonicalPath.begin())
        {
            return true;
        }
    }
    return false;
}
```

Use `std::filesystem::weakly_canonical()` (not `canonical()` — avoids throwing on non-existent intermediate dirs) to resolve `..` and symlinks before checking.

### EngineError Integration

`ErrorCode::ScriptError` is **already defined** in `engine/include/voxel/core/Result.h`:

```cpp
enum class ErrorCode : uint8 {
    FileNotFound,
    InvalidFormat,
    ShaderCompileError,
    VulkanError,
    ChunkNotLoaded,
    OutOfMemory,
    InvalidArgument,
    ScriptError  // <-- Already exists
};
```

Use `core::makeError(core::ErrorCode::ScriptError, "message")` for all Lua errors.

### What NOT to Do

- **DO NOT add sol2 headers to the precompiled header** — sol2 is extremely large (50K+ lines after expansion). Including it in PCH would severely slow compilation of every translation unit. Only include in `.cpp` files that need it.
- **DO NOT use `sol::function`** — always use `sol::protected_function`. Without exceptions, raw `sol::function` calls `abort()` on error.
- **DO NOT use `script()` or `script_file()`** — always use `safe_script()` or `safe_script_file()` with error handler.
- **DO NOT open `sol::lib::ffi`** — LuaJIT's FFI allows Lua to call arbitrary C functions and access arbitrary memory. This is a **critical security hole** that completely bypasses the sandbox.
- **DO NOT open `sol::lib::jit`** — exposes LuaJIT internals, not needed by mods.
- **DO NOT use `sol::state_view` in the header** — it requires full sol2 headers. Use forward declarations only.
- **DO NOT modify `Block.h` or `BlockRegistry`** — callback extensions come in Story 9.2.
- **DO NOT bind any `voxel.*` API functions** — just create the empty `voxel` table. API binding is Story 9.2+.
- **DO NOT create `LuaBindings.h/cpp`** — deferred to Story 9.2.
- **DO NOT implement mod loading** — that's Story 9.11.

### File Structure

| Action | File | Namespace | Notes |
|--------|------|-----------|-------|
| NEW | `engine/include/voxel/scripting/ScriptEngine.h` | `voxel::scripting` | Header with forward-declared sol types |
| NEW | `engine/src/scripting/ScriptEngine.cpp` | `voxel::scripting` | Full implementation |
| NEW | `tests/scripting/TestScriptEngine.cpp` | — | Integration tests |
| NEW | `tests/scripting/test_scripts/set_global.lua` | — | Test script: sets `test_var = 42` |
| NEW | `tests/scripting/test_scripts/add_function.lua` | — | Test script: defines `function add(a,b) return a+b end` |
| NEW | `tests/scripting/test_scripts/syntax_error.lua` | — | Test script: intentional syntax error |
| MODIFY | `engine/CMakeLists.txt` | — | Add sol2/LuaJIT find_package, link, compile definitions, add ScriptEngine.cpp |
| MODIFY | `tests/CMakeLists.txt` | — | Add TestScriptEngine.cpp, link LuaJIT to test target |

### Naming & Style

- Classes: `ScriptEngine` (PascalCase)
- Methods: `loadScript`, `callFunction`, `isPathAllowed` (camelCase)
- Members: `m_lua`, `m_allowedPaths`, `m_isInitialized` (m_ prefix)
- Constants: `MAX_SCRIPT_SIZE` if needed (SCREAMING_SNAKE)
- `#pragma once` for headers
- Namespace: `voxel::scripting`
- No exceptions — use `Result<T>` for all fallible operations
- Max ~500 lines per file

### Testing Pattern

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/scripting/ScriptEngine.h"

using namespace voxel::scripting;

TEST_CASE("ScriptEngine", "[scripting]")
{
    ScriptEngine engine;

    SECTION("initialization succeeds")
    {
        auto result = engine.init();
        REQUIRE(result.has_value());
        REQUIRE(engine.isInitialized());
    }

    SECTION("load and execute Lua string setting global")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());
        // Use sol state directly for inline script test
        auto& lua = engine.getLuaState();
        // ...
    }

    SECTION("sandbox blocks os.exit")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());
        // Attempt to call os.exit — should fail gracefully
    }

    SECTION("path traversal rejected")
    {
        auto initResult = engine.init();
        REQUIRE(initResult.has_value());
        engine.addAllowedPath("assets/scripts/");
        auto result = engine.loadScript("../../../etc/passwd");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == voxel::core::ErrorCode::InvalidArgument);
    }
}
```

Test Lua files should be minimal:
- `set_global.lua`: `test_var = 42`
- `add_function.lua`: `function add(a, b) return a + b end`
- `syntax_error.lua`: `this is not valid lua !!!`

### Existing Infrastructure to Reuse

| Component | File | Usage |
|-----------|------|-------|
| `Result<T>` | `engine/include/voxel/core/Result.h` | Return type for all fallible operations |
| `ErrorCode::ScriptError` | `engine/include/voxel/core/Result.h` | Already defined — use for Lua errors |
| `core::makeError()` | `engine/include/voxel/core/Result.h` | Helper to create EngineError |
| `VX_LOG_*` | `engine/include/voxel/core/Log.h` | Logging Lua errors with context |
| `VX_FATAL` | `engine/include/voxel/core/Assert.h` | For unrecoverable Lua panics |
| `VX_ASSERT` | `engine/include/voxel/core/Assert.h` | Debug assertions |
| `VX_ASSETS_DIR` | CMake define | Base path for script files |

### Git Intelligence

Recent commits show the project is at the end of Epic 8 (Lighting). The last 10 commits are all `feat(world)` and `feat(renderer)` — no scripting work has been done yet. This is a clean start for the scripting subsystem.

Commit style: `feat(scripting): implement sol2/LuaJIT ScriptEngine with sandbox`

### Project Structure Notes

- New `engine/include/voxel/scripting/` directory follows existing pattern (`core/`, `world/`, `renderer/`, `game/`, `physics/`, `input/`, `math/`)
- New `engine/src/scripting/` directory mirrors include structure
- New `tests/scripting/` directory follows existing test organization (`tests/core/`, `tests/world/`, `tests/math/`, `tests/physics/`, `tests/renderer/`)
- `VX_ASSETS_DIR` is already defined as a CMake compile definition pointing to the assets directory — use it for default allowed paths

### Future Story Dependencies

This story establishes the foundation for:
- **Story 9.2**: Binds `voxel.register_block()` onto the empty `voxel` table created here
- **Story 9.8**: Binds `voxel.get_block()`, `voxel.set_block()` world query APIs
- **Story 9.10**: Binds `voxel.on()` event subscription via `EventBus`
- **Story 9.11**: Builds mod loading on top of `loadScript()` with per-mod isolated environments

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-09-lua-scripting.md — Story 9.1 acceptance criteria]
- [Source: _bmad-output/planning-artifacts/architecture.md — System 9: Scripting architecture, ADR-007: Lua/sol2/LuaJIT over Wren]
- [Source: _bmad-output/project-context.md — Naming conventions, error handling, testing standards, vcpkg.json dependencies]
- [Source: engine/include/voxel/core/Result.h — Result<T>, EngineError, ErrorCode::ScriptError]
- [Source: engine/include/voxel/core/Log.h — VX_LOG_* macros]
- [Source: engine/include/voxel/core/Assert.h — VX_ASSERT, VX_FATAL]
- [Source: engine/CMakeLists.txt — build target structure, existing find_package patterns]
- [Source: vcpkg.json — sol2 and luajit declared as dependencies]
- [Source: sol2 docs — exceptions.html, errors.html — SOL_NO_EXCEPTIONS, protected_function_result]
- [Source: vcpkg discussions #31702 — LuaJIT + sol2 CMake integration patterns]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- SIGSEGV crash on first test run: caused by spdlog `SPDLOG_LOGGER_CALL` macro dereferencing null logger pointer. Fixed by calling `Log::init()` in test setup. The spdlog macro does NOT null-check the logger; this was a known gap in the test infrastructure.

### Completion Notes List

- **CMake integration**: Used manual LuaJIT discovery (Option B from Dev Notes) since vcpkg doesn't provide a CMake config for LuaJIT. `find_path` + `find_library` for LuaJIT, `find_package(sol2 CONFIG)` for sol2. LuaJIT headers at `include/luajit/` confirmed, added PUBLIC include dir so sol2's `#include <lua.h>` resolves.
- **ScriptEngine header**: Uses `sol/forward.hpp` for lightweight forward declarations. `unique_ptr<sol::state>` (PIMPL) avoids full sol2 include in header. `callFunction` returns `Result<void>` (V1 zero-arg) instead of `Result<sol::object>` because `sol::object` cannot be a complete type with only forward declarations. Full `Result<sol::object>` version deferred to Story 9.2 when actual callers exist.
- **Sandbox**: Removes `os`, `io`, `debug`, `loadfile`, `dofile`, `load`, `package`, `require`, `collectgarbage`. Only opens `base`, `math`, `string`, `table`, `coroutine`. Does NOT open `ffi` or `jit` (security-critical).
- **Error handling**: All Lua interactions use `sol::protected_function` and `safe_script_file` with error handler callbacks. Custom `at_panic` handler logs via `VX_LOG_CRITICAL` then calls `VX_FATAL` (avoids silent abort).
- **Path validation**: Uses `std::filesystem::weakly_canonical()` + `std::mismatch()` on path iterators to detect sandbox escapes. Rejects path traversal attacks.
- **ScriptEngine.cpp skips PCH**: sol2 is ~50K lines expanded; added `SKIP_PRECOMPILE_HEADERS ON` to avoid conflicts and bloating compilation of other TUs.
- **Tests**: 13 sections, 45 assertions. Covers init/shutdown lifecycle, Lua string execution, file loading, function calling, sandbox enforcement (os/io), nonexistent files, syntax errors, path traversal, nonexistent functions, voxel API table existence, re-init after shutdown.
- **Full regression**: 490,130 assertions across 242 test cases — all pass.

### Implementation Plan

1. CMake: sol2 header-only + LuaJIT manual discovery + compile definitions
2. ScriptEngine: PIMPL with unique_ptr<sol::state>, init/shutdown lifecycle
3. Sandbox: whitelist libraries, nil-out dangerous globals
4. API: loadScript (path validation + safe_script_file), callFunction (protected_function)
5. Tests: 13 test sections covering all ACs

### File List

| Action | File |
|--------|------|
| NEW | `engine/include/voxel/scripting/ScriptEngine.h` |
| NEW | `engine/src/scripting/ScriptEngine.cpp` |
| NEW | `tests/scripting/TestScriptEngine.cpp` |
| NEW | `tests/scripting/test_scripts/set_global.lua` |
| NEW | `tests/scripting/test_scripts/add_function.lua` |
| NEW | `tests/scripting/test_scripts/syntax_error.lua` |
| MODIFIED | `engine/CMakeLists.txt` |
| MODIFIED | `tests/CMakeLists.txt` |

### Change Log

- 2026-03-30: Story 9.1 implemented — sol2 + LuaJIT ScriptEngine with sandbox, filesystem restriction, and integration tests. All 8 ACs satisfied.
- 2026-03-30: Code review fixes applied (2 MEDIUM, 2 LOW):
  - M1: Updated AC 5 text to reflect `Result<void>` V1 signature (documented deviation)
  - M2: Removed empty-allowedPaths bypass — sandbox now enforced unconditionally; updated test to configure allowed path before testing nonexistent file
  - L1: Added `message(FATAL_ERROR ...)` guards for LuaJIT find_path/find_library in CMake
  - L2: Added `VX_LOG_WARN` when `addAllowedPath` canonicalization fails (was silent)
