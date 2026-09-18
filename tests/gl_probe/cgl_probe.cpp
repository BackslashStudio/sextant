// macOS half of sextant_gl_probe: a windowless CGL context, bypassing GLFW.
// GLFW's NSGL backend always requires an accelerated renderer, so on a GPU-less
// machine it finds no pixel format at all; CGL can still reach the Apple
// Software Renderer. Asks for 4.1 core twice -- any renderer, then accelerated
// only -- and reports which one each attempt got and whether it renders.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl3.h>

#include <cstdio>

namespace {
    const char* gl_string(GLenum name) {
        const auto* s = reinterpret_cast<const char *>(glGetString(name));
        return s ? s : "(null)";
    }

    bool fbo_readback_ok() {
        GLuint fbo = 0, tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        unsigned char px[4] = {};
        if (complete) {
            glViewport(0, 0, 4, 4);
            glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);

        auto near = [](int got, int want) { return got >= want - 1 && got <= want + 1; };
        const bool ok = complete && near(px[0], 51) && near(px[1], 102) && near(px[2], 153) && px[3] == 255;
        std::printf("FBO readback: %s (framebuffer %s, pixel %d,%d,%d,%d)\n",
                    ok ? "ok" : "FAILED", complete ? "complete" : "incomplete",
                    px[0], px[1], px[2], px[3]);
        return ok;
    }

    // One attempt; true if a 4.1+ core context was created and renders.
    bool try_cgl(bool accelerated_only) {
        std::printf("\n-- CGL, windowless: 4.1 core, %s --\n",
                    accelerated_only ? "accelerated only" : "any renderer");
        CGLPixelFormatAttribute attrs[16];
        int n = 0;
        attrs[n++] = kCGLPFAOpenGLProfile;
        attrs[n++] = static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core);
        attrs[n++] = kCGLPFAColorSize;
        attrs[n++] = static_cast<CGLPixelFormatAttribute>(24);
        attrs[n++] = kCGLPFAAlphaSize;
        attrs[n++] = static_cast<CGLPixelFormatAttribute>(8);
        attrs[n++] = kCGLPFAStencilSize;
        attrs[n++] = static_cast<CGLPixelFormatAttribute>(8);
        if (accelerated_only)
            attrs[n++] = kCGLPFAAccelerated;
        attrs[n] = static_cast<CGLPixelFormatAttribute>(0);

        CGLPixelFormatObj pix = nullptr;
        GLint npix = 0;
        CGLError err = CGLChoosePixelFormat(attrs, &pix, &npix);
        if (err != kCGLNoError || !pix) {
            std::printf("no pixel format: %s\n", CGLErrorString(err));
            return false;
        }
        std::printf("pixel formats: %d\n", npix);

        CGLContextObj ctx = nullptr;
        err = CGLCreateContext(pix, nullptr, &ctx);
        CGLReleasePixelFormat(pix);
        if (err != kCGLNoError || !ctx) {
            std::printf("CGLCreateContext failed: %s\n", CGLErrorString(err));
            return false;
        }
        CGLSetCurrentContext(ctx);

        GLint renderer_id = 0;
        CGLGetParameter(ctx, kCGLCPCurrentRendererID, &renderer_id);
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        std::printf("GL_VENDOR:   %s\n", gl_string(GL_VENDOR));
        std::printf("GL_RENDERER: %s\n", gl_string(GL_RENDERER));
        std::printf("GL_VERSION:  %s\n", gl_string(GL_VERSION));
        std::printf("GLSL:        %s\n", gl_string(GL_SHADING_LANGUAGE_VERSION));
        std::printf("renderer ID: 0x%X%s\n", static_cast<unsigned>(renderer_id),
                    (renderer_id & kCGLRendererIDMatchingMask) == kCGLRendererGenericFloatID
                        ? " (Apple Software Renderer)" : "");
        const bool renders = fbo_readback_ok();

        CGLSetCurrentContext(nullptr);
        CGLDestroyContext(ctx);
        return (major > 4 || (major == 4 && minor >= 1)) && renders;
    }
} // namespace

bool cgl_probe() {
    const bool any = try_cgl(false);
    const bool accelerated = try_cgl(true);
    std::printf("\nCGL RESULT: any renderer %s, accelerated %s\n",
                any ? "ok" : "FAILED", accelerated ? "ok" : "none");
    return any;
}
