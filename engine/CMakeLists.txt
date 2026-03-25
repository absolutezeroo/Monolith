# engine/CMakeLists.txt — VoxelEngine static library

add_library(VoxelEngine STATIC
    src/core/CoreModule.cpp
    src/core/Log.cpp
    src/game/Window.cpp
    src/game/GameLoop.cpp
)

target_include_directories(VoxelEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compiler warnings and sanitizers
voxelforge_set_warnings(VoxelEngine)
voxelforge_enable_sanitizers(VoxelEngine)

# vcpkg dependencies — only link what is needed now
# PUBLIC: headers exposed to consumers of VoxelEngine
# PRIVATE: implementation-only dependencies
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(VoxelEngine
    PUBLIC  glm::glm
    PUBLIC  spdlog::spdlog
    PUBLIC  EnTT::EnTT
    PRIVATE glfw
)

# spdlog: disable exceptions (project uses -fno-exceptions)
target_compile_definitions(VoxelEngine PUBLIC SPDLOG_NO_EXCEPTIONS)

# GLM: Vulkan depth range [0,1] and left-handed coordinate system
target_compile_definitions(VoxelEngine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_LEFT_HANDED
)

# Windows: prevent <windows.h> from defining min/max macros
target_compile_definitions(VoxelEngine PUBLIC NOMINMAX)

# spdlog compile-time log level filtering
target_compile_definitions(VoxelEngine PUBLIC
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
)

# Precompiled headers (PRIVATE — not forced on consumers)
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
    <GLFW/glfw3.h>
)
