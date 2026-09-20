#include "imgui_impl_sextant.h"
#include "../window_link.h"
#include "../platform/platform.h"
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>   // key constants, and the clipboard read below

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <string>
#include <vector>

namespace sextant {
    namespace {
        struct Backend {
            WindowLink* link = nullptr;
            std::chrono::steady_clock::time_point last_time{};
            bool have_time = false;

            // The last position a motion event reported, restored when the pointer
            // comes back (X11's spurious leave/enter pairs, imgui #4984).
            ImVec2 last_mouse_pos{0.0f, 0.0f};
            bool mouse_inside = false;

            WindowCursor last_cursor = WindowCursor::Count; // nothing posted yet
            std::string clipboard;                          // held for the caller
            std::vector<WindowEvent> events;
        };

        Backend* backend() {
            return ImGui::GetCurrentContext()
                       ? static_cast<Backend *>(ImGui::GetIO().BackendPlatformUserData)
                       : nullptr;
        }

        // GLFW keycode -> ImGuiKey. Copied from imgui_impl_glfw.cpp's
        // ImGui_ImplGlfw_KeyToImGuiKey (MIT), which this ImGui version does not
        // declare in its header.
        ImGuiKey to_imgui_key(int keycode) {
            switch (keycode) {
                case GLFW_KEY_TAB: return ImGuiKey_Tab;
                case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
                case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
                case GLFW_KEY_UP: return ImGuiKey_UpArrow;
                case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
                case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
                case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
                case GLFW_KEY_HOME: return ImGuiKey_Home;
                case GLFW_KEY_END: return ImGuiKey_End;
                case GLFW_KEY_INSERT: return ImGuiKey_Insert;
                case GLFW_KEY_DELETE: return ImGuiKey_Delete;
                case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
                case GLFW_KEY_SPACE: return ImGuiKey_Space;
                case GLFW_KEY_ENTER: return ImGuiKey_Enter;
                case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
                case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
                case GLFW_KEY_COMMA: return ImGuiKey_Comma;
                case GLFW_KEY_MINUS: return ImGuiKey_Minus;
                case GLFW_KEY_PERIOD: return ImGuiKey_Period;
                case GLFW_KEY_SLASH: return ImGuiKey_Slash;
                case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
                case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
                case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
                case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
                case GLFW_KEY_WORLD_1: return ImGuiKey_Oem102;
                case GLFW_KEY_WORLD_2: return ImGuiKey_Oem102;
                case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
                case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
                case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
                case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
                case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
                case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
                case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
                case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
                case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
                case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
                case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
                case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
                case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
                case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
                case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
                case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
                case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
                case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
                case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
                case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
                case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
                case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
                case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
                case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
                case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
                case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
                case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
                case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
                case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
                case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
                case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
                case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
                case GLFW_KEY_MENU: return ImGuiKey_Menu;
                case GLFW_KEY_0: return ImGuiKey_0;
                case GLFW_KEY_1: return ImGuiKey_1;
                case GLFW_KEY_2: return ImGuiKey_2;
                case GLFW_KEY_3: return ImGuiKey_3;
                case GLFW_KEY_4: return ImGuiKey_4;
                case GLFW_KEY_5: return ImGuiKey_5;
                case GLFW_KEY_6: return ImGuiKey_6;
                case GLFW_KEY_7: return ImGuiKey_7;
                case GLFW_KEY_8: return ImGuiKey_8;
                case GLFW_KEY_9: return ImGuiKey_9;
                case GLFW_KEY_A: return ImGuiKey_A;
                case GLFW_KEY_B: return ImGuiKey_B;
                case GLFW_KEY_C: return ImGuiKey_C;
                case GLFW_KEY_D: return ImGuiKey_D;
                case GLFW_KEY_E: return ImGuiKey_E;
                case GLFW_KEY_F: return ImGuiKey_F;
                case GLFW_KEY_G: return ImGuiKey_G;
                case GLFW_KEY_H: return ImGuiKey_H;
                case GLFW_KEY_I: return ImGuiKey_I;
                case GLFW_KEY_J: return ImGuiKey_J;
                case GLFW_KEY_K: return ImGuiKey_K;
                case GLFW_KEY_L: return ImGuiKey_L;
                case GLFW_KEY_M: return ImGuiKey_M;
                case GLFW_KEY_N: return ImGuiKey_N;
                case GLFW_KEY_O: return ImGuiKey_O;
                case GLFW_KEY_P: return ImGuiKey_P;
                case GLFW_KEY_Q: return ImGuiKey_Q;
                case GLFW_KEY_R: return ImGuiKey_R;
                case GLFW_KEY_S: return ImGuiKey_S;
                case GLFW_KEY_T: return ImGuiKey_T;
                case GLFW_KEY_U: return ImGuiKey_U;
                case GLFW_KEY_V: return ImGuiKey_V;
                case GLFW_KEY_W: return ImGuiKey_W;
                case GLFW_KEY_X: return ImGuiKey_X;
                case GLFW_KEY_Y: return ImGuiKey_Y;
                case GLFW_KEY_Z: return ImGuiKey_Z;
                case GLFW_KEY_F1: return ImGuiKey_F1;
                case GLFW_KEY_F2: return ImGuiKey_F2;
                case GLFW_KEY_F3: return ImGuiKey_F3;
                case GLFW_KEY_F4: return ImGuiKey_F4;
                case GLFW_KEY_F5: return ImGuiKey_F5;
                case GLFW_KEY_F6: return ImGuiKey_F6;
                case GLFW_KEY_F7: return ImGuiKey_F7;
                case GLFW_KEY_F8: return ImGuiKey_F8;
                case GLFW_KEY_F9: return ImGuiKey_F9;
                case GLFW_KEY_F10: return ImGuiKey_F10;
                case GLFW_KEY_F11: return ImGuiKey_F11;
                case GLFW_KEY_F12: return ImGuiKey_F12;
                case GLFW_KEY_F13: return ImGuiKey_F13;
                case GLFW_KEY_F14: return ImGuiKey_F14;
                case GLFW_KEY_F15: return ImGuiKey_F15;
                case GLFW_KEY_F16: return ImGuiKey_F16;
                case GLFW_KEY_F17: return ImGuiKey_F17;
                case GLFW_KEY_F18: return ImGuiKey_F18;
                case GLFW_KEY_F19: return ImGuiKey_F19;
                case GLFW_KEY_F20: return ImGuiKey_F20;
                case GLFW_KEY_F21: return ImGuiKey_F21;
                case GLFW_KEY_F22: return ImGuiKey_F22;
                case GLFW_KEY_F23: return ImGuiKey_F23;
                case GLFW_KEY_F24: return ImGuiKey_F24;
                default: return ImGuiKey_None;
            }
        }

