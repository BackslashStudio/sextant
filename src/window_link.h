#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWcursor;

namespace sextant {
    // One window's link between the thread that pumps its events and the thread
    // that renders it. Everything GLFW calls on the window belongs to the pumping
    // thread (on macOS that has to be the main one); the render thread reads a
    // mirror of the window's state, replays a queue of input events into ImGui,
    // and posts back what it needs done as a request.
    //
    // On Windows and Linux both threads are the window thread, one after the
    // other in the same loop — so the queues cost a drain per frame and the code
    // path is the one macOS needs.

    // Modifier state resolved on the pumping thread and carried with the event
    // that saw it: X11 leaves the modifier being pressed out of the callback's
    // own mods (imgui #6034), so the keys are read directly instead.
    enum WindowMod : int {
        ModCtrl = 1 << 0,
        ModShift = 1 << 1,
        ModAlt = 1 << 2,
        ModSuper = 1 << 3
    };

    struct WindowEvent {
        enum class Kind {
            MousePos, MouseEnter, MouseLeave, MouseButton, Scroll, Key, Char, Focus
        };

        Kind kind = Kind::MousePos;
        double x = 0.0, y = 0.0;    // MousePos: window coordinates; Scroll: offsets
        int key = 0;                // Key: GLFW keycode, already untranslated;
                                    // MouseButton: the button index
        int scancode = 0;
        int mods = 0;               // WindowMod mask, on Key and MouseButton
        unsigned int codepoint = 0; // Char
        bool down = false;          // key/button pressed, or Focus: focused
    };

    // The cursor shapes ImGui asks for, named here so window_link.cpp needs no
    // ImGui and imgui_impl_sextant.cpp no GLFW cursor constants.
    enum class WindowCursor {
        Hidden, Arrow, TextInput, ResizeAll, ResizeNS, ResizeEW,
        ResizeNESW, ResizeNWSE, Hand, NotAllowed, Count
    };

    class WindowLink {
    public:
        WindowLink() = default;

        ~WindowLink();

        WindowLink(const WindowLink&) = delete;

        WindowLink& operator=(const WindowLink&) = delete;

        // --- the pumping thread ------------------------------------------------

        // Installs this link's GLFW callbacks on `w` and seeds the mirror. The
        // window's user pointer becomes this link.
        void attach(GLFWwindow* w);

        // Re-reads the whole mirror. Called after every poll, so the render
        // thread's view of size, scale, focus, hover and cursor is one poll old at
        // worst and always internally consistent — no size callback needed.
        void sync_state();

        // Runs what the render thread posted: cursor shape, window resize,
        // clipboard writes.
        void service_requests();

        // Queues an event as one of the callbacks would. The pumping thread's, and
        // what a test uses in place of input GLFW cannot be asked to invent.
        void post_event(const WindowEvent& e);

        // The shape service_requests() last applied. Pumping thread only.
        WindowCursor cursor() const { return cursor_; }

        // --- the render thread -------------------------------------------------

        void take_events(std::vector<WindowEvent>& out);

        void post_cursor(WindowCursor c);

        void post_resize(int width, int height); // screen coordinates

        void post_clipboard(std::string text);

        // --- the mirror --------------------------------------------------------

        void window_size(int& w, int& h) const { win_.load(w, h); }

        void framebuffer_size(int& w, int& h) const { fb_.load(w, h); }

        int framebuffer_width() const { return fb_.first(); }

        int framebuffer_height() const { return fb_.second(); }

        float content_scale() const { return scale_.load(std::memory_order_relaxed); }

        bool focused() const { return focused_.load(std::memory_order_relaxed); }

        bool hovered() const { return hovered_.load(std::memory_order_relaxed); }

        void cursor_pos(float& x, float& y) const {
            x = cursor_x_.load(std::memory_order_relaxed);
            y = cursor_y_.load(std::memory_order_relaxed);
        }

    private:
        // Two ints in one word, so a size read never pairs a width from one
        // resize with a height from the next.
        class AtomicIntPair {
        public:
            void store(int a, int b) {
                v_.store((static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32)
                         | static_cast<std::uint32_t>(b), std::memory_order_release);
            }

            void load(int& a, int& b) const {
                const std::uint64_t v = v_.load(std::memory_order_acquire);
                a = static_cast<int>(static_cast<std::uint32_t>(v >> 32));
                b = static_cast<int>(static_cast<std::uint32_t>(v));
            }

            int first() const {
                int a = 0, b = 0;
                load(a, b);
                return a;
            }

            int second() const {
                int a = 0, b = 0;
                load(a, b);
                return b;
            }

        private:
            std::atomic<std::uint64_t> v_{0};
        };

        struct Request {
            enum class Kind { Cursor, Resize, Clipboard };

            Kind kind = Kind::Cursor;
            int a = 0, b = 0;
            std::string text;
        };

        void post(Request r);

        GLFWwindow* window_ = nullptr; // the pumping thread's, never dereferenced elsewhere

        AtomicIntPair win_, fb_;
        std::atomic<float> scale_{1.0f};
        std::atomic<bool> focused_{true}, hovered_{false};
        std::atomic<float> cursor_x_{0.0f}, cursor_y_{0.0f};

        std::mutex events_mutex_;
        std::vector<WindowEvent> events_;
        std::mutex requests_mutex_;
        std::vector<Request> requests_;

        // Standard cursors, made on the pumping thread on first use (GLFW puts
        // cursor creation on the main thread too).
        GLFWcursor* cursors_[static_cast<int>(WindowCursor::Count)] = {};
        WindowCursor cursor_ = WindowCursor::Arrow;
    };
} // namespace sextant
