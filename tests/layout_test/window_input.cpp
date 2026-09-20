// The window's input path: a WindowLink's queues and state mirror, and
// imgui_impl_sextant replaying them into ImGui. Input GLFW cannot be asked to
// invent, so the events are posted as its callbacks would; everything after
// that -- the key table, the modifier mask, the mirror, the requests going back
// -- is the real path a click takes.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_link.h"
#include "widgets/imgui_impl_sextant.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace lt {
    using namespace sextant;

    namespace {
        // A hidden window, its link, and an ImGui context with our backend on it.
        // Everything the window thread would own, on the test's own thread.
        struct Harness {
            GLContext ctx;
            ImGuiContext* imgui = nullptr;

            Harness()
                : ctx({.width = 800, .height = 600, .title = "window_input", .visible = false}) {
                imgui = ImGui::CreateContext();
                ImGui::SetCurrentContext(imgui);
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.Fonts->AddFontDefault();
                ImGui_ImplSextant_Init(ctx.link());
            }

            ~Harness() {
                ImGui::SetCurrentContext(imgui);
                ImGui_ImplSextant_Shutdown();
                ImGui::DestroyContext(imgui);
            }

            WindowLink& link() { return ctx.link(); }

            // Replay whatever is queued and open the frame. What ImGui clears at
            // the end of one -- the wheel, the characters typed -- can only be
            // read between these two.
            void begin() {
                ImGui::SetCurrentContext(imgui);
                ImGui_ImplSextant_NewFrame(ctx.link());
                ImGui::NewFrame();
            }

            // Close it, and let the pump side service what the frame asked for.
            void end() {
                ImGui::Render();
                ctx.link().service_requests();
            }

            void frame(const std::function<void()>& ui = {}) {
                begin();
                if (ui) ui();
                end();
            }
        };

        WindowEvent mouse_pos(double x, double y) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MousePos;
            e.x = x;
            e.y = y;
            return e;
        }

        WindowEvent button(int index, bool down, int mods = 0) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MouseButton;
            e.key = index;
            e.down = down;
            e.mods = mods;
            return e;
        }

        WindowEvent key(int glfw_key, bool down, int mods = 0) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::Key;
            e.key = glfw_key;
            e.down = down;
            e.mods = mods;
            return e;
        }

        WindowEvent simple(WindowEvent::Kind k) {
            WindowEvent e;
            e.kind = k;
            return e;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // The queue: every kind of input, replayed into ImGui by the backend
    // -------------------------------------------------------------------------
    void test_window_input_queue() {
        std::printf("\n[window input: the event queue]\n");

        Harness h;
        ImGuiIO& io = ImGui::GetIO();

        // The mirror stands in for the GLFW calls imgui_impl_glfw makes here.
        int ww = 0, wh = 0, fw = 0, fh = 0;
        h.link().window_size(ww, wh);
        h.link().framebuffer_size(fw, fh);
        h.frame();
        check(io.DisplaySize.x == static_cast<float>(ww) &&
              io.DisplaySize.y == static_cast<float>(wh),
              "input: the display size comes from the mirror, not from GLFW");
        check(io.DisplayFramebufferScale.x > 0.0f &&
              std::abs(io.DisplayFramebufferScale.x * ww - fw) < 1.0f,
              "input: and the framebuffer scale is what the two sizes disagree by");
        check(io.DeltaTime > 0.0f, "input: the frame's time step is positive");

        // Motion, then a right-button click with Ctrl and Shift held.
        h.link().post_event(mouse_pos(120.0, 45.0));
        h.frame();
        check(io.MousePos.x == 120.0f && io.MousePos.y == 45.0f,
              "input: a queued motion event moves ImGui's pointer");

        h.link().post_event(button(1, true, ModCtrl | ModShift));
        h.frame();
        check(io.MouseDown[1], "input: a queued button press reaches ImGui");
        // ImGui swaps Cmd and Ctrl in AddKeyEvent() under ConfigMacOSXBehaviors
        // (on by default there), which is what makes Cmd the shortcut key on
        // that platform -- so the physical Ctrl the pump read arrives as Super.
        // The backend feeds it the physical modifier either way, as
        // imgui_impl_glfw did; which one ImGui files it under is ImGui's call.
        const bool ctrl_arrived = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
        check(ctrl_arrived && io.KeyShift && !io.KeyAlt,
              "input: with the modifier mask the pump read off the keyboard");
        h.link().post_event(button(1, false, 0));
        h.frame();
        check(!io.MouseDown[1] && !io.KeyCtrl && !io.KeySuper && !io.KeyShift,
              "input: and the release, with the modifiers let go");

        // The scroll wheel.
        WindowEvent scroll = simple(WindowEvent::Kind::Scroll);
        scroll.x = 0.0;
        scroll.y = -2.0;
        h.link().post_event(scroll);
        h.begin();
        check(io.MouseWheel == -2.0f, "input: a scroll event keeps its sign and size");
        h.end();

        // The key table: a letter, a named key and a keypad key, by GLFW keycode.
        h.link().post_event(key(GLFW_KEY_A, true));
        h.link().post_event(key(GLFW_KEY_F5, true));
        h.link().post_event(key(GLFW_KEY_KP_7, true));
        h.frame();
        check(ImGui::IsKeyDown(ImGuiKey_A) && ImGui::IsKeyDown(ImGuiKey_F5) &&
              ImGui::IsKeyDown(ImGuiKey_Keypad7),
              "input: the copied key table maps letters, function keys and the keypad");
        check(!ImGui::IsKeyDown(ImGuiKey_B), "input: and nothing else");

        // Typing is its own event: a key ImGui knows nothing about still types.
        WindowEvent ch = simple(WindowEvent::Kind::Char);
        ch.codepoint = 0x00E9; // e-acute, a character no keycode carries
        h.link().post_event(ch);
        h.begin();
        check(io.InputQueueCharacters.Size == 1 && io.InputQueueCharacters[0] == 0x00E9,
              "input: a char event types what no key table could name");
        h.end();

        // Losing focus releases what was held, or the key sticks down forever.
        WindowEvent focus = simple(WindowEvent::Kind::Focus);
        focus.down = false;
        h.link().post_event(focus);
        h.frame();
        check(!ImGui::IsKeyDown(ImGuiKey_A),
              "input: a focus-lost event releases the keys that were down");

        // The pointer leaving is a position, not a flag; nothing puts it back
        // here, since a hidden window is never the focused one.
        check(!h.link().focused(), "input: the hidden window is not focused");
        h.link().post_event(mouse_pos(200.0, 200.0));
        h.frame();
        h.link().post_event(simple(WindowEvent::Kind::MouseLeave));
        h.frame();
        check(io.MousePos.x < -1e6f, "input: the pointer leaving puts it out of reach");
        h.link().post_event(simple(WindowEvent::Kind::MouseEnter));
        h.frame();
        check(io.MousePos.x == 200.0f && io.MousePos.y == 200.0f,
              "input: coming back restores where it was, not where the leave left it");
    }

    // -------------------------------------------------------------------------
    // The other direction: what the render thread asks the pump to do
    // -------------------------------------------------------------------------
    void test_window_input_requests() {
        std::printf("\n[window input: the request queue]\n");

        Harness h;

        // The cursor shape: set in the UI, posted by the next frame's NewFrame,
        // applied by the pump.
        h.frame([] { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); });
        h.frame();
        check(h.link().cursor() == WindowCursor::ResizeEW,
              "requests: a cursor the UI asked for reaches the window");

        h.frame();
        check(h.link().cursor() == WindowCursor::Arrow,
              "requests: and a frame that asks for nothing goes back to the arrow");

        ImGui::GetIO().MouseDrawCursor = true;
        h.frame();
        h.frame();
        check(h.link().cursor() == WindowCursor::Hidden,
              "requests: ImGui drawing its own cursor hides the window's");
        ImGui::GetIO().MouseDrawCursor = false;

        // A resize, which is the panel's Plot-size request: posted in window
        // coordinates, and the mirror reports it back once the pump has run.
        int ww = 0, wh = 0;
        h.link().window_size(ww, wh);
        h.link().post_resize(ww - 60, wh - 40);
        int after_w = ww, after_h = wh;
        h.link().window_size(after_w, after_h);
        check(after_w == ww && after_h == wh,
              "requests: posting a resize does not touch the window by itself");
        h.link().service_requests();
        h.link().sync_state();
        h.link().window_size(after_w, after_h);
        check(after_w == ww - 60 && after_h == wh - 40,
              "requests: the pump resizes it, and the mirror carries the new size");
        check(h.ctx.width() > 0 && h.ctx.height() > 0,
              "requests: GLContext's framebuffer size reads the same mirror");

        // The clipboard, put back as it was found.
        const char* before = glfwGetClipboardString(nullptr);
        const std::string held = before ? before : "";
        h.link().post_clipboard("sextant window_input");
        h.link().service_requests();
        const char* now = glfwGetClipboardString(nullptr);
        check(now != nullptr && std::string(now) == "sextant window_input",
              "requests: a clipboard write goes through the queue too");
        h.link().post_clipboard(held);
        h.link().service_requests();
    }
} // namespace lt
