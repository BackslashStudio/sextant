#include "window_link.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstring>
#include <utility>

// GLFW's own version, in the form imgui_impl_glfw.cpp compares against.
#define SEXTANT_GLFW_COMBINED (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + \
                               GLFW_VERSION_REVISION * 10)

namespace sextant {
    namespace {
        WindowLink& link_of(GLFWwindow* w) {
            return *static_cast<WindowLink *>(glfwGetWindowUserPointer(w));
        }

        // The modifiers as the keyboard has them now, rather than as the event
        // reports them. Pumping thread only (glfwGetKey).
        int resolve_mods(GLFWwindow* w) {
            const auto down = [w](int a, int b) {
                return glfwGetKey(w, a) == GLFW_PRESS || glfwGetKey(w, b) == GLFW_PRESS;
            };
            int m = 0;
            if (down(GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL)) m |= ModCtrl;
            if (down(GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT)) m |= ModShift;
            if (down(GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT)) m |= ModAlt;
            if (down(GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER)) m |= ModSuper;
            return m;
        }

        // GLFW reports the character a layout produces rather than the key that
        // was struck, so a lettered shortcut moves with the layout. Undo that by
        // the key's name, which is what every other toolkit reports. From
        // imgui_impl_glfw.cpp's ImGui_ImplGlfw_TranslateUntranslatedKey (MIT);
        // glfwGetKeyName is what puts it on the pumping thread.
        int untranslate_key(int key, int scancode) {
#if SEXTANT_GLFW_COMBINED >= 3200
            if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL) return key;
            GLFWerrorfun prev = glfwSetErrorCallback(nullptr);
            const char* name = glfwGetKeyName(key, scancode);
            glfwSetErrorCallback(prev);
            (void) glfwGetError(nullptr); // eat the error a nameless key raises
            if (name && name[0] != 0 && name[1] == 0) {
                static const char char_names[] = {'`', '-', '=', '[', ']', '\\',
                                                  ',', ';', '\'', '.', '/', 0};
                static const int char_keys[] = {
                    GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET,
                    GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON,
                    GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0
                };
                if (name[0] >= '0' && name[0] <= '9') key = GLFW_KEY_0 + (name[0] - '0');
                else if (name[0] >= 'A' && name[0] <= 'Z') key = GLFW_KEY_A + (name[0] - 'A');
                else if (name[0] >= 'a' && name[0] <= 'z') key = GLFW_KEY_A + (name[0] - 'a');
                else if (const char* p = std::strchr(char_names, name[0]))
                    key = char_keys[p - char_names];
            }
#else
            (void) scancode;
#endif
            return key;
        }

