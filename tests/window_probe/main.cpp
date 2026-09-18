// sextant_window_probe: the throwaway prototype of v1.0 step 21.1, built before
// anything of the macOS threading design lands in src/. Raw GLFW + GL 4.1 +
// Dear ImGui; not linked against sextant. Replaced at step 21.4 by a test that
// drives the library.
//
// The design, run on every platform: the main thread creates, pumps and
// destroys two windows; each window is rendered and swapped on its own thread,
// under its context's CGL lock on macOS; input reaches that thread's ImGui only
// through a queue the pump fills, window state only through an atomic mirror;
// and what the render thread needs done (cursor shape, clipboard set, destroy)
// goes back to the pump as a request.
//
//   sextant_window_probe            scripted: open, render, resize, input,
//                                   clipboard, close out of order. Exit 0 when
//                                   every check passes, 1 when one fails, 2 on a
//                                   timeout (most likely a deadlock). Replaces
//                                   the clipboard's contents.
//   sextant_window_probe --manual   the same two windows left to a person:
//                                   drag-resize, click, type, copy/paste, and
//                                   watch the moving bar for flicker or tearing.
#include "imgui.h"   // GLAD first, via IMGUI_USER_CONFIG
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "platform.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

thread_local ImGuiContext* MyImGuiTLS = nullptr;   // see widgets/sextant_imconfig.h

// Compiled in from imgui_impl_glfw.cpp for its key table alone. Not declared in
// that backend's header in this ImGui version, hence here; step 21.3 copies it.
ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);

namespace {
    using Clock = std::chrono::steady_clock;

    // Pump -> render thread.
    struct Event {
        enum Kind { CursorPos, CursorLeave, MouseButton, Scroll, Key, Char, Focus } kind;
        double x = 0.0, y = 0.0;         // CursorPos, Scroll
        int a = 0, b = 0, c = 0, d = 0;  // MouseButton: button, action, mods
                                         // Key: key, scancode, action, mods
                                         // Char: codepoint; Focus: focused
    };

    // Render thread -> pump.
    struct Request {
        enum Kind { CursorShape, SetClipboard, Destroy } kind;
        int shape = 0;
        std::string text;
    };

    struct Link {
        const char* title = "";
        GLFWwindow* window = nullptr;   // the pump's; gone after the Destroy request

        // State mirror, written by the pump's callbacks.
        std::atomic<int> win_w{0}, win_h{0}, fb_w{0}, fb_h{0};

        std::mutex events_mutex;
        std::vector<Event> events;
        std::mutex requests_mutex;
        std::vector<Request> requests;

        std::mutex clipboard_mutex;
        std::string clipboard_mirror;   // what the pump last read (Windows/Linux)

        std::thread thread;
        std::atomic<bool> stop{false};  // close() from the API, any thread
        std::atomic<bool> destroyed{false};

        // What the render thread reports.
        std::atomic<long> frames{0};
        std::atomic<int> drawn_fb_w{0}, drawn_fb_h{0};
        std::atomic<float> button_x{0.0f}, button_y{0.0f}, input_x{0.0f}, input_y{0.0f};
        std::atomic<bool> input_active{false};
        std::atomic<int> clicks{0}, a_presses{0};
        std::atomic<int> clipboard_step{0};    // the script: 1 = set it, 2 = read it back
        std::atomic<int> clipboard_result{0};  // 1 read back what was set, -1 not
        std::atomic<double> worst_frame_ms{0.0};
        std::mutex report_mutex;
        std::string typed, renderer, clipboard_seen;
    };

    std::atomic<const char*> g_step{"start"};
    std::atomic<int> g_created{0}, g_destroyed{0}, g_cursor_changes{0}, g_clipboard_sets{0};
    std::string g_clipboard_text;   // written before the script asks for it
    std::mutex g_glad_mutex;
    bool g_glad_loaded = false;
    GLFWcursor* g_cursors[ImGuiMouseCursor_COUNT] = {};
    std::vector<Link*> g_links;
    int g_checks = 0, g_failures = 0;

    void check(bool ok, const std::string& what) {
        ++g_checks;
        if (!ok) ++g_failures;
        std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    }

