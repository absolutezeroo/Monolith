# Project Context — VoxelForge Engine

> Implementation guide for all AI agents. Loaded automatically by BMAD workflows:
> `bmad-create-story`, `bmad-dev-story`, `bmad-code-review`, `bmad-quick-dev`,
> `bmad-create-architecture`, `bmad-sprint-planning`, `bmad-retrospective`, `bmad-correct-course`.

---

## Technology Stack & Versions

| Component | Version/Choice | Notes |
|-----------|---------------|-------|
| C++ Standard | C++20, selective C++23 | `-std=c++20` minimum, `-std=c++23` if available |
| Compiler (Windows) | MSVC 2022 (v17.8+) | `/std:c++20 /W4 /WX /permissive-` |
| Compiler (Linux) | GCC 13+ or Clang 16+ | `-std=c++20 -Wall -Wextra -Wpedantic -Werror` |
| Vulkan | 1.3 | Dynamic rendering, sync2, BDA, descriptor indexing |
| vk-bootstrap | Latest stable | Instance/Device/Swapchain creation |
| VMA | 3.0+ | GPU memory sub-allocation, VmaVirtualBlock |
| volk | Latest stable | Vulkan function pointer loader |
| EnTT | Latest stable | ECS for entities only, NOT for chunk data |
| enkiTS | Latest stable | Job system, 5 priority levels |
| sol2 | v3.x | C++ Lua binding, header-only |
| LuaJIT | 2.1 | Lua runtime (Lua 5.1 compatible) |
| FastNoiseLite | Latest | Single-header noise generation |
| GLM | 0.9.9+ | Column-major, left-handed |
| GLFW | 3.4+ | Windowing, input, Vulkan surface |
| spdlog | 1.12+ | Logging with fmt |
| Dear ImGui | Latest docking branch | Debug overlay, Vulkan backend |
| Catch2 | v3.x | Unit tests, sections, BDD, BENCHMARK |
| CMake | 3.25+ | Modern targets, presets, compile_commands |
| vcpkg | Manifest mode | `vcpkg.json` at project root |
| SPIR-V compiler | glslangValidator or shaderc | Shader compilation |
| LZ4 | Latest | Chunk serialization compression |

---

## Critical Implementation Rules

### Naming Conventions

```
Classes / Structs       → PascalCase          ChunkManager, BlockRegistry
Methods / Functions     → camelCase           getChunkAt(), generateMesh()
Member variables        → m_ + camelCase      m_chunkSize, m_renderDistance
Local variables         → camelCase           blockIndex, localPos
Constants / constexpr   → SCREAMING_SNAKE     MAX_CHUNK_SIZE, BLOCK_AIR
Enums                   → enum class Pascal   enum class BlockFace { Top, Bottom, North }
Namespaces              → lowercase           voxel::core, voxel::renderer
Files                   → PascalCase          ChunkManager.h / ChunkManager.cpp
Interfaces              → I prefix            IRenderable, ISerializable
Template parameters     → PascalCase          typename TAllocator, typename BlockType
Booleans                → is/has/should       m_isLoaded, hasCollision(), shouldRebuild
Macros                  → VX_ prefix          VX_ASSERT, VX_LOG_INFO, VX_FATAL
```

### Include Order (enforced by .clang-format)

```cpp
// 1. Associated header (for .cpp files)
#include "voxel/world/ChunkManager.h"

// 2. Project headers
#include "voxel/core/Assert.h"
#include "voxel/core/Log.h"

// 3. Third-party headers
#include <entt/entt.hpp>
#include <glm/glm.hpp>

// 4. Standard library
#include <vector>
#include <memory>
#include <cstdint>
```

### Code Organization

- **One class per file** (except trivially related types grouped together)
- **Max ~500 lines per file** — refactor if exceeded
- **No code in headers** except templates and trivial inline functions
- **`#pragma once`** — no manual include guards
- **Public headers** in `engine/include/voxel/` — implementation in `engine/src/` (mirror structure)
- **Never `using namespace`** in headers — OK in .cpp for own module namespace only

### Memory & Ownership

- **RAII everywhere** — every resource has one clear owner
- **`std::unique_ptr`** for exclusive ownership
- **`std::shared_ptr`** only when shared ownership is proven necessary (rare)
- **Raw pointers = non-owning** — never `new`/`delete` manually
- **Pool allocators** for chunks (amortized allocation)
- **Arena allocators** for temporary meshing buffers (reset per frame/job)
- **Never `malloc`/`free`** — use Core allocators

### Error Handling

```
Programmer errors   → VX_ASSERT(condition, "message")     Debug only, stripped in Release
Expected errors     → Result<T> = std::expected<T, EngineError>
Fatal errors        → VX_FATAL("message") → log critical + std::abort()
Monadic chaining    → .and_then() .or_else() .transform()  C++23 style
```

