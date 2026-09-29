#include "key_names.h"
#include "window_link.h"
#include <GLFW/glfw3.h>

namespace sextant {
namespace {
    // The modifier keys, by name, or null.
    const char* modifier_name(int key) {
        switch (key) {
            case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_RIGHT_SHIFT: return "shift";
            case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL: return "ctrl";
            case GLFW_KEY_LEFT_ALT: case GLFW_KEY_RIGHT_ALT: return "alt";
            case GLFW_KEY_LEFT_SUPER: case GLFW_KEY_RIGHT_SUPER: return "super";
            default: return nullptr;
        }
    }

    // The key without modifiers; `letter` says the name is a single a-z, which
    // shift upper-cases instead of prefixing.
    std::string base_name(int key, bool& letter) {
        letter = false;
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            letter = true;
            return std::string(1, static_cast<char>('a' + (key - GLFW_KEY_A)));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
            return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
            return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_KP_0)));
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
            return "f" + std::to_string(key - GLFW_KEY_F1 + 1);

        switch (key) {
            case GLFW_KEY_SPACE: return "space";
            case GLFW_KEY_APOSTROPHE: return "'";
            case GLFW_KEY_COMMA: return ",";
            case GLFW_KEY_MINUS: return "-";
            case GLFW_KEY_PERIOD: return ".";
            case GLFW_KEY_SLASH: return "/";
            case GLFW_KEY_SEMICOLON: return ";";
            case GLFW_KEY_EQUAL: return "=";
            case GLFW_KEY_LEFT_BRACKET: return "[";
            case GLFW_KEY_BACKSLASH: return "\\";
            case GLFW_KEY_RIGHT_BRACKET: return "]";
            case GLFW_KEY_GRAVE_ACCENT: return "`";
            case GLFW_KEY_ESCAPE: return "escape";
            case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return "enter";
            case GLFW_KEY_TAB: return "tab";
            case GLFW_KEY_BACKSPACE: return "backspace";
            case GLFW_KEY_INSERT: return "insert";
            case GLFW_KEY_DELETE: return "delete";
            case GLFW_KEY_RIGHT: return "right";
            case GLFW_KEY_LEFT: return "left";
            case GLFW_KEY_DOWN: return "down";
            case GLFW_KEY_UP: return "up";
            case GLFW_KEY_PAGE_UP: return "pageup";
            case GLFW_KEY_PAGE_DOWN: return "pagedown";
            case GLFW_KEY_HOME: return "home";
            case GLFW_KEY_END: return "end";
            case GLFW_KEY_CAPS_LOCK: return "caps_lock";
            case GLFW_KEY_SCROLL_LOCK: return "scroll_lock";
            case GLFW_KEY_NUM_LOCK: return "num_lock";
            case GLFW_KEY_PRINT_SCREEN: return "print_screen";
            case GLFW_KEY_PAUSE: return "pause";
            case GLFW_KEY_MENU: return "menu";
            case GLFW_KEY_KP_DECIMAL: return ".";
            case GLFW_KEY_KP_DIVIDE: return "/";
            case GLFW_KEY_KP_MULTIPLY: return "*";
            case GLFW_KEY_KP_SUBTRACT: return "-";
            case GLFW_KEY_KP_ADD: return "+";
            case GLFW_KEY_KP_EQUAL: return "=";
            default: return {};
        }
    }
} // namespace

std::string key_event_name(int glfw_key, int mods) {
    if (const char* m = modifier_name(glfw_key)) return m;

    bool letter = false;
    std::string name = base_name(glfw_key, letter);
    if (name.empty()) return {};

    const bool shift = (mods & ModShift) != 0;
    if (letter && shift) name[0] = static_cast<char>(name[0] - 'a' + 'A');

    std::string out;
    if (mods & ModCtrl) out += "ctrl+";
    if (mods & ModAlt) out += "alt+";
    if (mods & ModSuper) out += "super+";
    if (shift && !letter) out += "shift+";
    return out + name;
}
} // namespace sextant
