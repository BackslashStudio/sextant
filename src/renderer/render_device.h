#pragma once
#include "nvg_renderer.h"
#include <memory>

struct NVGcontext;

namespace sextant {
    // GLAD's loader signature (glad.h's GLADloadproc), without including glad.h.
    using GLLoadProc = void* (*)(const char* name);

    // Loads GLAD's process-wide function table, once. A no-op when the table is
    // already filled, whoever filled it: reloading would rewrite it under other
    // rendering threads, and over a host's own load. A host that owns its GL
    // context calls this (instead of gladLoadGLLoader()) with its context
    // current; the library's GLContexts call it too. False if the load failed.
    bool load_gl(GLLoadProc loader);

    // What draws a figure, on whatever GL context is current: the NanoVG context
    // and the NvgRenderer on it (fonts and colorbar images, shared by every
    // figure drawn through it). Per-view caches are DataRenderer's.
    //
    // Built on the current context, on the thread that draws; used and destroyed
    // only while that context is current, before the context goes. It never
    // makes a context current, swaps or loads GLAD. Needs GLAD loaded (load_gl())
    // with GL 4.1; throws std::runtime_error otherwise. A current context is a
    // precondition it cannot check.
    class RenderDevice {
    public:
        RenderDevice();
        ~RenderDevice();

        RenderDevice(const RenderDevice&) = delete;
        RenderDevice& operator=(const RenderDevice&) = delete;

        NVGcontext* nvg() const { return vg_; }
        NvgRenderer& renderer() { return *renderer_; }

        // One NanoVG frame of w x h logical pixels. pixel_ratio is the device-pixel
        // ratio (display scale x supersample), which keeps NanoVG's AA and text
        // sharp.
        void begin_nvg_frame(int w, int h, float pixel_ratio = 1.0f) const;
        void end_nvg_frame() const;

    private:
        NVGcontext* vg_ = nullptr;
        std::unique_ptr<NvgRenderer> renderer_;  // destroyed before vg_
    };
} // namespace sextant