- **Exceptions disabled**: `-fno-exceptions` / `/EHsc-`
- **RTTI disabled**: `-fno-rtti` / `/GR-`
- STL allocations that would throw → crash (acceptable for a game engine)

### Threading Rules

- Chunk pipeline parallelized via enkiTS job system
- Jobs read **immutable snapshots** of chunk neighborhoods — never the live world
- **Never lock on chunks** — jobs produce results, main thread integrates
- `std::atomic` for inter-thread flags and counters
- `ConcurrentQueue` (SPSC or MPSC) for job results and commands
- **Max N GPU uploads per frame** (default 8, configurable via config)
- Main thread owns: ECS registry, ChunkManager mutation, Vulkan submission

### Mandatory Patterns

- **Command Pattern**: every game action = serializable `GameCommand` object in a queue
- **Tick-based simulation**: 20 ticks/sec (50ms), fixed timestep, logic in discrete steps
- **Game State ≠ Render State**: game state is truth, renderer interpolates between ticks
- **Event Bus**: all state changes published — Lua hooks subscribe to events
- **Data-driven content**: blocks/items/recipes defined in JSON + Lua, never hardcoded in C++
- **Chunks outside ECS**: dedicated `ChunkManager` spatial storage, ECS for entity metadata only

### What to Avoid

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `auto` everywhere | Types should be explicit in most cases | Explicit types; `auto` OK for iterators, lambdas, complex template returns |
| C++ exceptions | Disabled via compiler flag | `Result<T>`, `VX_ASSERT`, `VX_FATAL` |
| RTTI | Disabled, not needed | `enum class` tags, ECS component type IDs |
| `std::shared_ptr` by default | Unclear ownership | `std::unique_ptr` + raw non-owning pointers |
| `new`/`delete` | Manual memory management | RAII wrappers, pool/arena allocators |
| `using namespace std;` | Pollutes namespace | Explicit `std::` prefix |
| C++20 modules | Tooling immature | Traditional headers |
| `std::format` in hot paths | Slow compile times | spdlog/fmtlib |
| Ranges in critical code | Slow compile times | Raw loops or STL algorithms |
| Voxel data in ECS | Wrong access pattern | `ChunkManager` flat arrays |
| Direct state mutation | Breaks network-readiness | Command queue + event bus |
| Floating-point for block coords | Precision errors | Integer coordinates (`glm::ivec3`) |

### C++20/23 Features: Adopt vs Avoid

**Adopt without hesitation:**
`concepts`, designated initializers, `constexpr`/`consteval` expansions, `constinit`,
`std::span`, three-way comparison `<=>`, `[[likely]]`/`[[unlikely]]`,
`std::expected<T,E>` (C++23), `std::mdspan` (C++23), multidimensional `operator[]` (C++23),
`std::unreachable()` (C++23)

**Use with caution:**
`std::format` (prefer fmtlib for compile speed), ranges (only in non-hot code),
coroutines (OK for async asset loading, not frame-critical)

**Do not use:**
C++20 modules, `std::jthread` (use enkiTS instead), `std::latch`/`std::barrier` (use enkiTS sync)

---

## Build System

### CMake Principles

- **Targets, not variables** — `target_link_libraries`, `target_include_directories`
- **PUBLIC/PRIVATE/INTERFACE** propagation — Core is PUBLIC to Engine, Engine is PRIVATE to Game
- **`CMAKE_EXPORT_COMPILE_COMMANDS ON`** — for clangd, clang-tidy
- **Precompiled headers** — STL, GLM, spdlog, EnTT in engine PCH
- **CMakePresets.json** — Debug (ASan+UBSan), Release (-O2 -DNDEBUG), RelWithDebInfo

### vcpkg.json

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

---

## .clang-format

```yaml
Language: Cpp
BasedOnStyle: Microsoft
Standard: c++20
ColumnLimit: 120
IndentWidth: 4
UseTab: Never
BreakBeforeBraces: Allman
PointerAlignment: Left
DerivePointerAlignment: false
AllowShortFunctionsOnASingleLine: Inline
AllowShortLambdasOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortBlocksOnASingleLine: Empty
NamespaceIndentation: None
CompactNamespaces: false
FixNamespaceComments: true
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"voxel/'
    Priority: 1
  - Regex: '^<(entt|sol|glm|vulkan|VkBootstrap|vk_mem_alloc|volk|glfw|spdlog|fmt|imgui|enkiTS|FastNoiseLite|catch2|lz4)'
    Priority: 2
  - Regex: '^<'
    Priority: 3
BinPackArguments: false
BinPackParameters: false
AlignAfterOpenBracket: AlwaysBreak
BreakConstructorInitializers: BeforeComma
PackConstructorInitializers: NextLine
Cpp11BracedListStyle: true
SpaceAfterCStyleCast: false
SpaceBeforeParens: ControlStatements
EmptyLineAfterAccessModifier: Never
EmptyLineBeforeAccessModifier: LogicalBlock
SeparateDefinitionBlocks: Leave
```

