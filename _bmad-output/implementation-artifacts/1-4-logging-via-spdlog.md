# Story 1.4: Logging via spdlog

Status: ready-for-dev

## Story

As a developer,
I want centralized logging with multiple severity levels and sinks,
so that I can trace execution and debug effectively.

## Acceptance Criteria

1. **Log Header** — `voxel/core/Log.h` provides macros `VX_LOG_TRACE(...)` through `VX_LOG_CRITICAL(...)` wrapping spdlog
2. **Initialization** — `Log::init()` creates two sinks: stdout color sink + rotating file sink (`logs/voxelforge.log`, 5 MB max, 3 rotated files)
3. **Runtime Level** — `Log::setLevel(spdlog::level::info)` changes log verbosity at runtime
4. **VX_FATAL Integration** — `VX_FATAL(msg)` calls `VX_LOG_CRITICAL` then `std::abort()` (update existing `Assert.h`)
5. **Log Format** — `[HH:MM:SS.mmm] [level] [source:line] message` (spdlog pattern: `[%H:%M:%S.%e] [%l] [%s:%#] %v`)
6. **Release Optimization** — TRACE/DEBUG compile to no-ops in Release via `SPDLOG_ACTIVE_LEVEL`

## Tasks / Subtasks

- [ ] Task 1: Create `engine/include/voxel/core/Log.h` (AC: 1, 3)
  - [ ] 1.1 Define `Log` class with `static init()`, `static shutdown()`, `static setLevel()`, `static getLogger()`
  - [ ] 1.2 Define `VX_LOG_TRACE` through `VX_LOG_CRITICAL` macros using `SPDLOG_LOGGER_*` variants
- [ ] Task 2: Create `engine/src/core/Log.cpp` (AC: 2, 5)
  - [ ] 2.1 Implement `Log::init()` with stdout color sink + rotating file sink
  - [ ] 2.2 Set spdlog pattern to `[%H:%M:%S.%e] [%l] [%s:%#] %v`
  - [ ] 2.3 Set default level to `trace` in Debug, `info` in Release
  - [ ] 2.4 Register logger as spdlog default
- [ ] Task 3: Update `engine/include/voxel/core/Assert.h` (AC: 4)
  - [ ] 3.1 Add `#include "voxel/core/Log.h"` to Assert.h
  - [ ] 3.2 Change `VX_FATAL` to call `VX_LOG_CRITICAL(msg)` before `std::abort()`
  - [ ] 3.3 Update `VX_ASSERT` failure path to also route through `VX_LOG_CRITICAL` for log file capture
- [ ] Task 4: Update `engine/CMakeLists.txt` (AC: 6)
  - [ ] 4.1 Add `src/core/Log.cpp` to source list
  - [ ] 4.2 Add `SPDLOG_ACTIVE_LEVEL` compile definitions per configuration (PUBLIC scope)
  - [ ] 4.3 Add `SPDLOG_NO_EXCEPTIONS` compile definition (required — project uses `-fno-exceptions`)
- [ ] Task 5: Create `tests/core/TestLog.cpp` (all ACs)
  - [ ] 5.1 Test `Log::init()` succeeds without crash
  - [ ] 5.2 Test all log level macros execute without crash
  - [ ] 5.3 Test `Log::setLevel()` filters messages correctly
  - [ ] 5.4 Test rotating file sink creates log file
  - [ ] 5.5 Test that calling `Log::init()` twice does not leak or corrupt (reinit guard)
- [ ] Task 6: Update `tests/CMakeLists.txt`
  - [ ] 6.1 Add `core/TestLog.cpp` to VoxelTests sources

## Dev Notes

### Critical: SPDLOG_LOGGER_* Macros Required for Source Location

spdlog only embeds `__FILE__` / `__LINE__` in log output when using compile-time macros (`SPDLOG_TRACE`, `SPDLOG_INFO`, etc.), **NOT** when calling `spdlog::info()` directly. The `VX_LOG_*` macros MUST wrap the `SPDLOG_LOGGER_*` variants to get `[source:line]` in output.

```cpp
// CORRECT — source location captured
#define VX_LOG_INFO(...) SPDLOG_LOGGER_INFO(::voxel::core::Log::getLogger(), __VA_ARGS__)

// WRONG — source location will be empty
#define VX_LOG_INFO(...) ::voxel::core::Log::getLogger()->info(__VA_ARGS__)
```