        WindowCursor to_window_cursor(ImGuiMouseCursor c) {
            switch (c) {
                case ImGuiMouseCursor_TextInput: return WindowCursor::TextInput;
                case ImGuiMouseCursor_ResizeAll: return WindowCursor::ResizeAll;
                case ImGuiMouseCursor_ResizeNS: return WindowCursor::ResizeNS;
                case ImGuiMouseCursor_ResizeEW: return WindowCursor::ResizeEW;
                case ImGuiMouseCursor_ResizeNESW: return WindowCursor::ResizeNESW;
                case ImGuiMouseCursor_ResizeNWSE: return WindowCursor::ResizeNWSE;
                case ImGuiMouseCursor_Hand: return WindowCursor::Hand;
                case ImGuiMouseCursor_NotAllowed: return WindowCursor::NotAllowed;
                default: return WindowCursor::Arrow; // Wait and Progress included
            }
        }

        // Before every key and button event, as imgui_impl_glfw does: the mask was
        // read off the keyboard when the event arrived.
        void add_mods(ImGuiIO& io, int mods) {
            io.AddKeyEvent(ImGuiMod_Ctrl, (mods & ModCtrl) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (mods & ModShift) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (mods & ModAlt) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (mods & ModSuper) != 0);
        }

        void replay(ImGuiIO& io, Backend& bd, const WindowEvent& e) {
            switch (e.kind) {
                case WindowEvent::Kind::MousePos:
                    bd.last_mouse_pos = ImVec2(static_cast<float>(e.x), static_cast<float>(e.y));
                    io.AddMousePosEvent(bd.last_mouse_pos.x, bd.last_mouse_pos.y);
                    break;
                case WindowEvent::Kind::MouseEnter:
                    bd.mouse_inside = true;
                    io.AddMousePosEvent(bd.last_mouse_pos.x, bd.last_mouse_pos.y);
                    break;
                case WindowEvent::Kind::MouseLeave:
                    bd.mouse_inside = false;
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    break;
                case WindowEvent::Kind::MouseButton:
                    add_mods(io, e.mods);
                    if (e.key >= 0 && e.key < ImGuiMouseButton_COUNT)
                        io.AddMouseButtonEvent(e.key, e.down);
                    break;
                case WindowEvent::Kind::Scroll:
                    io.AddMouseWheelEvent(static_cast<float>(e.x), static_cast<float>(e.y));
                    break;
                case WindowEvent::Kind::Key: {
                    add_mods(io, e.mods);
                    const ImGuiKey key = to_imgui_key(e.key);
                    io.AddKeyEvent(key, e.down);
                    io.SetKeyEventNativeData(key, e.key, e.scancode);
                    break;
                }
                case WindowEvent::Kind::Char:
                    io.AddInputCharacter(e.codepoint);
                    break;
                case WindowEvent::Kind::Focus:
                    io.AddFocusEvent(e.down);
                    break;
            }
        }
    } // namespace

