#include "voxel/scripting/InputEventTracker.h"

#include "voxel/input/InputManager.h"
#include "voxel/scripting/EntityHandle.h"
#include "voxel/scripting/GlobalEventRegistry.h"

#include <GLFW/glfw3.h>

#include <cmath>

namespace voxel::scripting
{

void InputEventTracker::update(
    const input::InputManager& input,
    GlobalEventRegistry& registry,
    EntityHandle& entity)
{
    // --- Keys ---
    for (int k = 0; k < MAX_TRACKED_KEYS; ++k)
    {
        if (input.wasKeyPressed(k))
        {
            (void)registry.fireCancelableEvent("key_pressed", std::ref(entity), keyName(k));
        }
        if (input.wasKeyReleased(k))
        {
            registry.fireEvent("key_released", std::ref(entity), keyName(k), input.getKeyHoldDuration(k));
        }
        if (input.isKeyDown(k))
        {
            registry.fireEvent("key_held", std::ref(entity), keyName(k), input.getKeyHoldDuration(k));
        }
        if (input.wasKeyDoubleTapped(k))
        {
            (void)registry.fireCancelableEvent("key_double_tap", std::ref(entity), keyName(k));
        }
    }

    // --- Mouse buttons ---
    for (int b = 0; b < MAX_TRACKED_BUTTONS; ++b)
    {
        if (input.wasMouseButtonPressed(b))
        {
            (void)registry.fireCancelableEvent("mouse_click", std::ref(entity), buttonName(b));
        }
        if (input.wasMouseButtonReleased(b))
        {
            registry.fireEvent("mouse_released", std::ref(entity), buttonName(b), input.getMouseButtonHoldDuration(b));
        }
        if (input.isMouseButtonDown(b))
        {
            registry.fireEvent("mouse_held", std::ref(entity), buttonName(b), input.getMouseButtonHoldDuration(b));
        }
    }

    // --- Scroll wheel ---
    float scroll = input.getScrollDelta();
    if (std::abs(scroll) > 0.001f)
    {
        int delta = scroll > 0.0f ? 1 : -1;
        (void)registry.fireCancelableEvent("scroll_wheel", std::ref(entity), delta);
    }
}

std::string_view InputEventTracker::keyName(int glfwKey)
{
    switch (glfwKey)
    {
    // Letters
    case GLFW_KEY_A: return "a";
    case GLFW_KEY_B: return "b";
    case GLFW_KEY_C: return "c";
    case GLFW_KEY_D: return "d";
    case GLFW_KEY_E: return "e";
    case GLFW_KEY_F: return "f";
    case GLFW_KEY_G: return "g";
    case GLFW_KEY_H: return "h";
    case GLFW_KEY_I: return "i";
    case GLFW_KEY_J: return "j";
    case GLFW_KEY_K: return "k";
    case GLFW_KEY_L: return "l";
    case GLFW_KEY_M: return "m";
    case GLFW_KEY_N: return "n";
    case GLFW_KEY_O: return "o";
    case GLFW_KEY_P: return "p";
    case GLFW_KEY_Q: return "q";
    case GLFW_KEY_R: return "r";
    case GLFW_KEY_S: return "s";
    case GLFW_KEY_T: return "t";
    case GLFW_KEY_U: return "u";
    case GLFW_KEY_V: return "v";
    case GLFW_KEY_W: return "w";
    case GLFW_KEY_X: return "x";
    case GLFW_KEY_Y: return "y";
    case GLFW_KEY_Z: return "z";

    // Numbers
    case GLFW_KEY_0: return "0";
    case GLFW_KEY_1: return "1";
    case GLFW_KEY_2: return "2";
    case GLFW_KEY_3: return "3";
    case GLFW_KEY_4: return "4";
    case GLFW_KEY_5: return "5";
    case GLFW_KEY_6: return "6";
    case GLFW_KEY_7: return "7";
    case GLFW_KEY_8: return "8";
    case GLFW_KEY_9: return "9";

    // Function keys
    case GLFW_KEY_F1: return "f1";
    case GLFW_KEY_F2: return "f2";
    case GLFW_KEY_F3: return "f3";
    case GLFW_KEY_F4: return "f4";
    case GLFW_KEY_F5: return "f5";
    case GLFW_KEY_F6: return "f6";
    case GLFW_KEY_F7: return "f7";
    case GLFW_KEY_F8: return "f8";
    case GLFW_KEY_F9: return "f9";
    case GLFW_KEY_F10: return "f10";
    case GLFW_KEY_F11: return "f11";
    case GLFW_KEY_F12: return "f12";

    // Modifiers
    case GLFW_KEY_LEFT_SHIFT: return "left_shift";
    case GLFW_KEY_RIGHT_SHIFT: return "right_shift";
    case GLFW_KEY_LEFT_CONTROL: return "left_ctrl";
    case GLFW_KEY_RIGHT_CONTROL: return "right_ctrl";
    case GLFW_KEY_LEFT_ALT: return "left_alt";
    case GLFW_KEY_RIGHT_ALT: return "right_alt";

    // Special
    case GLFW_KEY_SPACE: return "space";
    case GLFW_KEY_ESCAPE: return "escape";
    case GLFW_KEY_ENTER: return "enter";
    case GLFW_KEY_TAB: return "tab";
    case GLFW_KEY_BACKSPACE: return "backspace";
    case GLFW_KEY_DELETE: return "delete";
    case GLFW_KEY_INSERT: return "insert";
    case GLFW_KEY_HOME: return "home";
    case GLFW_KEY_END: return "end";
    case GLFW_KEY_PAGE_UP: return "page_up";
    case GLFW_KEY_PAGE_DOWN: return "page_down";

    // Arrow keys
    case GLFW_KEY_UP: return "up";
    case GLFW_KEY_DOWN: return "down";
    case GLFW_KEY_LEFT: return "left";
    case GLFW_KEY_RIGHT: return "right";

    // Punctuation
    case GLFW_KEY_MINUS: return "minus";
    case GLFW_KEY_EQUAL: return "equal";
    case GLFW_KEY_LEFT_BRACKET: return "left_bracket";
    case GLFW_KEY_RIGHT_BRACKET: return "right_bracket";
    case GLFW_KEY_BACKSLASH: return "backslash";
    case GLFW_KEY_SEMICOLON: return "semicolon";
    case GLFW_KEY_APOSTROPHE: return "apostrophe";
    case GLFW_KEY_COMMA: return "comma";
    case GLFW_KEY_PERIOD: return "period";
    case GLFW_KEY_SLASH: return "slash";
    case GLFW_KEY_GRAVE_ACCENT: return "grave";

    default: return "unknown";
    }
}

std::string_view InputEventTracker::buttonName(int glfwButton)
{
    switch (glfwButton)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return "left";
    case GLFW_MOUSE_BUTTON_RIGHT: return "right";
    case GLFW_MOUSE_BUTTON_MIDDLE: return "middle";
    case GLFW_MOUSE_BUTTON_4: return "button4";
    case GLFW_MOUSE_BUTTON_5: return "button5";
    case GLFW_MOUSE_BUTTON_6: return "button6";
    case GLFW_MOUSE_BUTTON_7: return "button7";
    case GLFW_MOUSE_BUTTON_8: return "button8";
    default: return "unknown";
    }
}

} // namespace voxel::scripting
