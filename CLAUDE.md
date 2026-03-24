# CLAUDE.md — VoxelForge Engine

> Single source of truth for all AI agents working on this project.
> Follows BMAD v6 methodology — all planning artifacts live in `_bmad-output/`.

## Project

Minecraft-like voxel engine in **C++20/23** with **Vulkan 1.3**.
Solo-first, network-ready architecture. Full Lua scripting (sol2 + LuaJIT) for modding.

## BMAD Artifacts

| Phase | Artifact | Path |
|-------|----------|------|
| Analysis | Product Brief | `_bmad-output/planning-artifacts/product-brief.md` |
| Planning | PRD | `_bmad-output/planning-artifacts/PRD.md` |
| Solutioning | Architecture + ADRs | `_bmad-output/planning-artifacts/architecture.md` |
| Solutioning | Epics & Stories | `_bmad-output/planning-artifacts/epics/` |
| Implementation | Sprint Status | `_bmad-output/implementation-artifacts/sprint-status.yaml` |
| Context | Project Context | `_bmad-output/project-context.md` |

## Tech Stack

```
C++20 · Vulkan 1.3 · EnTT · enkiTS · sol2/LuaJIT · FastNoiseLite
GLM · GLFW · CMake 3.25+ · vcpkg · Catch2 v3 · spdlog · Dear ImGui
```

## Critical Rules for Agents

1. **Read `project-context.md` before any implementation** — it is the technical constitution
2. **Read `architecture.md` before coding** — every technical decision has an ADR
3. **Follow the BMAD pipeline** — story → dev → code review, fresh chat per workflow
4. **No direct state mutation** — use Command Pattern for all game actions
5. **C++ exceptions disabled** — use `Result<T>` (`std::expected<T, EngineError>`)
6. **Chunks do NOT go in the ECS** — dedicated spatial storage in ChunkManager
7. **One class per file** — max ~500 lines, refactor if exceeded

## Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes/Structs | PascalCase | `ChunkManager`, `BlockRegistry` |
| Methods/Functions | camelCase | `getChunkAt()`, `generateMesh()` |
| Member variables | `m_` prefix | `m_chunkSize`, `m_renderDistance` |
| Local variables | camelCase | `blockIndex`, `localPos` |
| Constants/constexpr | SCREAMING_SNAKE | `MAX_CHUNK_SIZE`, `BLOCK_AIR` |
| Namespaces | lowercase | `voxel::core`, `voxel::renderer` |
| Files | PascalCase | `ChunkManager.h` / `ChunkManager.cpp` |
| Enums | `enum class PascalCase { PascalCase }` | `BlockFace::Top` |
| Booleans | `is`/`has`/`should` prefix | `m_isLoaded`, `hasCollision()` |
| Macros | `VX_` prefix | `VX_ASSERT`, `VX_LOG_INFO` |

## Project Structure

```
VoxelForge/
├── CLAUDE.md
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── .clang-format / .clang-tidy / .editorconfig
├── _bmad/                         # BMAD config (agents, workflows)
├── _bmad-output/                  # BMAD artifacts (planning, impl, context)
├── cmake/                         # CompilerWarnings.cmake, Sanitizers.cmake
├── engine/
│   ├── CMakeLists.txt
│   ├── include/voxel/
│   │   ├── core/                  # Types, Assert, Log, Result, Allocator
│   │   ├── math/                  # Vec3, AABB, Ray
│   │   ├── world/                 # Chunk, ChunkManager, Block, BlockRegistry, WorldGen, Palette
│   │   ├── renderer/              # VulkanContext, Renderer, Mesh, Shader, Camera, TextureArray
│   │   ├── ecs/                   # Components, Systems
│   │   ├── physics/               # Collision, DDA
│   │   ├── scripting/             # ScriptEngine, LuaBindings
│   │   └── input/                 # InputManager
│   └── src/                       # Mirror of include/
├── game/
│   └── src/main.cpp
├── tests/
├── assets/
│   ├── shaders/                   # GLSL → SPIR-V
│   ├── textures/
│   └── scripts/                   # Lua mods
└── tools/
```

## Quickstart

```bash
npx bmad-method install            # Install BMAD + Game Dev Studio module
bmad-help                          # Context-aware guidance
bmad-create-story                  # Prepare next story
bmad-dev-story                     # Implement
bmad-code-review                   # Validate
```