## .clang-tidy

```yaml
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  clang-analyzer-*,
  cppcoreguidelines-*,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-type-reinterpret-cast,
  -cppcoreguidelines-owning-memory,
  misc-*,
  -misc-non-private-member-variables-in-classes,
  modernize-*,
  -modernize-use-trailing-return-type,
  -modernize-use-nodiscard,
  performance-*,
  readability-*,
  -readability-magic-numbers,
  -readability-identifier-length,
  -readability-function-cognitive-complexity

WarningsAsErrors: ''

CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
  - key: readability-identifier-naming.VariableCase
    value: camelBack
  - key: readability-identifier-naming.MemberPrefix
    value: 'm_'
  - key: readability-identifier-naming.ConstantCase
    value: UPPER_CASE
  - key: readability-identifier-naming.EnumConstantCase
    value: CamelCase
  - key: readability-identifier-naming.NamespaceCase
    value: lower_case
  - key: readability-identifier-naming.TemplateParameterCase
    value: CamelCase
  - key: cppcoreguidelines-special-member-functions.AllowSoleDefaultDtor
    value: true
  - key: performance-move-const-arg.CheckTriviallyCopyableMove
    value: false
```

## .editorconfig

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{h,hpp,cpp,cxx,inl}]
indent_style = space
indent_size = 4
max_line_length = 120

[*.{json,yaml,yml,toml}]
indent_style = space
indent_size = 2

[CMakeLists.txt]
indent_style = space
indent_size = 4

[*.cmake]
indent_style = space
indent_size = 4

[*.md]
trim_trailing_whitespace = false

[*.{glsl,vert,frag,comp,geom}]
indent_style = space
indent_size = 4

[*.lua]
indent_style = space
indent_size = 4

[Makefile]
indent_style = tab
```

---

## Testing Strategy

### Framework

Catch2 v3 — inline sections, free-form test names, BENCHMARK macro, BDD (GIVEN/WHEN/THEN).

### What to Test

| System | What to test | Test type |
|--------|-------------|-----------|
| Core/Result | Monadic chaining, error propagation | Unit |
| Core/Allocators | Alloc/free/reuse, fragmentation | Unit |
| Math/AABB | Intersection, contains, swept collision | Unit |
| Math/CoordUtils | worldToChunk, blockToIndex roundtrips | Unit |
| World/ChunkSection | get/set, bounds, fill | Unit |
| World/Palette | Roundtrip, bit transitions, compression ratio | Unit |
| World/Meshing | Quad count, face culling correctness, AO values | Unit (vs reference) |
| World/WorldGen | Determinism (same seed = same output) | Snapshot |
| Physics/Collision | Axis clipping, edge cases, ground detection | Unit |
| Physics/DDA | Hit detection, face ID, max distance | Unit |
| World/BlockRegistry | Register, lookup, ID stability per session | Unit |
| Renderer/Gigabuffer | Alloc/free/reuse (CPU-side VmaVirtualBlock logic) | Unit |

### What NOT to Unit Test

- Vulkan rendering (use RenderDoc + visual inspection)
- Lua scripting (integration tests: load mod, verify block registered)
- Full game loop (manual playtesting)
- Performance (separate benchmarks via Catch2 BENCHMARK)

### Coverage Targets

- Core layer: >60%
- Engine layer: >40%
- Game layer: 0% (Lua-driven, tested via integration)

---

## Documentation

### Doxygen Style

```cpp
/**
 * @brief Generates the mesh for a chunk section via binary greedy meshing.
 *
 * @param section The section to mesh (16³ voxels).
 * @param neighbors The 6 neighbor sections (for face culling at boundaries).
 * @return The generated mesh data, or an error if input is invalid.
 *
 * @note Thread-safe: operates on an immutable snapshot.
 * @see ChunkManager::requestRemesh()
 */
Result<ChunkMesh> generateMesh(const ChunkSection& section,
                                std::span<const ChunkSection*, 6> neighbors);
```

- `@brief` and `@param` in **headers** (declarations)
- `@details` in **sources** (implementations) when needed
- `JAVADOC_AUTOBRIEF = YES` in Doxyfile
- No trivial comments (`// increment counter` on `counter++`)
- Generate call graphs and inheritance graphs via DOT

### Commit Messages

```
type(scope): short description

types: feat, fix, refactor, perf, test, docs, build, chore
scope: core, math, world, renderer, physics, scripting, input, ecs, game

Examples:
feat(world): implement palette compression for chunk sections
fix(renderer): fix quad diagonal flip for anisotropic AO
perf(world): optimize binary greedy meshing inner loop
test(physics): add DDA raycasting edge case tests
docs(architecture): add ADR-011 for texture array format
build(cmake): add sanitizer presets for CI
```
