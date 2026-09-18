// Windows/Linux half of platform.h: no context lock is needed, and the
// clipboard is read by the pumping thread instead.
#include "platform.h"

namespace wp {
    void lock_current_context() {}

    void unlock_current_context() {}

    bool read_clipboard_here(std::string&) { return false; }
} // namespace wp
