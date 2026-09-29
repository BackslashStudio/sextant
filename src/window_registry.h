#pragma once
#include <atomic>
#include <functional>

namespace sextant {
// Process-wide record of open figure windows, behind one mutex and one
// condition variable: Figure::show() registers a window, the window thread's
// close callback unregisters it, and Figure::wait_closed()/run() block here.
//
// A figure's own `open` flag is what wait_window_closed() watches, not the
// count -- one figure closing must not wake it. Unregistering publishes that
// flag before it takes the lock, so a waiter that wakes reads the new value.

void register_open_window();

// Idempotence is the caller's: Figure pairs exactly one of these with each
// register_open_window(), whichever of the window thread or close() gets there
// first.
void unregister_open_window();

int open_window_count();

// Wait until `open` reads false, or `timeout_s` seconds pass; a negative or
// non-finite timeout waits forever. Returns the flag's final value inverted --
// true when closed, false when the wait timed out.
bool wait_window_closed(const std::atomic<bool>& open, double timeout_s);

// The same for "no window is open at all", which is true when none ever was.
bool wait_all_windows_closed(double timeout_s);

// Wakes every waiter below without changing anything they watch: how a queued
// event reaches a wait_closed()/run() that has callbacks to deliver.
void wake_waiters();

// Wait until `open` reads false, `wake()` returns true, or the timeout passes,
// whichever is first; the caller looks at what happened. `wake` runs under the
// registry's lock, so it should only read atomics.
void wait_window_closed_or(const std::atomic<bool>& open, double timeout_s,
                           const std::function<bool()>& wake);

// The same over "no window is open at all".
void wait_all_windows_closed_or(double timeout_s, const std::function<bool()>& wake);
} // namespace sextant
