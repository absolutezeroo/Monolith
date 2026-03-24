cmake_minimum_required(VERSION 3.25)
project(VoxelForge VERSION 0.1.0 LANGUAGES CXX)

# Per architecture spec: Release uses -O2, not CMake default -O3 (GCC/Clang)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 23)    # Bumped from 20 for std::expected (ADR-001)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake modules
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# Enable testing
enable_testing()

# Subdirectories
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(tests)
