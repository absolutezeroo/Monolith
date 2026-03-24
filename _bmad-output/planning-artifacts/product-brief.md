# Product Brief — VoxelForge Engine

## Vision

An open-source, modern C++ voxel engine that combines Vulkan GPU-driven rendering, full Lua scripting, and network-ready architecture to deliver a moddable Minecraft-like experience with next-gen performance.

## Problem

Existing voxel engines force fundamental trade-offs:

- **Minecraft**: Java, legacy OpenGL renderer, modding via bytecode hacking (Forge/Fabric), closed source, 20 FPS in heavy modpacks
- **Luanti (Minetest)**: C++ + Lua but aging Irrlicht renderer, no GPU-driven pipeline, limited visual quality, single-threaded meshing
- **Veloren**: Rust ECS voxel RPG — excellent architecture but high modding barrier (Rust), not Minecraft-like gameplay
- **Project Ascendant**: Vulkan GPU-driven reference but focused on rendering R&D, not a playable engine
- **Custom engines (Craft, ClassiCube, VoxelCore)**: Either too basic, too specialized, or incomplete modding support

No modern C++ voxel engine exists that combines Vulkan, ECS, full Lua scripting, and network-ready architecture in a playable package.

## Proposed Solution

A 3-layer voxel engine (Core / Engine / Game):

- **Core**: Pure C++20, zero external dependencies — testable, portable, data-oriented
- **Engine**: Vulkan 1.3 GPU-driven with indirect rendering, binary greedy meshing, async chunk pipeline
- **Game**: Entirely Lua-driven — blocks, items, gameplay, mods via Luanti-style API

## Target Audience

| Persona | Need | How VoxelForge serves them |
|---------|------|---------------------------|
| C++ engine developer | Study/fork a modern voxel engine | Clean architecture, documented ADRs, open source |
| Lua modder | Create content without recompiling | Complete Lua API, hot-reload, sandboxed mods |
| Player | Performant, extensible Minecraft-like | 60 FPS, infinite world, mod ecosystem |

## Key Differentiators

1. **Vulkan GPU-driven renderer** — gigabuffer + indirect draw + compute culling, single draw call for entire world
2. **Binary greedy meshing** — ~74μs/chunk average (benchmarked), 30x faster than classic greedy
3. **Full Lua scripting** — sol2 + LuaJIT, Luanti-style modding API, hot-reload, sandboxed
4. **Network-ready from day 1** — Command Pattern + tick-based simulation, no retrofit needed
5. **Modern C++20/23** — `std::expected`, `concepts`, data-oriented design, no exceptions

## Inspirations & Reference Projects

| Project | What to learn | What to avoid |
|---------|--------------|---------------|
| **Luanti** (C++ + Lua) | Modding API design, client-server even in solo | Irrlicht renderer, single-threaded meshing |
| **Project Ascendant** (C++ + Vulkan + Flecs) | GPU-driven rendering, gigabuffer, indirect draw | Not a playable game, Flecs over EnTT for solo |
| **Minecraft** (Java) | Gameplay loop, biome system, cave culling | Java performance, closed source |
| **VoxelCore** (C++ + EnTT + Lua) | Clean modern structure | Smaller scope, OpenGL |
| **Veloren** (Rust + ECS) | Large-scale multiplayer architecture | Rust modding barrier |
| **Craft** (C, ~3500 lines) | Minimal viable voxel engine | Too simple for production |

## Constraints

- Solo developer — architecture must remain manageable by one person
- No multiplayer in V1 — but architecturally prepared via Command Pattern + ticks
- Performance target: 60 FPS at 16 chunks render distance on GTX 1660+
- Vulkan 1.3 required — no OpenGL fallback, excludes pre-2018 GPUs and macOS (without MoltenVK)

## Success Metrics

| Metric | Target | How to measure |
|--------|--------|----------------|
| Playable engine | Place/break blocks, move, infinite world | Manual testing |
| Lua modding | At least 1 working mod demonstrating the API | Mod loads and functions correctly |
| Meshing perf | < 200μs/chunk single-thread | Catch2 BENCHMARK macro |
| GPU upload | < 1ms/frame | GPU profiler (RenderDoc) |
| Framerate | 60 FPS at 16 chunks RD, GTX 1660 | In-game F3 overlay |
| Documentation | Complete BMAD artifacts + Doxygen | All artifacts exist and pass review |

## Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Vulkan complexity | High | High | vk-bootstrap + VMA abstractions, vkguide.dev tutorial |
| Scope creep | High | High | Strict BMAD methodology, epic by epic, out-of-scope list |
| Meshing performance | Low | Medium | Binary greedy meshing proven by public benchmarks |
| Lua scripting overhead | Low | Low | LuaJIT near-C perf, rate-limited engine API calls |
| Build system complexity | Medium | Medium | vcpkg manifest mode, CMakePresets.json |
| Cross-platform issues | Medium | Low | Windows primary, Linux secondary, CI on both |