        int glfw_cursor_shape(WindowCursor c) {
            switch (c) {
                case WindowCursor::TextInput: return GLFW_IBEAM_CURSOR;
                case WindowCursor::ResizeNS: return GLFW_VRESIZE_CURSOR;
                case WindowCursor::ResizeEW: return GLFW_HRESIZE_CURSOR;
                case WindowCursor::Hand: return GLFW_HAND_CURSOR;
#if SEXTANT_GLFW_COMBINED >= 3400
                case WindowCursor::ResizeAll: return GLFW_RESIZE_ALL_CURSOR;
                case WindowCursor::ResizeNESW: return GLFW_RESIZE_NESW_CURSOR;
                case WindowCursor::ResizeNWSE: return GLFW_RESIZE_NWSE_CURSOR;
                case WindowCursor::NotAllowed: return GLFW_NOT_ALLOWED_CURSOR;
#endif
                default: return GLFW_ARROW_CURSOR; // and the shapes GLFW 3.3 lacks
            }
        }
    } // namespace

    // -------------------------------------------------------------------------
    // The pumping thread
    // -------------------------------------------------------------------------
    void WindowLink::attach(GLFWwindow* w) {
        window_ = w;
        glfwSetWindowUserPointer(w, this);

        glfwSetCursorPosCallback(w, [](GLFWwindow* win, double x, double y) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MousePos;
            e.x = x;
            e.y = y;
            link_of(win).post_event(e);
        });
        // X11 sends spurious leave/enter pairs, so the position is restored on the
        // way back in rather than left where the leave put it (imgui #4984).
        glfwSetCursorEnterCallback(w, [](GLFWwindow* win, int entered) {
            WindowEvent e;
            e.kind = entered ? WindowEvent::Kind::MouseEnter : WindowEvent::Kind::MouseLeave;
            link_of(win).post_event(e);
        });
        glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int button, int action, int) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MouseButton;
            e.key = button;
            e.down = action == GLFW_PRESS;
            e.mods = resolve_mods(win);
            link_of(win).post_event(e);
        });
        glfwSetScrollCallback(w, [](GLFWwindow* win, double x, double y) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::Scroll;
            e.x = x;
            e.y = y;
            link_of(win).post_event(e);
        });
        glfwSetKeyCallback(w, [](GLFWwindow* win, int key, int scancode, int action, int) {
            if (action != GLFW_PRESS && action != GLFW_RELEASE) return; // no repeats
            WindowEvent e;
            e.kind = WindowEvent::Kind::Key;
            e.key = untranslate_key(key, scancode);
            e.scancode = scancode;
            e.down = action == GLFW_PRESS;
            e.mods = resolve_mods(win);
            link_of(win).post_event(e);
        });
        glfwSetCharCallback(w, [](GLFWwindow* win, unsigned int c) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::Char;
            e.codepoint = c;
            link_of(win).post_event(e);
        });
        glfwSetWindowFocusCallback(w, [](GLFWwindow* win, int focused) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::Focus;
            e.down = focused != 0;
            link_of(win).post_event(e);
        });

        sync_state();
    }

    float WindowLink::framebuffer_scale() const {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        // X11 (what ensure_glfw_init() pins) reports a framebuffer in window
        // coordinates, so the ratio is 1 by construction -- imgui_impl_glfw
        // makes the same exception, since it is Wayland that scales.
        return 1.0f;
#else
        int ww = 0, wh = 0, fw = 0, fh = 0;
        window_size(ww, wh);
        framebuffer_size(fw, fh);
        (void) wh;
        (void) fh;
        if (ww <= 0 || fw <= 0) return 1.0f;
        return static_cast<float>(fw) / static_cast<float>(ww);
#endif
    }

    float WindowLink::chrome_scale() const {
        return chrome_scale_from(content_scale(), framebuffer_scale());
    }

    void WindowLink::sync_state() {
        if (!window_) return;
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        win_.store(w, h);
        glfwGetFramebufferSize(window_, &w, &h);
        fb_.store(w, h);

        float sx = 1.0f, sy = 1.0f;
        glfwGetWindowContentScale(window_, &sx, &sy);
        if (sx > 0.0f) scale_.store(sx, std::memory_order_relaxed);

        focused_.store(glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0, std::memory_order_relaxed);
        hovered_.store(glfwGetWindowAttrib(window_, GLFW_HOVERED) != 0, std::memory_order_relaxed);

        double cx = 0.0, cy = 0.0;
        glfwGetCursorPos(window_, &cx, &cy);
        cursor_x_.store(static_cast<float>(cx), std::memory_order_relaxed);
        cursor_y_.store(static_cast<float>(cy), std::memory_order_relaxed);
    }

    void WindowLink::service_requests() {
        std::vector<Request> todo;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            todo.swap(requests_);
        }
        if (!window_) return;
        for (const Request& r: todo) {
            switch (r.kind) {
                case Request::Kind::Cursor: {
                    const auto c = static_cast<WindowCursor>(r.a);
                    if (c == cursor_) break;
                    cursor_ = c;
                    if (c == WindowCursor::Hidden) {
                        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                        break;
                    }
                    GLFWcursor*& slot = cursors_[static_cast<int>(c)];
                    if (!slot) {
                        // A shape X11 has no cursor for raises an error and gives
                        // null; the arrow stands in, as imgui_impl_glfw does.
                        GLFWerrorfun prev = glfwSetErrorCallback(nullptr);
                        slot = glfwCreateStandardCursor(glfw_cursor_shape(c));
                        glfwSetErrorCallback(prev);
                        (void) glfwGetError(nullptr);
                    }
                    glfwSetCursor(window_, slot);
                    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    break;
                }
                case Request::Kind::Resize:
                    glfwSetWindowSize(window_, r.a, r.b);
                    break;
                case Request::Kind::Clipboard:
                    glfwSetClipboardString(window_, r.text.c_str());
                    break;
            }
        }
    }

    void WindowLink::post_event(const WindowEvent& e) {
        std::lock_guard<std::mutex> lock(events_mutex_);
        events_.push_back(e);
    }

    WindowLink::~WindowLink() {
        for (GLFWcursor*& c: cursors_) {
            if (c) glfwDestroyCursor(c);
            c = nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // The render thread
    // -------------------------------------------------------------------------
    void WindowLink::take_events(std::vector<WindowEvent>& out) {
        out.clear();
        std::lock_guard<std::mutex> lock(events_mutex_);
        out.swap(events_);
    }

    void WindowLink::post_cursor(WindowCursor c) {
        Request r;
        r.kind = Request::Kind::Cursor;
        r.a = static_cast<int>(c);
        post(std::move(r));
    }

    void WindowLink::post_resize(int width, int height) {
        Request r;
        r.kind = Request::Kind::Resize;
        r.a = width;
        r.b = height;
        post(std::move(r));
    }

    void WindowLink::post_clipboard(std::string text) {
        Request r;
        r.kind = Request::Kind::Clipboard;
        r.text = std::move(text);
        post(std::move(r));
    }

    void WindowLink::post(Request r) {
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            requests_.push_back(std::move(r));
        }
        // Any thread; wakes a pump that is waiting rather than polling.
        glfwPostEmptyEvent();
    }
} // namespace sextant
