# Story 1.3: Core Types, Assert, and Result

Status: ready-for-dev

---

## Story

As a **developer**,
I want **foundational types and error handling primitives**,
so that **all engine code builds on a consistent, safe base**.

## Acceptance Criteria

1. `engine/include/voxel/core/Types.h` — fixed-width typedefs (`using uint8 = std::uint8_t;` etc.) and common forward declarations
2. `engine/include/voxel/core/Assert.h` — `VX_ASSERT(cond, msg)` active in Debug, no-op in Release; `VX_FATAL(msg)` always logs to stderr + calls `std::abort()`
3. `engine/include/voxel/core/Result.h` — `enum class EngineError : uint8` with error variants; `template<typename T> using Result = std::expected<T, EngineError>;`
4. All types in `namespace voxel::core`
5. Unit tests: Result construction, `.and_then()` chaining, `.or_else()` error path, `std::unexpected` propagation

## Tasks / Subtasks

- [ ] Task 1: Bump C++ standard from 20 to 23 (AC: #3)
  - [ ] 1.1 Change `CMAKE_CXX_STANDARD` from `20` to `23` in root `CMakeLists.txt`
  - [ ] 1.2 Verify project still compiles clean with `cmake --preset msvc-debug`
- [ ] Task 2: Create `engine/include/voxel/core/Types.h` (AC: #1, #4)
  - [ ] 2.1 Create directory `engine/include/voxel/core/` if not present
  - [ ] 2.2 Write Types.h with `#pragma once`, fixed-width aliases, namespace
  - [ ] 2.3 Verify no conflicts with existing PCH headers
- [ ] Task 3: Create `engine/include/voxel/core/Assert.h` (AC: #2, #4)
  - [ ] 3.1 Write Assert.h with `VX_ASSERT` and `VX_FATAL` macros
  - [ ] 3.2 Create `engine/src/core/Assert.cpp` for `VX_FATAL` stderr + abort implementation
  - [ ] 3.3 Verify `VX_ASSERT` compiles to nothing in Release (`#ifdef NDEBUG`)
- [ ] Task 4: Create `engine/include/voxel/core/Result.h` (AC: #3, #4)
  - [ ] 4.1 Write Result.h with `EngineError` enum and `Result<T>` alias
  - [ ] 4.2 Add `<expected>` to PCH in `engine/CMakeLists.txt`
  - [ ] 4.3 Verify `Result<int>` compiles and `.value()` / `.error()` work
- [ ] Task 5: Update `engine/CMakeLists.txt` (AC: all)
  - [ ] 5.1 Add `src/core/Assert.cpp` to the source list
  - [ ] 5.2 Remove `src/Placeholder.cpp` from source list (replaced by real sources)
  - [ ] 5.3 Add `<expected>` to `target_precompile_headers`
  - [ ] 5.4 Verify library compiles clean
- [ ] Task 6: Create unit tests `tests/core/TestResult.cpp` (AC: #5)
  - [ ] 6.1 Create `tests/core/` directory
  - [ ] 6.2 Write tests using Catch2 BDD style (GIVEN/WHEN/THEN) or SECTION blocks
  - [ ] 6.3 Test: Result<int> success construction and `.value()` access
  - [ ] 6.4 Test: Result<int> error construction via `std::unexpected(EngineError::...)`
  - [ ] 6.5 Test: `.and_then()` monadic chaining on success path
  - [ ] 6.6 Test: `.and_then()` short-circuits on error path
  - [ ] 6.7 Test: `.or_else()` invoked on error, skipped on success
  - [ ] 6.8 Test: `.transform()` maps value on success
  - [ ] 6.9 Test: `std::unexpected` propagation through chained operations
  - [ ] 6.10 Test: EngineError enum covers all defined variants
- [ ] Task 7: Update `tests/CMakeLists.txt` (AC: #5)
  - [ ] 7.1 Add `core/TestResult.cpp` to test sources
  - [ ] 7.2 Keep `TestPlaceholder.cpp` (build system smoke test)
  - [ ] 7.3 Build and run all tests — verify 0 failures
- [ ] Task 8: Final verification (AC: all)
  - [ ] 8.1 Full build with `cmake --preset msvc-debug` + `cmake --build`
  - [ ] 8.2 Run `ctest` — all tests pass
  - [ ] 8.3 Run `tools/check-format.sh` — all new files pass formatting check
  - [ ] 8.4 Verify `clang-format -i` produces no changes on new files

## Dev Notes

### CRITICAL: C++ Standard Must Be Bumped to 23

The root `CMakeLists.txt` currently has `CMAKE_CXX_STANDARD 20`. **`std::expected` is a C++23 library feature** and is NOT available under `-std=c++20` on any compiler (GCC, Clang, or MSVC).

**Required change** in `CMakeLists.txt` line 10:
```cmake
set(CMAKE_CXX_STANDARD 23)    # Was 20 — bumped for std::expected (ADR-001)
```

All target compilers support C++23:
- MSVC 2022 v17.8+: full `std::expected` support
- GCC 13+: full `std::expected` support with `-std=c++23`
- Clang 16+: full `std::expected` support with `-std=c++23`

This aligns with ADR-001 which says: "Opt-in C++23 features: `std::expected`, `std::mdspan`, multidimensional `operator[]`."

### CRITICAL: `<expected>` Missing from PCH

The Story 1.1 ACs required `<expected>` in the precompiled headers, but it was omitted (likely because it fails under C++20). After bumping to C++23, add it to `engine/CMakeLists.txt`:

```cmake
target_precompile_headers(VoxelEngine PRIVATE
    <vector>
    <array>
    <string>
    <memory>
    <cstdint>
    <expected>       # ADD THIS — needed for Result<T>
    <glm/glm.hpp>
    <spdlog/spdlog.h>
    <entt/entt.hpp>
)
```

### CRITICAL: No Exceptions and No RTTI

Exceptions are disabled project-wide (`-fno-exceptions` / `_HAS_EXCEPTIONS=0`). RTTI is disabled (`-fno-rtti` / `/GR-`). This means:
- **Never `throw`** — use `std::unexpected` to create error results
- **Never `dynamic_cast`** — use `enum class` tags
- **`std::expected::value()`** will call `std::abort()` on error (because `bad_expected_access` is an exception that can't be thrown) — this is acceptable behavior for a game engine, but prefer explicit `.has_value()` checks or monadic chaining
- **VX_ASSERT** must NOT throw — use `std::abort()` directly

### CRITICAL: spdlog NOT Available Yet for VX_FATAL

Story 1.4 (Logging via spdlog) has NOT been implemented yet. `VX_FATAL(msg)` cannot use `VX_LOG_CRITICAL`. Instead, write to `stderr` directly:

```cpp
#define VX_FATAL(msg)                                    \
    do {                                                 \
        std::fprintf(stderr, "FATAL [%s:%d]: %s\n",     \
            __FILE__, __LINE__, msg);                    \
        std::abort();                                    \
    } while (0)
```

When Story 1.4 is implemented, `VX_FATAL` will be updated to use `VX_LOG_CRITICAL` before `std::abort()`. The dev agent for 1.4 should update Assert.h.

### Types.h — Exact Specification

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace voxel::core
{

// Fixed-width integer aliases
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Size and pointer types
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Floating-point aliases (explicit width documentation)
using float32 = float;
using float64 = double;

} // namespace voxel::core
```

**Do NOT add forward declarations for classes that don't exist yet** (ChunkManager, Renderer, etc.). Those belong in their own headers. Keep this file minimal.

### Assert.h — Exact Specification

```cpp
#pragma once

#include <cstdio>
#include <cstdlib>

// VX_ASSERT: Debug-only assertion. No-op in Release.
#ifdef NDEBUG
    #define VX_ASSERT(cond, msg) ((void)0)
#else
    #define VX_ASSERT(cond, msg)                                     \
        do {                                                         \
            if (!(cond)) {                                           \
                std::fprintf(stderr, "ASSERT FAILED [%s:%d]: %s\n", \
                    __FILE__, __LINE__, (msg));                      \
                std::abort();                                        \
            }                                                        \
        } while (0)
#endif

// VX_FATAL: Always active. Logs to stderr and aborts.
#define VX_FATAL(msg)                                        \
    do {                                                     \
        std::fprintf(stderr, "FATAL [%s:%d]: %s\n",         \
            __FILE__, __LINE__, (msg));                      \
        std::abort();                                        \
    } while (0)
```

**Key decisions:**
- Macros, not functions — must capture `__FILE__` / `__LINE__` at call site
- `do { ... } while(0)` idiom — safe in all statement contexts (if/else etc.)
- `stderr` not `stdout` — errors go to error stream
- No dependency on spdlog — that comes in Story 1.4
- `(void)0` for Release VX_ASSERT — suppresses "unused variable" warnings

**Assert.cpp is optional.** If VX_FATAL and VX_ASSERT are macro-only (no function body), no .cpp file is needed. Only create Assert.cpp if you add a helper function (e.g., `detail::assertFailed()`). A simple macro-only approach is preferred.

### Result.h — Exact Specification

```cpp
#pragma once

#include <expected>

#include "voxel/core/Types.h"

namespace voxel::core
{

/// Error codes for engine operations.
enum class EngineError : uint8
{
    FileNotFound,
    InvalidFormat,
    ShaderCompileError,
    VulkanError,
    ChunkNotLoaded,
    OutOfMemory,
    ScriptError
};

/// Result type for fallible operations.
/// Success: holds T. Failure: holds EngineError.
template<typename T>
using Result = std::expected<T, EngineError>;

} // namespace voxel::core
```

**Key decisions:**
- `enum class EngineError : uint8` — smallest representation, sufficient for error codes
- `uint8` alias from Types.h — project convention, not raw `uint8_t`
- 7 error variants as specified in epic ACs — do NOT add more speculatively
- `Result<T>` is a simple `using` alias — no wrapper class, no abstraction

### Test File — Pattern and Location

Tests go in `tests/core/TestResult.cpp`. Use Catch2 v3 with SECTION blocks.

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voxel/core/Result.h"

using namespace voxel::core;

TEST_CASE("Result<T> success and error construction", "[core][result]")
{
    SECTION("Success construction holds value")
    {
        Result<int> r = 42;
        REQUIRE(r.has_value());
        REQUIRE(r.value() == 42);
    }

    SECTION("Error construction via std::unexpected")
    {
        Result<int> r = std::unexpected(EngineError::FileNotFound);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == EngineError::FileNotFound);
    }
}

TEST_CASE("Result<T> monadic operations", "[core][result]")
{
    SECTION(".and_then() chains on success")
    {
        Result<int> r = 10;
        auto doubled = r.and_then([](int v) -> Result<int> { return v * 2; });
        REQUIRE(doubled.has_value());
        REQUIRE(doubled.value() == 20);
    }

    SECTION(".and_then() short-circuits on error")
    {
        Result<int> r = std::unexpected(EngineError::OutOfMemory);
        bool called = false;
        auto result = r.and_then([&](int v) -> Result<int> {
            called = true;
            return v * 2;
        });
        REQUIRE_FALSE(called);
        REQUIRE(result.error() == EngineError::OutOfMemory);
    }

    SECTION(".or_else() invoked on error")
    {
        Result<int> r = std::unexpected(EngineError::FileNotFound);
        auto recovered = r.or_else([](EngineError) -> Result<int> { return 0; });
        REQUIRE(recovered.has_value());
        REQUIRE(recovered.value() == 0);
    }

    SECTION(".or_else() skipped on success")
    {
        Result<int> r = 42;
        bool called = false;
        auto result = r.or_else([&](EngineError) -> Result<int> {
            called = true;
            return 0;
        });
        REQUIRE_FALSE(called);
        REQUIRE(result.value() == 42);
    }

    SECTION(".transform() maps value on success")
    {
        Result<int> r = 5;
        auto mapped = r.transform([](int v) { return v * 3; });
        REQUIRE(mapped.has_value());
        REQUIRE(mapped.value() == 15);
    }
}

TEST_CASE("std::unexpected propagation through chain", "[core][result]")
{
    auto step1 = [](int v) -> Result<int> { return v + 1; };
    auto step2 = [](int) -> Result<int> {
        return std::unexpected(EngineError::InvalidFormat);
    };
    auto step3 = [](int v) -> Result<int> { return v * 10; };

    Result<int> r = 1;
    auto final_result = r.and_then(step1).and_then(step2).and_then(step3);

    REQUIRE_FALSE(final_result.has_value());
    REQUIRE(final_result.error() == EngineError::InvalidFormat);
}

TEST_CASE("EngineError covers all defined variants", "[core][result]")
{
    // Verify all enum values are distinct and constructable
    REQUIRE(EngineError::FileNotFound != EngineError::InvalidFormat);
    REQUIRE(EngineError::ShaderCompileError != EngineError::VulkanError);
    REQUIRE(EngineError::ChunkNotLoaded != EngineError::OutOfMemory);
    REQUIRE(EngineError::ScriptError != EngineError::FileNotFound);

    // Verify underlying type is uint8
    REQUIRE(sizeof(EngineError) == sizeof(uint8));
}
```

**Testing notes:**
- Tags: `[core]` for module, `[result]` for specific type
- Use `SECTION` blocks (not BDD GIVEN/WHEN/THEN) for unit tests — simpler and the project already uses sections in TestPlaceholder.cpp
- Test both success and error paths for every monadic operation
- Verify error propagation through chains — this is the most common usage pattern
- Do NOT test `VX_ASSERT` or `VX_FATAL` (they abort — can't test in-process)

### CMakeLists.txt Changes

**Root `CMakeLists.txt`** — change line 10:
```cmake
set(CMAKE_CXX_STANDARD 23)  # Was 20
```

**`engine/CMakeLists.txt`** — replace contents:
```cmake
add_library(VoxelEngine STATIC
    src/core/Assert.cpp       # Only if Assert.cpp has content; omit if macro-only
)

target_include_directories(VoxelEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

voxelforge_set_warnings(VoxelEngine)
voxelforge_enable_sanitizers(VoxelEngine)

find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

target_link_libraries(VoxelEngine
    PUBLIC  glm::glm
    PUBLIC  spdlog::spdlog
    PUBLIC  EnTT::EnTT
)

target_precompile_headers(VoxelEngine PRIVATE
    <vector>
    <array>
    <string>
    <memory>
    <cstdint>
    <expected>
    <glm/glm.hpp>
    <spdlog/spdlog.h>
    <entt/entt.hpp>
)
```

**Note:** If Assert.h is macro-only (recommended), you don't need Assert.cpp. But a static library MUST have at least one translation unit. Options:
1. Keep `Placeholder.cpp` temporarily, or
2. Create a minimal `src/core/CoreModule.cpp` that includes the core headers (ensures they compile)

Option 2 is better — it validates header compilation and replaces the placeholder:
```cpp
// engine/src/core/CoreModule.cpp
// Ensures all core headers compile correctly.
// Remove this file once real core source files exist.
#include "voxel/core/Assert.h"
#include "voxel/core/Result.h"
#include "voxel/core/Types.h"
```

**`tests/CMakeLists.txt`** — add test file:
```cmake
add_executable(VoxelTests
    TestPlaceholder.cpp
    core/TestResult.cpp
)
```

### Project Structure Notes

All paths align with the architecture document:
```
engine/include/voxel/core/Types.h     ← New
engine/include/voxel/core/Assert.h    ← New
engine/include/voxel/core/Result.h    ← New
engine/src/core/CoreModule.cpp        ← New (replaces Placeholder.cpp)
tests/core/TestResult.cpp             ← New
```

Headers in `engine/include/voxel/core/` — source mirrors in `engine/src/core/`. This matches the architecture's "Public headers in `engine/include/voxel/` — implementation in `engine/src/` (mirror structure)" rule.

### Files to Create

| File | Action | Description |
|------|--------|-------------|
| `engine/include/voxel/core/Types.h` | CREATE | Fixed-width type aliases |
| `engine/include/voxel/core/Assert.h` | CREATE | VX_ASSERT + VX_FATAL macros |
| `engine/include/voxel/core/Result.h` | CREATE | EngineError enum + Result<T> alias |
| `engine/src/core/CoreModule.cpp` | CREATE | Header compilation validator (replaces Placeholder.cpp) |
| `tests/core/TestResult.cpp` | CREATE | Catch2 unit tests for Result<T> |

### Files to Modify

| File | Action | Description |
|------|--------|-------------|
| `CMakeLists.txt` | MODIFY | Bump `CMAKE_CXX_STANDARD` from 20 to 23 |
| `engine/CMakeLists.txt` | MODIFY | Update source list, add `<expected>` to PCH |
| `tests/CMakeLists.txt` | MODIFY | Add `core/TestResult.cpp` to test sources |

### Files to Delete

| File | Action | Description |
|------|--------|-------------|
| `engine/src/Placeholder.cpp` | DELETE | Replaced by `engine/src/core/CoreModule.cpp` |

### Architecture Compliance

- **Namespace**: `voxel::core` per naming conventions
- **File naming**: PascalCase (Types.h, Assert.h, Result.h)
- **One class per file**: EngineError enum and Result alias are in the same file (they're tightly coupled — this is fine per "except trivially related types")
- **`#pragma once`**: per code organization rules
- **No code in headers** except macros and template aliases (both require header-only placement)
- **No exceptions**: VX_ASSERT/VX_FATAL use `std::abort()`, not throw
- **No RTTI**: nothing in this story requires RTTI
- **Macro prefix**: `VX_` per naming conventions
- **Include order**: project headers → third-party → standard library (enforced by .clang-format)

### Previous Story Intelligence

From Story 1.2 implementation:
- `.clang-format`, `.clang-tidy`, `.editorconfig` are in place — run `clang-format -i` on all new files
- `tools/check-format.sh` exists and works — run it as final verification
- All files MUST use LF line endings (`.editorconfig` specifies `end_of_line = lf`)
- **MSVC preset used**: `msvc-debug` — this is the verified working preset on the developer's machine
- **compile_commands.json**: only generated by Ninja/Makefiles generator, not VS generator. clang-tidy won't run against msvc-debug preset
- Code review for 1.2 found CRLF line ending issues — ensure all new files use LF

From Story 1.1 implementation:
- Build system is fully operational
- MSVC 2022 with msvc-debug preset verified working
- `/GR-` for no-RTTI, `_HAS_EXCEPTIONS=0` for no exceptions are already in CompilerWarnings.cmake
- PCH is configured — new `<expected>` header just needs to be appended to the list

### Git Intelligence

Recent commits:
```
b711dd4 Add project-wide formatting and linting configuration     ← Story 1.2
f629de9 Initialize CMake project with vcpkg manifest, presets...  ← Story 1.1
```

Pattern: commit messages follow `type(scope): description` format. Use:
```
feat(core): add foundational types, assert macros, and Result<T> error handling
```

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.3]
- [Source: _bmad-output/planning-artifacts/architecture.md — Project Tree, System 1, ADR-001, ADR-008]
- [Source: _bmad-output/project-context.md — Naming Conventions, Error Handling, Code Organization, Testing Strategy]
- [Source: _bmad-output/implementation-artifacts/1-2-clang-format-clang-tidy-editorconfig.md — Previous story learnings]
- [Source: CLAUDE.md — Naming Conventions, Project Structure, Critical Rules]
- [Source: cmake/CompilerWarnings.cmake — Exception/RTTI disable flags]
- [Source: engine/CMakeLists.txt — Current PCH and source list]
- [Source: cppreference.com — std::expected requires C++23]

## Dev Agent Record

### Agent Model Used

{{agent_model_name_version}}

### Debug Log References

### Completion Notes List

### File List
