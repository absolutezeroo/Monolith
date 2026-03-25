// ImGuiImpl.cpp — Compile the ImGui Vulkan backend with project-correct defines.
// VK_NO_PROTOTYPES is set globally via CMake. IMGUI_IMPL_VULKAN_NO_PROTOTYPES
// is auto-detected by imgui_impl_vulkan.h when VK_NO_PROTOTYPES is defined.
//
// The GLFW and Vulkan backends MUST be compiled in separate translation units
// because each defines Vulkan types independently, causing redefinition errors
// if combined. See ImGuiImplGlfw.cpp for the GLFW backend.

#include <imgui_impl_vulkan.cpp>
