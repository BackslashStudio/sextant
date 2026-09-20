#include "window_registry.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace sextant {
namespace {
    struct Registry {
        std::mutex              m;
        std::condition_variable cv;
        int                     count = 0;
    };

    // Deliberately never destroyed: a Figure held in a static outlives ordinary
    // static destruction, and its close path takes this lock.
    Registry& reg() {
        static Registry* r = new Registry();
        return *r;
    }

    // Longest finite wait, ~31 years: keeps the deadline arithmetic in range
    // for any timeout a caller can name.
    constexpr double kMaxTimeout = 1e9;

    template <class Pred>
    bool wait_until(Pred pred, double timeout_s) {
        std::unique_lock<std::mutex> lock(reg().m);
        if (!(timeout_s >= 0.0)) {           // negative or NaN
            reg().cv.wait(lock, pred);
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(std::min(timeout_s, kMaxTimeout)));
        return reg().cv.wait_until(lock, deadline, pred);
    }
} // namespace

void register_open_window() {
    std::lock_guard<std::mutex> lock(reg().m);
    ++reg().count;
}

void unregister_open_window() {
    {
        std::lock_guard<std::mutex> lock(reg().m);
        if (reg().count > 0) --reg().count;
    }
    reg().cv.notify_all();   // both kinds of waiter re-check their own predicate
}

int open_window_count() {
    std::lock_guard<std::mutex> lock(reg().m);
    return reg().count;
}

bool wait_window_closed(const std::atomic<bool>& open, double timeout_s) {
    return wait_until([&open] { return !open.load(); }, timeout_s);
}

bool wait_all_windows_closed(double timeout_s) {
    return wait_until([] { return reg().count == 0; }, timeout_s);
}
} // namespace sextant
