# engine/CMakeLists.txt — VoxelEngine static library

add_library(VoxelEngine STATIC
src/core/CoreModule.cpp
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

target_link_libraries(VoxelEngine
    PUBLIC  glm::glm
    PUBLIC  spdlog::spdlog
    PUBLIC  EnTT::EnTT
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
)
