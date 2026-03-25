// ImGuiImpl.cpp — Compile ImGui backends with project-correct defines.
// VK_NO_PROTOTYPES is set globally via CMake. IMGUI_IMPL_VULKAN_NO_PROTOTYPES
// is auto-detected by imgui_impl_vulkan.h when VK_NO_PROTOTYPES is defined.
//
// We compile the backend .cpp files ourselves instead of using vcpkg's
// pre-compiled versions because vcpkg may compile without VK_NO_PROTOTYPES.
// Backend sources are copied from vcpkg's imgui docking branch.

#include <imgui_impl_glfw.cpp>
#include <imgui_impl_vulkan.cpp>