    void post_event(Link& l, const Event& e) {
        std::lock_guard lock(l.events_mutex);
        l.events.push_back(e);
    }

    void post_request(Link& l, Request r) {
        {
            std::lock_guard lock(l.requests_mutex);
            l.requests.push_back(std::move(r));
        }
        glfwPostEmptyEvent();   // any thread; wakes a waiting pump
    }

    // -------------------------------------------------------------------------
    // Render thread
    // -------------------------------------------------------------------------

    void add_mods(ImGuiIO& io, int mods) {
        io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLFW_MOD_CONTROL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (mods & GLFW_MOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mods & GLFW_MOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mods & GLFW_MOD_SUPER) != 0);
    }

    void replay(ImGuiIO& io, const Event& e) {
        switch (e.kind) {
            case Event::CursorPos:
                io.AddMousePosEvent(static_cast<float>(e.x), static_cast<float>(e.y));
                break;
            case Event::CursorLeave:
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                break;
            case Event::MouseButton:
                add_mods(io, e.c);
                if (e.a >= 0 && e.a < ImGuiMouseButton_COUNT)
                    io.AddMouseButtonEvent(e.a, e.b == GLFW_PRESS);
                break;
            case Event::Scroll:
                io.AddMouseWheelEvent(static_cast<float>(e.x), static_cast<float>(e.y));
                break;
            case Event::Key:
                add_mods(io, e.d);
                if (e.c == GLFW_PRESS || e.c == GLFW_RELEASE)
                    io.AddKeyEvent(ImGui_ImplGlfw_KeyToImGuiKey(e.a, e.b), e.c == GLFW_PRESS);
                break;
            case Event::Char:
                io.AddInputCharacter(static_cast<unsigned int>(e.a));
                break;
            case Event::Focus:
                io.AddFocusEvent(e.a != 0);
                break;
        }
    }

    Link& clipboard_link() {
        return *static_cast<Link *>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
    }

    void build_ui(Link& l, int ww, int wh, double seconds) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(std::max(ww - 20, 120)), 200.0f));
        ImGui::Begin(l.title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("frame %ld, window %dx%d, framebuffer %dx%d", l.frames.load(), ww, wh,
                    l.fb_w.load(), l.fb_h.load());
        ImGui::Text("worst frame %.1f ms", l.worst_frame_ms.load());

        if (ImGui::Button("Click me")) ++l.clicks;
        ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        l.button_x = (lo.x + hi.x) * 0.5f;
        l.button_y = (lo.y + hi.y) * 0.5f;
        ImGui::SameLine();
        ImGui::Text("clicks %d", l.clicks.load());

        static thread_local char text[64] = "";
        ImGui::InputText("type here", text, sizeof text);
        lo = ImGui::GetItemRectMin();
        hi = ImGui::GetItemRectMax();
        l.input_x = (lo.x + hi.x) * 0.5f;
        l.input_y = (lo.y + hi.y) * 0.5f;
        l.input_active = ImGui::IsItemActive();

        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) ++l.a_presses;
        ImGui::Text("A pressed %d times", l.a_presses.load());

        const int step = l.clipboard_step.exchange(0);
        if (step == 1) ImGui::SetClipboardText(g_clipboard_text.c_str());
        std::lock_guard lock(l.report_mutex);
        if (step == 2) {
            const char* got = ImGui::GetClipboardText();
            l.clipboard_seen = got ? got : "(null)";
            l.clipboard_result = (got && g_clipboard_text == got) ? 1 : -1;
        }
        l.typed = text;
        ImGui::Text("clipboard read: %s", l.clipboard_seen.c_str());
        ImGui::End();

        // A bar crossing the window: flicker or tearing shows here (--manual).
        const float w = static_cast<float>(std::max(ww, 1)), h = static_cast<float>(wh);
        const float x = std::fmod(static_cast<float>(seconds) * 300.0f, w);
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, h - 50.0f), ImVec2(x + 40.0f, h - 10.0f),
                                                      IM_COL32(255, 200, 0, 255));
    }

    void render_thread(Link& l, float r, float g, float b) {
        glfwMakeContextCurrent(l.window);
        glfwSwapInterval(1);
        {
            std::lock_guard lock(g_glad_mutex);
            if (!g_glad_loaded)
                g_glad_loaded = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
        }
        {
            std::lock_guard lock(l.report_mutex);
            const auto* s = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
            l.renderer = s ? s : "(none)";
        }

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendPlatformName = "window_probe queue";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_ClipboardUserData = &l;
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            post_request(clipboard_link(), {Request::SetClipboard, 0, text ? text : ""});
        };
        // macOS reads NSPasteboard right here on the render thread (the design's
        // plan); elsewhere the pump's copy.
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            thread_local std::string held;
            if (!wp::read_clipboard_here(held)) {
                Link& cl = clipboard_link();
                std::lock_guard lock(cl.clipboard_mutex);
                held = cl.clipboard_mirror;
            }
            return held.c_str();
        };
        ImGui_ImplOpenGL3_Init("#version 410");

        ImGuiMouseCursor last_cursor = ImGuiMouseCursor_COUNT;
        const auto start = Clock::now();
        auto prev = start;
        while (!l.stop.load() && !glfwWindowShouldClose(l.window)) {
            const auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - prev).count();
            prev = now;
            if (l.frames > 10) l.worst_frame_ms = std::max(l.worst_frame_ms.load(), dt * 1000.0);

            const int ww = l.win_w, wh = l.win_h, fw = l.fb_w, fh = l.fb_h;
            io.DisplaySize = ImVec2(static_cast<float>(ww), static_cast<float>(wh));
            if (ww > 0 && wh > 0)
                io.DisplayFramebufferScale = ImVec2(static_cast<float>(fw) / static_cast<float>(ww),
                                                    static_cast<float>(fh) / static_cast<float>(wh));
            io.DeltaTime = dt > 0.0 ? static_cast<float>(dt) : 1e-4f;

            std::vector<Event> events;
            {
                std::lock_guard lock(l.events_mutex);
                events.swap(l.events);
            }
            for (const Event& e: events) replay(io, e);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            build_ui(l, ww, wh, std::chrono::duration<double>(now - start).count());
            ImGui::Render();

            const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
            if (cursor != last_cursor) {
                last_cursor = cursor;
                post_request(l, {Request::CursorShape, cursor, {}});
            }

            wp::lock_current_context();
            glViewport(0, 0, fw, fh);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(l.window);
            wp::unlock_current_context();

            l.drawn_fb_w = fw;
            l.drawn_fb_h = fh;
            ++l.frames;
        }

        // Teardown as step 21.4 plans it: release the context, then ask the pump
        // to destroy the window without waiting for it.
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(ctx);
        glfwMakeContextCurrent(nullptr);
        post_request(l, {Request::Destroy, 0, {}});
    }

    // -------------------------------------------------------------------------
    // Main thread: callbacks, requests, the pump
    // -------------------------------------------------------------------------

    Link& link_of(GLFWwindow* w) { return *static_cast<Link *>(glfwGetWindowUserPointer(w)); }

    void install_callbacks(GLFWwindow* w) {
        glfwSetWindowSizeCallback(w, [](GLFWwindow* win, int x, int y) {
            link_of(win).win_w = x;
            link_of(win).win_h = y;
        });
        glfwSetFramebufferSizeCallback(w, [](GLFWwindow* win, int x, int y) {
            link_of(win).fb_w = x;
            link_of(win).fb_h = y;
        });
        glfwSetWindowFocusCallback(w, [](GLFWwindow* win, int focused) {
            post_event(link_of(win), {Event::Focus, 0.0, 0.0, focused});
        });
        glfwSetCursorPosCallback(w, [](GLFWwindow* win, double x, double y) {
            post_event(link_of(win), {Event::CursorPos, x, y});
        });
        glfwSetCursorEnterCallback(w, [](GLFWwindow* win, int entered) {
            if (!entered) post_event(link_of(win), {Event::CursorLeave});
        });
        glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int button, int action, int mods) {
            post_event(link_of(win), {Event::MouseButton, 0.0, 0.0, button, action, mods});
        });
        glfwSetScrollCallback(w, [](GLFWwindow* win, double x, double y) {
            post_event(link_of(win), {Event::Scroll, x, y});
        });
        glfwSetKeyCallback(w, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            post_event(link_of(win), {Event::Key, 0.0, 0.0, key, scancode, action, mods});
        });
        glfwSetCharCallback(w, [](GLFWwindow* win, unsigned int c) {
            post_event(link_of(win), {Event::Char, 0.0, 0.0, static_cast<int>(c)});
        });
    }

    GLFWcursor* cursor_for(int shape) {
        if (shape < 0 || shape >= ImGuiMouseCursor_COUNT) return nullptr;
        if (!g_cursors[shape]) {
            int glfw_shape = GLFW_ARROW_CURSOR;
            switch (shape) {
                case ImGuiMouseCursor_TextInput: glfw_shape = GLFW_IBEAM_CURSOR; break;
                case ImGuiMouseCursor_ResizeAll: glfw_shape = GLFW_RESIZE_ALL_CURSOR; break;
                case ImGuiMouseCursor_ResizeNS: glfw_shape = GLFW_RESIZE_NS_CURSOR; break;
                case ImGuiMouseCursor_ResizeEW: glfw_shape = GLFW_RESIZE_EW_CURSOR; break;
                case ImGuiMouseCursor_ResizeNESW: glfw_shape = GLFW_RESIZE_NESW_CURSOR; break;
                case ImGuiMouseCursor_ResizeNWSE: glfw_shape = GLFW_RESIZE_NWSE_CURSOR; break;
                case ImGuiMouseCursor_Hand: glfw_shape = GLFW_POINTING_HAND_CURSOR; break;
                case ImGuiMouseCursor_NotAllowed: glfw_shape = GLFW_NOT_ALLOWED_CURSOR; break;
                default: break;
            }
            g_cursors[shape] = glfwCreateStandardCursor(glfw_shape);
        }
        return g_cursors[shape];
    }

    void service(Link& l) {
        std::vector<Request> requests;
        {
            std::lock_guard lock(l.requests_mutex);
            requests.swap(l.requests);
        }
        for (const Request& r: requests) {
            if (!l.window) break;
            switch (r.kind) {
                case Request::CursorShape:
                    glfwSetCursor(l.window, cursor_for(r.shape));
                    if (r.shape > ImGuiMouseCursor_Arrow) ++g_cursor_changes;
                    break;
                case Request::SetClipboard: {
                    glfwSetClipboardString(l.window, r.text.c_str());
                    const char* s = glfwGetClipboardString(l.window);
                    std::lock_guard lock(l.clipboard_mutex);
                    l.clipboard_mirror = s ? s : "";
                    ++g_clipboard_sets;
                    break;
                }
                case Request::Destroy:
                    glfwDestroyWindow(l.window);
                    l.window = nullptr;
                    l.destroyed = true;
                    ++g_destroyed;
                    break;
            }
        }
    }

    void pump_once(double wait_s) {
        glfwWaitEventsTimeout(wait_s);
        for (Link* l: g_links) service(*l);
    }

    bool pump_until(const std::function<bool()>& done, double timeout_s) {
        const auto deadline = Clock::now() + std::chrono::duration<double>(timeout_s);
        while (!done()) {
            if (Clock::now() > deadline) return false;
            pump_once(0.005);
        }
        return true;
    }

    bool open(Link& l, const char* title, int x, int y, float r, float g, float b) {
        l.title = title;
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        l.window = glfwCreateWindow(420, 300, title, nullptr, nullptr);
        if (!l.window) return false;
        ++g_created;
        glfwSetWindowUserPointer(l.window, &l);
        install_callbacks(l.window);
        int w = 0, h = 0;
        glfwGetWindowSize(l.window, &w, &h);
        l.win_w = w;
        l.win_h = h;
        glfwGetFramebufferSize(l.window, &w, &h);
        l.fb_w = w;
        l.fb_h = h;
        glfwSetWindowPos(l.window, x, y);
        glfwShowWindow(l.window);
        l.thread = std::thread(render_thread, std::ref(l), r, g, b);
        return true;
    }

    void join(Link& l) {
        if (l.thread.joinable()) l.thread.join();
    }

    // After a failed open: stop whatever did start, and let the pump destroy it.
    void abandon(Link& a, Link& b) {
        a.stop = true;
        b.stop = true;
        pump_until([&] {
            return (!a.thread.joinable() || a.destroyed) && (!b.thread.joinable() || b.destroyed);
        }, 10.0);
        join(a);
        join(b);
    }

    void step(const char* name) {
        g_step = name;
        std::printf("-- %s --\n", name);
    }

    // Kills the process if the script stalls: a deadlock cannot be joined.
    void start_watchdog(double seconds) {
        std::thread([seconds] {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            std::printf("\nTIMEOUT after %.0f s, in step: %s\n", seconds, g_step.load());
            std::fflush(stdout);
            std::_Exit(2);
        }).detach();
    }

    void report_frames(Link& l) {
        std::lock_guard lock(l.report_mutex);
        std::printf("  %s: %ld frames, worst %.1f ms, renderer %s\n", l.title, l.frames.load(),
                    l.worst_frame_ms.load(), l.renderer.c_str());
    }

    // -------------------------------------------------------------------------
    // The scripted run
    // -------------------------------------------------------------------------

    void click(Link& l, float x, float y) {
        post_event(l, {Event::CursorPos, x, y});
        post_event(l, {Event::MouseButton, 0.0, 0.0, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0});
        post_event(l, {Event::MouseButton, 0.0, 0.0, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0});
    }

    int run_script() {
        Link a, b;
        g_links = {&a, &b};

        step("open two windows on the main thread, render each on its own");
        const bool opened = open(a, "window_probe A", 60, 60, 0.15f, 0.20f, 0.30f) &&
                            open(b, "window_probe B", 540, 60, 0.30f, 0.20f, 0.15f);
        check(opened, "both windows created on the main thread");
        if (!opened) {
            abandon(a, b);
            return 1;
        }
        check(pump_until([&] { return a.frames >= 30 && b.frames >= 30; }, 20.0),
              "each window renders and swaps on its own thread (30 frames each)");
        report_frames(a);
        check(a.drawn_fb_w == a.fb_w && a.drawn_fb_h == a.fb_h && a.fb_w > 0,
              "the render thread draws at the framebuffer size the pump's callback mirrored");

        step("resize from the main thread while both render");
        bool resized = true;
        for (int i = 1; i <= 12 && resized; ++i) {
            const long before = a.frames;
            glfwSetWindowSize(a.window, 420 + 15 * i, 300 + 10 * i);
            if (i % 3 == 0) glfwSetWindowSize(b.window, 420 - 10 * i, 300 + 5 * i);
            resized = pump_until([&] { return a.frames >= before + 2; }, 10.0);
        }
        check(resized, "12 resizes, each followed by frames: the pump's context update and the "
                       "render thread's lock do not deadlock");
        const bool caught_up = pump_until([&] {
            return a.win_w == 600 && a.win_h == 420 && a.drawn_fb_w == a.fb_w && a.drawn_fb_h == a.fb_h;
        }, 10.0);
        check(caught_up, "the final 600x420 reaches the render thread through the mirror (window " +
                         std::to_string(a.win_w.load()) + "x" + std::to_string(a.win_h.load()) +
                         ", drawn " + std::to_string(a.drawn_fb_w.load()) + "x" +
                         std::to_string(a.drawn_fb_h.load()) + ")");

        step("input, posted to the queue as the pump would");
        pump_until([&] { return a.button_x > 0.0f && a.input_x > 0.0f; }, 5.0);
        post_event(a, {Event::Focus, 0.0, 0.0, 1});
        click(a, a.button_x, a.button_y);
        check(pump_until([&] { return a.clicks >= 1; }, 5.0), "a click presses A's ImGui button");
        click(a, a.input_x, a.input_y);
        check(pump_until([&] { return a.input_active.load(); }, 5.0), "a click activates A's text field");
        for (const char c: std::string("probe")) post_event(a, {Event::Char, 0.0, 0.0, c});
        check(pump_until([&] {
            std::lock_guard lock(a.report_mutex);
            return a.typed == "probe";
        }, 5.0), "characters reach the text field");
        check(pump_until([&] { return g_cursor_changes > 0; }, 5.0),
              "the text field's cursor shape travels back to the pump as a request");
        post_event(b, {Event::Focus, 0.0, 0.0, 1});
        post_event(b, {Event::Key, 0.0, 0.0, GLFW_KEY_A, 0, GLFW_PRESS, 0});
        post_event(b, {Event::Key, 0.0, 0.0, GLFW_KEY_A, 0, GLFW_RELEASE, 0});
        check(pump_until([&] { return b.a_presses >= 1; }, 5.0),
              "a key reaches B's ImGui through GLFW's key table");

        step("clipboard");
        g_clipboard_text = "sextant window_probe " +
                           std::to_string(Clock::now().time_since_epoch().count() % 1000000);
        const int sets = g_clipboard_sets;
        a.clipboard_step = 1;
        check(pump_until([&] { return g_clipboard_sets > sets; }, 5.0),
              "SetClipboardText on the render thread is carried out by the pump");
        a.clipboard_step = 2;
        pump_until([&] { return a.clipboard_result != 0; }, 5.0);
        {
            std::lock_guard lock(a.report_mutex);
#if defined(__APPLE__)
            check(a.clipboard_result == 1, "read back from NSPasteboard on the render thread (\"" +
                                           a.clipboard_seen + "\")");
#else
            check(a.clipboard_result == 1, "read back through the pump's copy (\"" + a.clipboard_seen + "\")");
#endif
        }

#if defined(__APPLE__)
        pump_until([] { return false; }, 0.5);
        const int shot = std::system("screencapture -x window_probe.png");
        std::printf("  screenshot: %s\n", shot == 0 ? "window_probe.png" : "screencapture failed");
#endif

        step("close out of order: A (opened first) by its close button");
        glfwSetWindowShouldClose(a.window, GLFW_TRUE);
        check(pump_until([&] { return a.destroyed.load(); }, 10.0),
              "A's render thread releases its context and the pump destroys the window");
        join(a);
        const long b_before = b.frames;
        check(pump_until([&] { return b.frames > b_before + 5; }, 5.0), "B keeps rendering after A is gone");

        step("then B, closed through the API from a worker thread");
        std::thread([&b] { b.stop = true; }).join();
        check(pump_until([&] { return b.destroyed.load(); }, 10.0), "the pump destroys B at its next turn");
        join(b);
        check(g_created == 2 && g_destroyed == 2, "no window leaked (" + std::to_string(g_created.load()) +
                                                  " created, " + std::to_string(g_destroyed.load()) +
                                                  " destroyed)");
        report_frames(a);
        report_frames(b);
        g_links.clear();
        return g_failures == 0 ? 0 : 1;
    }

    int run_manual() {
        Link a, b;
        g_links = {&a, &b};
        if (!open(a, "window_probe A", 60, 60, 0.15f, 0.20f, 0.30f) ||
            !open(b, "window_probe B", 540, 60, 0.30f, 0.20f, 0.15f))
        {
            abandon(a, b);
            return 1;
        }
        std::printf("Drag-resize both windows, click, type, copy and paste (Ctrl/Cmd+C/V), and\n"
                    "watch the yellow bar for flicker or tearing. Close both windows to finish.\n");
        while (!(a.destroyed && b.destroyed)) {
            glfwWaitEvents();
            for (Link* l: g_links) service(*l);
        }
        join(a);
        join(b);
        report_frames(a);
        report_frames(b);
        g_links.clear();
        return 0;
    }
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool manual = argc > 1 && std::strcmp(argv[1], "--manual") == 0;

    glfwSetErrorCallback([](int code, const char* text) {
        std::printf("  GLFW error 0x%x: %s\n", code, text);
    });
#if defined(__linux__)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);   // as the library does
#endif
    if (!glfwInit()) {
        std::printf("glfwInit failed\n");
        return 1;
    }
    std::printf("=== sextant_window_probe (%s) ===\nGLFW %s\n\n", manual ? "manual" : "scripted",
                glfwGetVersionString());

    int rc = 0;
    if (manual) {
        rc = run_manual();
    } else {
        start_watchdog(120.0);
        rc = run_script();
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    }
    for (GLFWcursor*& c: g_cursors)
        if (c) glfwDestroyCursor(c), c = nullptr;
    glfwTerminate();
    return rc;
}
