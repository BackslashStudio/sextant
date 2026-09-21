#include "window_broker.h"
#include "window_link.h"
#include "platform/platform.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sextant {
    namespace {
        // Serialises window creation and destruction, which touch unlocked
        // process-wide state (GLFW's window list) and the window hints, which are
        // global. Never held during a frame.
        std::mutex& global_gl_mutex() {
            static std::mutex m;
            return m;
        }

        // Set once GLFW is up, so a request posted before that does not try to
        // wake a pump that cannot exist yet.
        std::atomic<bool> glfw_ready{false};

        void ensure_glfw_init() {
            static std::once_flag s_init;
            std::call_once(s_init, [] {
#if defined(__linux__)
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#if defined(__APPLE__)
                // An app bundle's working directory belongs to the program that
                // started, not to us: savefig("plot.png") has to land where the
                // caller thinks it will.
                glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#endif
                if (!glfwInit())
                    throw std::runtime_error("glfwInit failed");
                glfw_ready.store(true);
            });
        }

        struct Live {
            GLFWwindow* window = nullptr;
            std::shared_ptr<WindowLink> link;
        };

        // One piece of work for whoever owns windows, and the answer to it.
        struct Command {
            enum class Kind { Create, Destroy };

            Kind kind = Kind::Create;
            const WindowSpec* spec = nullptr; // Create: borrowed; the asker waits
            BrokeredWindow subject;           // Create: the answer; Destroy: what to unmake
            std::exception_ptr error;
            bool done = false;
        };

        struct Broker {
            std::mutex m;
            std::condition_variable cv;
            std::vector<Live> live;
            std::vector<std::shared_ptr<Command> > queue;
        };

        // Leaked on purpose, like the open-window registry: a Figure held in a
        // static outlives ordinary static destruction and closes through here.
        Broker& brk() {
            static Broker* b = new Broker();
            return *b;
        }

        void warn_no_pump() {
            static std::once_flag once;
            std::call_once(once, [] {
                std::fprintf(stderr,
                             "sextant: still waiting for a window. On this platform windows are "
                             "made and pumped on the main thread -- one shown from another thread "
                             "appears once the main thread calls Figure::run(), "
                             "Figure::poll_events() or Figure::wait_closed().\n");
            });
        }

        // --- the owning thread -------------------------------------------------

        BrokeredWindow make_window_here(const WindowSpec& spec) {
            ensure_glfw_init();

            BrokeredWindow bw;
            bw.link = std::make_shared<WindowLink>();
            {
                std::lock_guard<std::mutex> lock(global_gl_mutex());

                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                // macOS offers forward-compatible core contexts and nothing
                // else. GLFW 3.4's NSGL backend makes one whether or not this
                // is asked for, but 3.3 -- which find_package(glfw3 3.3) still
                // accepts under SEXTANT_FETCH_GLFW=OFF -- refuses without it.
                // Harmless everywhere: the library uses no removed entry point.
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                glfwWindowHint(GLFW_VISIBLE, spec.visible ? GLFW_TRUE : GLFW_FALSE);
                glfwWindowHint(GLFW_RESIZABLE, spec.resizable ? GLFW_TRUE : GLFW_FALSE);
                glfwWindowHint(GLFW_STENCIL_BITS, 8); // required by NanoVG
                // Always set: hints are sticky, so a live window's setting would
                // otherwise scale the next headless export.
                glfwWindowHint(GLFW_SCALE_TO_MONITOR,
                               spec.scale_to_monitor ? GLFW_TRUE : GLFW_FALSE);

                bw.window = glfwCreateWindow(spec.width, spec.height,
                                             spec.title.c_str(), nullptr, nullptr);
            }
            if (!bw.window)
                throw std::runtime_error("glfwCreateWindow failed");

            // Callbacks, user pointer and the first read of the mirror are GLFW
            // calls on the window, so they are this thread's too.
            bw.link->attach(bw.window);
            {
                std::lock_guard<std::mutex> lock(brk().m);
                brk().live.push_back({bw.window, bw.link});
            }
            return bw;
        }

        void destroy_window_here(GLFWwindow* w, std::shared_ptr<WindowLink> link) {
            std::lock_guard<std::mutex> lock(global_gl_mutex());
            glfwDestroyWindow(w);
            // `link` dies with this scope, and its standard cursors with it --
            // GLFW wants those freed on this thread as well.
        }

        // Takes a window off the live list. Null when it is not there, which is
        // what makes handing the same window back twice harmless.
        std::shared_ptr<WindowLink> take_live(GLFWwindow* w) {
            std::lock_guard<std::mutex> lock(brk().m);
            const auto it = std::find_if(brk().live.begin(), brk().live.end(),
                                         [w](const Live& l) { return l.window == w; });
            if (it == brk().live.end()) return nullptr;
            std::shared_ptr<WindowLink> link = std::move(it->link);
            brk().live.erase(it);
            return link;
        }

        void serve_queue() {
            std::vector<std::shared_ptr<Command> > todo;
            {
                std::lock_guard<std::mutex> lock(brk().m);
                todo.swap(brk().queue);
            }
            if (todo.empty()) return;

            for (const auto& c: todo) {
                try {
                    if (c->kind == Command::Kind::Create)
                        c->subject = make_window_here(*c->spec);
                    else
                        destroy_window_here(c->subject.window, std::move(c->subject.link));
                } catch (...) {
                    c->error = std::current_exception(); // handed to whoever asked
                }
            }
            {
                std::lock_guard<std::mutex> lock(brk().m);
                for (const auto& c: todo) c->done = true;
            }
            brk().cv.notify_all();
        }

        void post(const std::shared_ptr<Command>& c) {
            {
                std::lock_guard<std::mutex> lock(brk().m);
                brk().queue.push_back(c);
            }
            if (glfw_ready.load()) glfwPostEmptyEvent(); // wake a pump that is waiting
        }
    } // namespace

    BrokeredWindow create_window(const WindowSpec& spec) {
        if (platform::this_thread_owns_windows())
            return make_window_here(spec);

        auto c = std::make_shared<Command>();
        c->kind = Command::Kind::Create;
        c->spec = &spec; // borrowed: this thread waits for the answer
        post(c);

        {
            std::unique_lock<std::mutex> lock(brk().m);
            const auto ready = [&c] { return c->done; };
            // Not an error, just slow: the main thread may not have reached its
            // pump yet. Say so once, then keep waiting.
            if (!brk().cv.wait_for(lock, std::chrono::seconds(2), ready)) {
                warn_no_pump();
                brk().cv.wait(lock, ready);
            }
        }

        if (c->error) std::rethrow_exception(c->error);
        return std::move(c->subject);
    }

    void destroy_window(GLFWwindow* w) {
        if (!w) return;
        std::shared_ptr<WindowLink> link = take_live(w);
        if (!link) return; // handed back already

        if (platform::this_thread_owns_windows()) {
            destroy_window_here(w, std::move(link));
            return;
        }
        auto c = std::make_shared<Command>();
        c->kind = Command::Kind::Destroy;
        c->subject.window = w;
        c->subject.link = std::move(link);
        post(c); // and no waiting: the asker is on its way out
    }

    void pump_windows(double timeout_s) {
        if constexpr (!platform::windows_on_main_thread) {
            (void) timeout_s; // every window is pumped by its own thread, in GLContext
            return;
        }

        if (!platform::this_thread_owns_windows())
            throw std::logic_error(
                "sextant: window events are pumped on the main thread on this platform "
                "(Figure::poll_events(), run() and wait_closed() have to be called there)");

        ensure_glfw_init();
        serve_queue(); // a window that was asked for is a window to pump

        if (timeout_s > 0.0) glfwWaitEventsTimeout(timeout_s);
        else glfwPollEvents();

        std::vector<Live> live;
        {
            std::lock_guard<std::mutex> lock(brk().m);
            live = brk().live;
        }
        for (const Live& l: live) {
            l.link->sync_state();
            l.link->service_requests();
        }

        serve_queue(); // including the destroy those requests were the last of
    }

    bool pump_runs_here() {
        return platform::windows_on_main_thread && platform::this_thread_owns_windows();
    }

    bool pump_until(const std::function<bool()>& done, double timeout_s) {
        const bool finite = timeout_s >= 0.0; // negative or NaN: forever
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>(finite ? timeout_s : 0.0));
        while (!done()) {
            double slice = 0.1; // short enough that a close is noticed promptly
            if (finite) {
                const std::chrono::duration<double> left =
                        deadline - std::chrono::steady_clock::now();
                if (left.count() <= 0.0) return done();
                slice = std::min(slice, left.count());
            }
            pump_windows(slice);
        }
        return true;
    }

    void serve_broker_requests() {
        if (platform::this_thread_owns_windows()) serve_queue();
    }

    int live_window_count() {
        std::lock_guard<std::mutex> lock(brk().m);
        return static_cast<int>(brk().live.size());
    }
} // namespace sextant