### Critical: Both Compile-Time AND Runtime Levels Required

spdlog requires **both** settings to show lower-level messages:
1. Compile-time: `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE` (preprocessor strips macro calls below threshold)
2. Runtime: `spdlog::set_level(spdlog::level::trace)` or `logger->set_level(...)` (logger filters at runtime)

If either is missing, messages won't appear. `Log::init()` must call `logger->set_level()` matching the build config.

### Critical: VX_FATAL Update Creates Assert.h → Log.h Dependency

Current `VX_FATAL` in `Assert.h` uses `std::fprintf(stderr, ...)`. Updating it to call `VX_LOG_CRITICAL` introduces a dependency from `Assert.h` → `Log.h`. This is safe because `Log.h` does NOT depend on `Assert.h`. Ensure `Log.h` never includes `Assert.h` to avoid circular dependency.

### Critical: SPDLOG_NO_EXCEPTIONS Required

This project disables exceptions (`-fno-exceptions` / `_HAS_EXCEPTIONS=0`). spdlog by default uses `try/catch` internally, which will cause **compiler errors** with `-fno-exceptions`. You MUST define `SPDLOG_NO_EXCEPTIONS` in CMakeLists.txt. When set, spdlog calls `std::abort()` on internal errors instead of throwing. Add to engine/CMakeLists.txt:

```cmake
target_compile_definitions(VoxelEngine PUBLIC SPDLOG_NO_EXCEPTIONS)
```

### spdlog Version

vcpkg will install spdlog 1.15.x (latest stable as of 2025). Uses bundled fmt. No breaking changes from 1.12+ to 1.15.x that affect this story's API surface.

### Project Structure Notes

Files to create/modify align with established structure:

```
engine/
├── include/voxel/core/
│   ├── Types.h          ← exists (Story 1.3)
│   ├── Assert.h         ← MODIFY (VX_FATAL update)
│   ├── Result.h         ← exists (Story 1.3)
│   └── Log.h            ← CREATE
└── src/core/
    ├── CoreModule.cpp   ← exists (Story 1.3, can be kept or removed)
    └── Log.cpp          ← CREATE
tests/core/
    ├── TestResult.cpp   ← exists (Story 1.3)
    └── TestLog.cpp      ← CREATE
```

### Implementation Blueprint

**Log.h** (~50 lines):
```cpp
#pragma once

#include <spdlog/spdlog.h>
#include <memory>

namespace voxel::core
{

class Log
{
public:
    static void init();
    static void shutdown();
    static void setLevel(spdlog::level::level_enum level);
    static std::shared_ptr<spdlog::logger>& getLogger();

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace voxel::core

// Macros MUST use SPDLOG_LOGGER_* for source location support
#define VX_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_INFO(...)     SPDLOG_LOGGER_INFO(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_WARN(...)     SPDLOG_LOGGER_WARN(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(::voxel::core::Log::getLogger(), __VA_ARGS__)
#define VX_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::voxel::core::Log::getLogger(), __VA_ARGS__)
```

**Log.cpp** (~40 lines):
```cpp
#include "voxel/core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <filesystem>

namespace voxel::core
{

std::shared_ptr<spdlog::logger> Log::s_logger;

void Log::init()
{
    std::filesystem::create_directories("logs");

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/voxelforge.log", 5 * 1024 * 1024, 3);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    s_logger = std::make_shared<spdlog::logger>("VoxelForge", sinks.begin(), sinks.end());

    s_logger->set_pattern("[%H:%M:%S.%e] [%l] [%s:%#] %v");
#ifdef NDEBUG
    s_logger->set_level(spdlog::level::info);
#else
    s_logger->set_level(spdlog::level::trace);
#endif
    s_logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(s_logger);
}

void Log::shutdown()
{
    spdlog::shutdown();
}

void Log::setLevel(spdlog::level::level_enum level)
{
    s_logger->set_level(level);
}

std::shared_ptr<spdlog::logger>& Log::getLogger()
{
    return s_logger;
}

} // namespace voxel::core
```

**Updated Assert.h** (add `#include "voxel/core/Log.h"` and replace both macros):
```cpp
#include "voxel/core/Log.h"

// VX_ASSERT: Debug-only. Routes failure through spdlog for log file capture.
#ifdef NDEBUG
#define VX_ASSERT(cond, msg) ((void)0)
#else
#define VX_ASSERT(cond, msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            VX_LOG_CRITICAL("ASSERT FAILED: {}", msg);                                                                 \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

// VX_FATAL: Always active. Logs critical then aborts.
#define VX_FATAL(msg)                          \
    do                                         \
    {                                          \
        VX_LOG_CRITICAL("{}", msg);            \
        std::abort();                          \
    } while (0)
```