    void ImGui_ImplSextant_Init(WindowLink& link) {
        ImGuiIO& io = ImGui::GetIO();
        IM_ASSERT(io.BackendPlatformUserData == nullptr &&
            "a platform backend is already installed on this context");

        auto* bd = IM_NEW(Backend)();
        bd->link = &link;
        io.BackendPlatformUserData = bd;
        io.BackendPlatformName = "imgui_impl_sextant";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        // Not set: HasSetMousePos (io.WantSetMousePos needs a GLFW call from this
        // thread and only ConfigNavMoveSetMousePos asks for it), the viewport
        // flags (docking only, no second OS window), and HasGamepad.

        // ImGui calls both with this context current, so backend() finds the
        // right one; the ImGuiContext* they are passed is the same context.
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            if (Backend* b = backend()) b->link->post_clipboard(text ? text : "");
        };
        // Read on the render thread, because a paste wants an answer this frame
        // and not next poll. Where the platform has a thread-safe clipboard of
        // its own (macOS: NSPasteboard) that is what answers; on Windows and X11
        // it is the one GLFW call left off the pumping thread, and harmless.
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            Backend* b = backend();
            if (!b) return "";
            if (!platform::read_clipboard(b->clipboard)) {
                const char* s = glfwGetClipboardString(nullptr);
                b->clipboard = s ? s : "";
            }
            return b->clipboard.c_str();
        };
    }

    void ImGui_ImplSextant_NewFrame(WindowLink& link) {
        ImGuiIO& io = ImGui::GetIO();
        Backend* bd = backend();
        IM_ASSERT(bd != nullptr && "ImGui_ImplSextant_Init() was not called on this context");

        int ww = 0, wh = 0, fw = 0, fh = 0;
        link.window_size(ww, wh);
        link.framebuffer_size(fw, fh);
        io.DisplaySize = ImVec2(static_cast<float>(ww), static_cast<float>(wh));
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        // X11 (what ensure_glfw_init() pins) reports a framebuffer in window
        // coordinates, so the ratio is 1 by construction -- imgui_impl_glfw makes
        // the same exception, since it is Wayland that scales.
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#else
        io.DisplayFramebufferScale = ImVec2(
            ww > 0 ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f,
            wh > 0 ? static_cast<float>(fh) / static_cast<float>(wh) : 1.0f);
#endif

        // steady_clock rather than glfwGetTime(), which is neither this thread's
        // to read on macOS nor guaranteed to move forward.
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = bd->have_time
                           ? std::max(1e-6f, std::chrono::duration<float>(now - bd->last_time).count())
                           : 1.0f / 60.0f;
        bd->last_time = now;
        bd->have_time = true;

        link.take_events(bd->events);
        for (const WindowEvent& e: bd->events) replay(io, *bd, e);

        // With the pointer outside the window but the window focused -- a drag
        // that left it -- no motion event arrives, so the mirror's position
        // stands in. Where imgui_impl_glfw calls glfwGetCursorPos().
        if (link.focused() && !bd->mouse_inside) {
            float cx = 0.0f, cy = 0.0f;
            link.cursor_pos(cx, cy);
            io.AddMousePosEvent(cx, cy);
        }

        // The shape last frame's UI asked for, posted only when it changes.
        if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) {
            bd->last_cursor = WindowCursor::Count; // re-post once it is allowed again
        } else {
            const ImGuiMouseCursor c = ImGui::GetMouseCursor();
            const WindowCursor want = (c == ImGuiMouseCursor_None || io.MouseDrawCursor)
                                          ? WindowCursor::Hidden
                                          : to_window_cursor(c);
            if (want != bd->last_cursor) {
                bd->last_cursor = want;
                link.post_cursor(want);
            }
        }
    }

    void ImGui_ImplSextant_Shutdown() {
        ImGuiIO& io = ImGui::GetIO();
        Backend* bd = backend();
        IM_ASSERT(bd != nullptr && "no platform backend to shut down");

        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_SetClipboardTextFn = nullptr;
        pio.Platform_GetClipboardTextFn = nullptr;

        io.BackendPlatformName = nullptr;
        io.BackendPlatformUserData = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
        IM_DELETE(bd);
    }
} // namespace sextant
