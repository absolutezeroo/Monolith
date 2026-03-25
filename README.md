# VoxelForge

[![CI](https://github.com/absolutezeroo/Monolith/actions/workflows/ci.yml/badge.svg)](https://github.com/absolutezeroo/Monolith/actions/workflows/ci.yml)

A Minecraft-like voxel engine built with C++20/23 and Vulkan 1.3.

## Build

Requires CMake 3.25+, vcpkg, and a C++20 compiler (MSVC 2022 or GCC 13+).

```bash
# Configure and build (Debug)
cmake --preset debug
cmake --build --preset debug

# Run tests
ctest --preset debug
```

See `CMakePresets.json` for all available presets (`debug`, `release`, `msvc-debug`, `msvc-release`).
