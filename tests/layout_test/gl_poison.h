// Fills every GL allocation made without data with seeded random bytes, so a
// read of memory sextant never wrote shows up as output that changes with the
// seed. Many drivers hand back zero-filled memory, which hides such a read; the
// macOS runner's software renderer may not (v1.0 step 21.1).
#pragma once

namespace lt {
    // Wraps GLAD's glTexImage2D, glRenderbufferStorage and glBufferData. GLAD
    // must already be loaded (any GLContext has been created); every later
    // allocation, in sextant, NanoVG and ImGui alike, goes through the wrappers.
    void install_gl_poison(unsigned seed);

    // Allocations poisoned so far, by kind: zero means the wrappers never ran.
    struct PoisonCounts {
        int textures = 0, renderbuffers = 0, buffers = 0;
    };
    PoisonCounts gl_poison_counts();
} // namespace lt
