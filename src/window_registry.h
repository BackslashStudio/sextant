#pragma once
#include <atomic>

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
} // namespace sextant
