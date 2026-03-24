# Story 1.1: CMake + vcpkg + Presets Setup

Status: ready-for-dev

---

## Story

As a **developer**,
I want **a fully configured CMake project with vcpkg manifest mode**,
so that **I can build the project reproducibly on any supported platform**.

## Acceptance Criteria

1. Root `CMakeLists.txt` with `project(VoxelForge)`, C++20 standard enforced (`CMAKE_CXX_STANDARD 20`)
2. `CMakePresets.json` with configurations: **Debug** (ASan+UBSan on GCC/Clang), **Release** (`-O2 -DNDEBUG`), **RelWithDebInfo**
3. `vcpkg.json` listing all dependencies (see Dependencies section below)
4. `cmake/CompilerWarnings.cmake` — MSVC: `/W4 /WX /permissive-`; GCC/Clang: `-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti`
5. `cmake/Sanitizers.cmake` — optional ASan, UBSan, TSan toggles via CMake options
6. Three CMake targets: `VoxelEngine` (static lib), `VoxelGame` (executable), `VoxelTests` (test executable)
7. `CMAKE_EXPORT_COMPILE_COMMANDS ON` for clangd/clang-tidy
8. Project compiles to empty targets on MSVC 2022 and GCC 13+

## Tasks / Subtasks

- [ ] Task 1: Create root `CMakeLists.txt` (AC: #1, #6, #7)
  - [ ] 1.1 `cmake_minimum_required(VERSION 3.25)` and `project(VoxelForge VERSION 0.1.0 LANGUAGES CXX)`
  - [ ] 1.2 Set `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`
  - [ ] 1.3 Set `CMAKE_EXPORT_COMPILE_COMMANDS ON`
  - [ ] 1.4 Include `cmake/CompilerWarnings.cmake` and `cmake/Sanitizers.cmake`
  - [ ] 1.5 `add_subdirectory(engine)`, `add_subdirectory(game)`, `add_subdirectory(tests)`
  - [ ] 1.6 Find vcpkg packages via `find_package()` calls
- [ ] Task 2: Create `vcpkg.json` manifest (AC: #3)
  - [ ] 2.1 Define `name: "voxelforge"`, `version-string: "0.1.0"`
  - [ ] 2.2 List all dependencies (see Dependencies section)
  - [ ] 2.3 Handle enkiTS and FastNoiseLite (NOT in vcpkg — add as vendored headers or git submodule)
- [ ] Task 3: Create `CMakePresets.json` (AC: #2)
  - [ ] 3.1 Configure preset with vcpkg toolchain: `CMAKE_TOOLCHAIN_FILE` = `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`
  - [ ] 3.2 Debug preset: ASan+UBSan on GCC/Clang, debug symbols
  - [ ] 3.3 Release preset: `-O2 -DNDEBUG`
  - [ ] 3.4 RelWithDebInfo preset
  - [ ] 3.5 MSVC-specific preset (no sanitizers, `/EHsc-` for no-exceptions)
- [ ] Task 4: Create `cmake/CompilerWarnings.cmake` (AC: #4)
  - [ ] 4.1 MSVC: `/W4 /WX /permissive-`
  - [ ] 4.2 GCC/Clang: `-Wall -Wextra -Wpedantic -Werror`
  - [ ] 4.3 Add `-fno-exceptions -fno-rtti` for GCC/Clang
  - [ ] 4.4 MSVC: `/GR-` for no-RTTI, handle exception disabling carefully (see Dev Notes)
  - [ ] 4.5 Expose as a CMake function: `voxelforge_set_warnings(<target>)`
- [ ] Task 5: Create `cmake/Sanitizers.cmake` (AC: #5)
  - [ ] 5.1 CMake options: `VOXELFORGE_ENABLE_ASAN`, `VOXELFORGE_ENABLE_UBSAN`, `VOXELFORGE_ENABLE_TSAN`
  - [ ] 5.2 GCC/Clang flags: `-fsanitize=address`, `-fsanitize=undefined`, `-fsanitize=thread`
  - [ ] 5.3 MSVC: `/fsanitize=address` (ASan only — UBSan/TSan not supported on MSVC)
  - [ ] 5.4 Expose as a CMake function: `voxelforge_enable_sanitizers(<target>)`
- [ ] Task 6: Create `engine/CMakeLists.txt` (AC: #6)
  - [ ] 6.1 `add_library(VoxelEngine STATIC)` with empty placeholder source
  - [ ] 6.2 `target_include_directories(VoxelEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)`
  - [ ] 6.3 Link vcpkg dependencies (PUBLIC for headers consumers need, PRIVATE for implementation-only)
  - [ ] 6.4 Apply compiler warnings via `voxelforge_set_warnings(VoxelEngine)`
  - [ ] 6.5 Setup precompiled headers (PRIVATE scope)
  - [ ] 6.6 Create placeholder: `engine/src/Placeholder.cpp` (static lib needs at least one source)
  - [ ] 6.7 Create directory structure: `engine/include/voxel/core/`, `engine/src/core/`
- [ ] Task 7: Create `game/CMakeLists.txt` (AC: #6)
  - [ ] 7.1 `add_executable(VoxelGame game/src/main.cpp)`
  - [ ] 7.2 `target_link_libraries(VoxelGame PRIVATE VoxelEngine)`
  - [ ] 7.3 Apply compiler warnings
  - [ ] 7.4 Create `game/src/main.cpp` with minimal `int main() { return 0; }`
- [ ] Task 8: Create `tests/CMakeLists.txt` (AC: #6)
  - [ ] 8.1 `find_package(Catch2 3 REQUIRED)`
  - [ ] 8.2 `add_executable(VoxelTests)` with placeholder test
  - [ ] 8.3 `target_link_libraries(VoxelTests PRIVATE VoxelEngine Catch2::Catch2WithMain)`
  - [ ] 8.4 `include(Catch2::Catch2WithMain)` and `catch_discover_tests(VoxelTests)`
  - [ ] 8.5 Create `tests/TestPlaceholder.cpp` with a basic passing test
- [ ] Task 9: Verify builds (AC: #8)
  - [ ] 9.1 Configure and build with default preset
  - [ ] 9.2 Run CTest to verify test target works
  - [ ] 9.3 Verify `compile_commands.json` is generated

## Dev Notes

### Critical Architecture Constraints

- **3-layer dependency rule**: Core depends on nothing. Engine depends on Core. Game depends on Engine. No reverse dependencies. Each is a separate CMake target with enforced dependency direction.
- **Engine is STATIC library** — game links against it.
- Use `target_include_directories` with `PUBLIC`/`PRIVATE` — **never** global `include_directories`.
- **Exceptions disabled** project-wide: `-fno-exceptions` (GCC/Clang), handle MSVC carefully (see below).
- **RTTI disabled** project-wide: `-fno-rtti` (GCC/Clang), `/GR-` (MSVC).

### MSVC Exception Handling — Critical Gotcha

MSVC exception disabling is not as straightforward as GCC/Clang:
- Do **NOT** use `/EHsc-` — it's not a valid flag. Instead, simply omit `/EHsc` and define `_HAS_EXCEPTIONS=0`.
- Add `target_compile_definitions(<target> PRIVATE _HAS_EXCEPTIONS=0)` for MSVC to disable STL exception usage.
- Some STL headers (like `<expected>`) work fine without exceptions. `std::expected` uses monadic error handling, not exceptions.
- If vcpkg-provided libraries were compiled with exceptions enabled, linking may work but runtime behavior differs — this is acceptable for a game engine that aborts on fatal errors.

### MSVC RTTI — Critical Gotcha

- `/GR-` disables `dynamic_cast` and `typeid`. EnTT uses its own type system and does NOT require RTTI.
- `std::any` requires RTTI — **do not use `std::any` anywhere in the project**.
- Some ImGui backends may use RTTI internally — test carefully. If issues arise, enable RTTI only for the ImGui compilation unit.

### std::expected — C++23 Requirement

- `std::expected<T,E>` is a **C++23 feature**, not C++20.
- **MSVC 2022 (v17.3+)**: Available with `/std:c++20` due to early adoption (Microsoft ships it under C++20 mode).
- **GCC 12+**: Available with `-std=c++23`. GCC 13 has full support.
- **Clang 16+**: Available with `-std=c++23`.
- **Recommendation**: Set `CMAKE_CXX_STANDARD 20` as the base. For `std::expected`, MSVC works natively. For GCC/Clang, either:
  - Use `-std=c++2b` / `-std=c++23` (preferred — architecture doc says "selective C++23")
  - Or use a polyfill like `tl::expected` (adds a dependency)
- **Decision**: Use `CMAKE_CXX_STANDARD 23` if all three target compilers support it, otherwise keep 20 and add a `__has_include(<expected>)` check with a polyfill fallback. Per ADR-001, selective C++23 is intended.

### enkiTS and FastNoiseLite — Not in vcpkg

These two dependencies are **NOT available in the vcpkg registry**:
- **enkiTS** (dougbinks/enkiTS): Header + source library. Options:
  1. Add as git submodule under `third_party/enkiTS/` and `add_subdirectory()`
  2. Use CMake `FetchContent` to pull at configure time
  3. Vendored copy in `third_party/`
- **FastNoiseLite**: Single header file. Options:
  1. Vendored copy in `third_party/FastNoiseLite/`
  2. `FetchContent`

**Recommended approach**: Create `third_party/` directory. Use `FetchContent` for enkiTS (it has a CMakeLists.txt). Vendor FastNoiseLite as a single header (simplest). Neither is needed for Story 1.1 — just ensure the pattern is established for future stories.

**For Story 1.1**: Only include dependencies actually needed to compile the empty targets. The `vcpkg.json` should list all planned dependencies for completeness, but `find_package()` calls for unused deps can be deferred. At minimum, the empty engine static lib + game executable + test executable must compile.

### vcpkg.json Dependencies

```json
{
  "name": "voxelforge",
  "version-string": "0.1.0",
  "dependencies": [
    "vulkan-memory-allocator",
    "vk-bootstrap",
    "volk",
    "glfw3",
    "glm",
    "spdlog",
    "entt",
    "imgui",
    "catch2",
    "stb",
    "lz4",
    "sol2",
    "luajit"
  ]
}
```

Note: `enkiTS` and `FastNoiseLite` are NOT in vcpkg. Handle via `FetchContent` or vendoring in `third_party/`. Not needed for this story.

### CMakePresets.json Structure

Must include explicit vcpkg toolchain file reference:
```json
{
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ]
}
```

### Precompiled Headers

PCH should include frequently used STL + third-party headers (PRIVATE to engine target):
```cmake
target_precompile_headers(VoxelEngine PRIVATE
    <vector>
    <array>
    <string>
    <memory>
    <cstdint>
    <expected>      # C++23
    <glm/glm.hpp>
    <spdlog/spdlog.h>
    <entt/entt.hpp>
)
```

Use **PRIVATE** scope — do not force PCH on consumers of the library.

### Directory Structure to Create

```
VoxelForge/
├── CMakeLists.txt              # NEW — root build
├── CMakePresets.json           # NEW — build presets
├── vcpkg.json                  # NEW — dependency manifest
├── cmake/
│   ├── CompilerWarnings.cmake  # NEW
│   └── Sanitizers.cmake        # NEW
├── engine/
│   ├── CMakeLists.txt          # NEW — static lib
│   ├── include/voxel/
│   │   └── core/               # NEW — empty dir for future headers
│   └── src/
│       └── Placeholder.cpp     # NEW — empty source for static lib
├── game/
│   ├── CMakeLists.txt          # NEW — executable
│   └── src/
│       └── main.cpp            # NEW — minimal entry point
└── tests/
    ├── CMakeLists.txt          # NEW — test executable
    └── TestPlaceholder.cpp     # NEW — basic passing test
```

### Existing Files to Preserve

The project currently has:
- `.gitignore` — update to include CMake build artifacts (`build/`, `out/`, `CMakeCache.txt`, etc.)
- `CLAUDE.md` — do not modify
- `Monolith.sln` + `Monolith/` — legacy Visual Studio solution. Leave in place (developer may clean up later).
- `.idea/` — Rider project files. Leave in place.

### .gitignore Updates

Add CMake/build artifacts to `.gitignore`:
```
build/
out/
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
compile_commands.json
*.vcxproj*
vcpkg_installed/
```

### Project Structure Notes

- All paths follow the architecture doc's project tree exactly.
- Namespace for all engine code: `voxel::` with sub-namespaces (`voxel::core`, `voxel::math`, etc.).
- Files use PascalCase per naming conventions.
- The `engine/include/voxel/` public header root enables `#include "voxel/core/Types.h"` style includes.

### References

- [Source: _bmad-output/planning-artifacts/architecture.md — Project Tree, ADR-001, ADR-008]
- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.1]
- [Source: _bmad-output/project-context.md — Build System, Technology Stack, vcpkg.json]
- [Source: CLAUDE.md — Project Structure, Tech Stack, Naming Conventions]

## Dev Agent Record

### Agent Model Used

### Debug Log References

### Completion Notes List

### File List