Note: Both `VX_ASSERT` and `VX_FATAL` now route through spdlog so failures appear in `logs/voxelforge.log` (not just stderr). The `SPDLOG_LOGGER_CRITICAL` macro captures `__FILE__:__LINE__` at the call site, so the explicit `__FILE__/__LINE__` format strings from the old implementation are no longer needed.

**CMakeLists.txt compile definitions** (add to engine/CMakeLists.txt):
```cmake
# spdlog: disable exceptions (project uses -fno-exceptions)
target_compile_definitions(VoxelEngine PUBLIC SPDLOG_NO_EXCEPTIONS)

# spdlog compile-time log level filtering
target_compile_definitions(VoxelEngine PUBLIC
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
)
```

Note: `PUBLIC` scope because the macros are in `Log.h` (a public header). Consumer targets (VoxelGame, VoxelTests) inherit `SPDLOG_ACTIVE_LEVEL` transitively through VoxelEngine. Do NOT add `SPDLOG_ACTIVE_LEVEL` to VoxelTests or VoxelGame independently — it will cause redefinition warnings.

### Testing Strategy

Use Catch2 TEST_CASE with SECTION blocks (established pattern from Story 1.3):

```cpp
TEST_CASE("Log system initialization", "[core][log]")
{
    SECTION("Log::init() creates logger without crash") { ... }
    SECTION("All log level macros execute without crash") { ... }
}

TEST_CASE("Log level filtering", "[core][log]")
{
    SECTION("setLevel filters messages below threshold") { ... }
}

TEST_CASE("Log file output", "[core][log]")
{
    SECTION("Rotating file sink creates log file in logs/ directory") { ... }
}
```

For file output tests: call `Log::init()`, write a log message, call `Log::shutdown()` (flushes), then verify `logs/voxelforge.log` exists and contains the message. Clean up test log files in a Catch2 event listener or test fixture.

### Established Patterns from Previous Stories

- **Header style**: `#pragma once`, all types in `namespace voxel::core`, Allman braces
- **Macros**: `VX_` prefix, `do { ... } while(0)` wrapping for statement-like macros
- **Static class members**: use `inline static` or define in .cpp (prefer .cpp for non-trivial init)
- **Include order**: associated header → project headers → third-party → std library
- **Testing**: Catch2 v3, `TEST_CASE` + `SECTION`, tags like `[core][log]`
- **CMake**: source files listed explicitly (no GLOBs), `PRIVATE`/`PUBLIC` scopes intentional
- **Line endings**: LF (enforced by `.editorconfig`)
- **Column limit**: 120 characters (`.clang-format`)

### Potential Pitfalls

1. **logs/ directory must be created explicitly** — `rotating_file_sink_mt` does NOT create parent directories. Call `std::filesystem::create_directories("logs")` in `Log::init()` before constructing the file sink. This is already shown in the blueprint above.

2. **`s_logger` null before init** — If any `VX_LOG_*` macro is called before `Log::init()`, it will dereference a null `shared_ptr`. Consider either: (a) lazy initialization, (b) a static null-check in `getLogger()`, or (c) documenting that `Log::init()` must be called first. Option (c) is simplest and matches engine initialization order.

3. **MSVC debug iterator checks** — MSVC in debug mode enables iterator debugging which can slow spdlog. This is acceptable for Debug builds.

4. **CoreModule.cpp** — Currently exists to validate header compilation. Once `Log.cpp` provides a real translation unit, `CoreModule.cpp` can remain (it's harmless) or be updated to also include `Log.h`.

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.4, lines 62-75]
- [Source: _bmad-output/planning-artifacts/architecture.md — ADR-008: Exceptions Disabled]
- [Source: _bmad-output/project-context.md — Tech stack, spdlog 1.12+, naming conventions]
- [Source: _bmad-output/implementation-artifacts/1-3-core-types-assert-result.md — Previous story patterns]
- [Source: spdlog GitHub wiki — SPDLOG_ACTIVE_LEVEL and source location requirements]
- [Source: spdlog v1.15.3 release notes — Latest stable version as of June 2025]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
