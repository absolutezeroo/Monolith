// ImGuiImplGlfw.cpp — Compile the ImGui GLFW backend with project-correct defines.
// Separated from ImGuiImpl.cpp (Vulkan backend) because both files independently
// define Vulkan types, causing redefinition errors if compiled in a single TU.

#include <imgui_impl_glfw.cpp>
