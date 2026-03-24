# Epic 1 — Foundation (Core + CMake + CI)

**Priority**: P0
**Dependencies**: None
**Goal**: Project skeleton compiles on Windows and Linux, core types are tested, logging works. Every subsequent epic builds on this.

---

## Story 1.1: CMake + vcpkg + Presets Setup

**As a** developer,
**I want** a fully configured CMake project with vcpkg manifest mode,
**so that** I can build the project reproducibly on any supported platform.

**Acceptance Criteria:**
- Root `CMakeLists.txt` with `project(VoxelForge)`, C++20 standard enforced
- `CMakePresets.json` with configurations: Debug (ASan+UBSan), Release (-O2 -DNDEBUG), RelWithDebInfo
- `vcpkg.json` listing all dependencies (see project-context.md)
- `cmake/CompilerWarnings.cmake` — MSVC: `/W4 /WX /permissive-`; GCC/Clang: `-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti`
- `cmake/Sanitizers.cmake` — optional ASan, UBSan, TSan toggles via CMake options
- Three CMake targets: `VoxelEngine` (static lib), `VoxelGame` (executable), `VoxelTests` (test executable)
- `CMAKE_EXPORT_COMPILE_COMMANDS ON` for clangd/clang-tidy
- Project compiles to empty targets on MSVC 2022 and GCC 13+

**Technical Notes:**
- Engine is a STATIC library target; game links against it
- Use `target_include_directories` with `PUBLIC`/`PRIVATE` — no global `include_directories`
- Precompiled headers: `<vector>`, `<array>`, `<string>`, `<memory>`, `<cstdint>`, `<expected>`, `<glm/glm.hpp>`, `<spdlog/spdlog.h>`, `<entt/entt.hpp>`

---

## Story 1.2: .clang-format + .clang-tidy + .editorconfig

**As a** developer,
**I want** code formatting and linting enforced by tooling,
**so that** all code follows the project conventions automatically.

**Acceptance Criteria:**
- `.clang-format` matching the config in `project-context.md` (Microsoft base, Allman braces, 120 col, include regroup)
- `.clang-tidy` matching the config in `project-context.md` (naming checks, bugprone, modernize, performance)
- `.editorconfig` matching the config in `project-context.md`
- Running `clang-format -i` on any source file produces no changes after initial format
- A format check script `tools/check-format.sh` that returns non-zero if any file needs formatting

---

## Story 1.3: Core Types, Assert, and Result

**As a** developer,
**I want** foundational types and error handling primitives,
**so that** all engine code builds on a consistent, safe base.

**Acceptance Criteria:**
- `voxel/core/Types.h` — typedefs: `using uint8 = std::uint8_t;` etc., common forward declarations
- `voxel/core/Assert.h` — `VX_ASSERT(cond, msg)` active in Debug, no-op in Release; `VX_FATAL(msg)` always logs + aborts
- `voxel/core/Result.h` — `enum class EngineError : uint8_t { FileNotFound, InvalidFormat, ShaderCompileError, VulkanError, ChunkNotLoaded, OutOfMemory, ScriptError }`; `template<typename T> using Result = std::expected<T, EngineError>;`
- All types in `namespace voxel::core`
- Unit tests: Result construction, `.and_then()` chaining, `.or_else()` error path, `std::unexpected` propagation

---

## Story 1.4: Logging via spdlog

**As a** developer,
**I want** centralized logging with multiple severity levels and sinks,
**so that** I can trace execution and debug effectively.

**Acceptance Criteria:**
- `voxel/core/Log.h` — macros: `VX_LOG_TRACE(...)` through `VX_LOG_CRITICAL(...)` wrapping spdlog
- `Log::init()` creates two sinks: stdout color sink + rotating file sink (`logs/voxelforge.log`, 5 MB max, 3 files)
- Log level configurable at runtime via `Log::setLevel(spdlog::level::info)`
- `VX_FATAL(msg)` calls `VX_LOG_CRITICAL` then `std::abort()`
- Format: `[HH:MM:SS.mmm] [level] [source:line] message`
- Compiles to no-ops for TRACE/DEBUG in Release build (via spdlog `SPDLOG_ACTIVE_LEVEL`)

---

## Story 1.5: Math Types and Coordinate Utilities

**As a** developer,
**I want** math primitives and voxel coordinate conversion helpers,
**so that** all spatial code uses consistent, tested utilities.

**Acceptance Criteria:**
- `voxel/math/MathTypes.h` — GLM aliases if raw GLM is sufficient, or thin wrappers
- `voxel/math/AABB.h` — struct with `min`/`max` (vec3); methods: `contains(point)`, `intersects(other)`, `expand(point)`, `center()`, `extents()`
- `voxel/math/Ray.h` — struct with `origin` (vec3) + `direction` (vec3, normalized)
- `voxel/math/CoordUtils.h` — free functions: `worldToChunk(dvec3) → ivec2`, `worldToLocal(dvec3) → ivec3`, `localToWorld(ivec2 chunk, ivec3 local) → dvec3`, `blockToIndex(int x, int y, int z) → int32_t`, `indexToBlock(int32_t) → ivec3`
- All in `namespace voxel::math`
- Unit tests: AABB intersection/contains, coordinate roundtrips (world→chunk→local→world identity), `blockToIndex`/`indexToBlock` inverse, boundary values (0,0,0 and 15,15,15)
