#pragma once
#include <string>

namespace sextant {
// The name Event::key carries for a GLFW key code (as WindowEvent::key holds it)
// pressed with `mods` (WindowMod bits): "a", "A", "ctrl+a", "escape", "f5",
// "shift+left". Empty for a key with no name (GLFW_KEY_UNKNOWN, and the
// platform-specific ones), which is not reported. A modifier key's own event is
// the bare name ("ctrl").
std::string key_event_name(int glfw_key, int mods);
} // namespace sextant
