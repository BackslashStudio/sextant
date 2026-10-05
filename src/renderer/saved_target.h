#pragma once

namespace sextant {
    // The caller's framebuffer bindings (draw and read) and viewport, saved
    // before an offscreen target is bound and put back afterwards, so a host
    // that renders inside its own FBO keeps it (GUI-kit R5). Current context only.
    struct SavedTarget {
        int draw = 0;
        int read = 0;
        int viewport[4] = {0, 0, 0, 0};
        bool held = false;

        void save();
        // Restores what save() took, once; nothing if nothing is held.
        void restore();
    };
} // namespace sextant
