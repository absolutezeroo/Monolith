# engine/CMakeLists.txt — VoxelEngine static library

add_library(VoxelEngine STATIC
        src/core/CoreModule.cpp
        src/core/ConfigManager.cpp
        src/core/JobSystem.cpp
        src/core/Log.cpp
        src/game/Window.cpp
        src/game/GameLoop.cpp
        src/renderer/VmaImpl.cpp
        src/renderer/VulkanContext.cpp
        src/renderer/DescriptorAllocator.cpp
        src/renderer/Renderer.cpp
        src/renderer/Gigabuffer.cpp
        src/renderer/StagingBuffer.cpp
        src/renderer/Camera.cpp
        src/renderer/ImGuiBackend.cpp
        src/renderer/ImGuiImpl.cpp
        src/renderer/ImGuiImplGlfw.cpp
        src/renderer/StbImageWriteImpl.cpp
        src/world/ChunkSection.cpp
        src/world/ChunkColumn.cpp
        src/world/BlockRegistry.cpp
        src/world/ChunkManager.cpp
        src/world/PaletteCompression.cpp
        src/world/RegionFile.cpp
        src/world/ChunkSerializer.cpp
        src/world/BiomeSystem.cpp
        src/world/CaveCarver.cpp
        src/world/SplineCurve.cpp
        src/world/StructureGenerator.cpp
        src/renderer/QuadIndexBuffer.cpp
        src/renderer/ChunkUploadManager.cpp
        src/renderer/MeshBuilder.cpp
        src/renderer/ModelMesher.cpp
        src/renderer/ModelRegistry.cpp
        src/renderer/TintPalette.cpp
        src/world/WorldGenerator.cpp
        src/input/InputManager.cpp
)

target_include_directories(VoxelEngine
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
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
find_package(volk CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(enkiTS CONFIG REQUIRED)

target_link_libraries(VoxelEngine
        PUBLIC glm::glm
        PUBLIC spdlog::spdlog
        PUBLIC EnTT::EnTT
        PRIVATE glfw
        PUBLIC volk::volk_headers
        PRIVATE volk::volk
        PRIVATE vk-bootstrap::vk-bootstrap
        PUBLIC GPUOpen::VulkanMemoryAllocator
        PRIVATE imgui::imgui
        PRIVATE nlohmann_json::nlohmann_json
        PRIVATE lz4::lz4
        PUBLIC enkiTS::enkiTS
)
target_include_directories(VoxelEngine PRIVATE ${Stb_INCLUDE_DIR})

# spdlog: disable exceptions (project uses -fno-exceptions)
target_compile_definitions(VoxelEngine PUBLIC SPDLOG_NO_EXCEPTIONS)

# nlohmann-json: disable exceptions (uses std::abort on parse errors instead)
target_compile_definitions(VoxelEngine PRIVATE JSON_NOEXCEPTION)

# volk: prevent static linking to Vulkan loader — consumers need this via VulkanContext.h
target_compile_definitions(VoxelEngine PUBLIC VK_NO_PROTOTYPES)

# VMA: use volk for function loading, not VMA's own dynamic loading
target_compile_definitions(VoxelEngine PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)

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
        <volk.h>
        <glm/glm.hpp>
        <spdlog/spdlog.h>
        <entt/entt.hpp>
        <GLFW/glfw3.h>
        <imgui.h>
        <enkiTS/TaskScheduler.h>
)

# ImGui backends: locate source files from vcpkg buildtrees.
# We compile them ourselves (via ImGuiImpl.cpp) to ensure VK_NO_PROTOTYPES is set,
# because vcpkg's pre-compiled backends don't have this define.
file(GLOB _imgui_backend_dirs
        "${CMAKE_BINARY_DIR}/vcpkg_installed/vcpkg/blds/imgui/src/*/backends"
        "${VCPKG_INSTALLED_DIR}/vcpkg/blds/imgui/src/*/backends"
)
if (_imgui_backend_dirs)
    list(GET _imgui_backend_dirs 0 IMGUI_BACKENDS_DIR)
    message(STATUS "ImGui backends found: ${IMGUI_BACKENDS_DIR}")
    # Make backend headers available to the entire engine (for ImGuiBackend.cpp etc.)
    target_include_directories(VoxelEngine PRIVATE "${IMGUI_BACKENDS_DIR}")
else ()
    message(FATAL_ERROR
            "Could not find imgui backends in vcpkg buildtrees.\n"
            "Searched: ${CMAKE_BINARY_DIR}/vcpkg_installed/vcpkg/blds/imgui/src/*/backends\n"
            "Ensure vcpkg manifest mode is enabled and imgui is installed.")
endif ()

# ImGuiImpl.cpp and ImGuiImplGlfw.cpp each #include a backend .cpp that pulls in
# <vulkan/vulkan.h> directly, which conflicts with volk.h from the PCH.
# Skip PCH and suppress warnings for these TUs — the backends are third-party code.
set_source_files_properties(
        src/renderer/ImGuiImpl.cpp
        src/renderer/ImGuiImplGlfw.cpp
        PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        INCLUDE_DIRECTORIES "${IMGUI_BACKENDS_DIR}"
)
if (MSVC)
    set_property(SOURCE src/renderer/ImGuiImpl.cpp src/renderer/ImGuiImplGlfw.cpp
            APPEND_STRING PROPERTY COMPILE_FLAGS " /w")
else ()
    set_property(SOURCE src/renderer/ImGuiImpl.cpp src/renderer/ImGuiImplGlfw.cpp
            APPEND_STRING PROPERTY COMPILE_FLAGS " -w")
endif ()

# StbImageWriteImpl.cpp — single-header implementation; skip PCH and suppress warnings.
set_source_files_properties(src/renderer/StbImageWriteImpl.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (MSVC)
    set_property(SOURCE src/renderer/StbImageWriteImpl.cpp
            APPEND_STRING PROPERTY COMPILE_FLAGS " /w")
else ()
    set_property(SOURCE src/renderer/StbImageWriteImpl.cpp
            APPEND_STRING PROPERTY COMPILE_FLAGS " -w")
endif ()
