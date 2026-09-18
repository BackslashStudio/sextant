// sextant_gl_probe: which OpenGL does this machine give us? Raw GLFW + GLAD,
// no sextant. Asks for the context sextant needs (4.1 core, forward-compatible,
// hidden window, main thread), prints the driver strings, and checks that a
// clear into an FBO reads back. If 4.1 core is refused, reports the error and
// the renderer a default context gets instead, then exits 1.
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

namespace {
    void print_glfw_error(int code, const char* desc) {
        std::printf("GLFW error 0x%X: %s\n", code, desc);
    }

    const char* platform_name() {
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_WIN32:   return "Win32";
            case GLFW_PLATFORM_COCOA:   return "Cocoa";
            case GLFW_PLATFORM_X11:     return "X11";
            case GLFW_PLATFORM_WAYLAND: return "Wayland";
            case GLFW_PLATFORM_NULL:    return "Null";
            default:                    return "unknown";
        }
    }

    const char* gl_string(GLenum name) {
        const auto* s = reinterpret_cast<const char *>(glGetString(name));
        return s ? s : "(null)";
    }

    void print_context() {
        std::printf("GL_VENDOR:   %s\n", gl_string(GL_VENDOR));
        std::printf("GL_RENDERER: %s\n", gl_string(GL_RENDERER));
        std::printf("GL_VERSION:  %s\n", gl_string(GL_VERSION));
        if (GLVersion.major >= 2)
            std::printf("GLSL:        %s\n", gl_string(GL_SHADING_LANGUAGE_VERSION));
        if (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2)) {
            GLint profile = 0, flags = 0;
            glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
            glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
            std::printf("profile:     %s%s\n",
                        (profile & GL_CONTEXT_CORE_PROFILE_BIT) ? "core" :
                        (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) ? "compatibility" : "none",
                        (flags & GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT) ? ", forward-compatible" : "");
        }
    }

    // Clear an RGBA8 FBO to a known colour and read one pixel back: proves the
    // context renders, not just that it was created.
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

        // 0.2/0.4/0.6 * 255 = 51/102/153; allow one step of rounding.
        auto near = [](int got, int want) { return got >= want - 1 && got <= want + 1; };
        const bool ok = complete && near(px[0], 51) && near(px[1], 102) && near(px[2], 153) && px[3] == 255;
        std::printf("FBO readback: %s (framebuffer %s, pixel %d,%d,%d,%d)\n",
                    ok ? "ok" : "FAILED", complete ? "complete" : "incomplete",
                    px[0], px[1], px[2], px[3]);
        return ok;
    }

    GLFWwindow* make_window(bool want_41_core) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        if (want_41_core) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        }
        return glfwCreateWindow(64, 64, "sextant_gl_probe", nullptr, nullptr);
    }
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    glfwSetErrorCallback(print_glfw_error);
    std::printf("=== sextant_gl_probe ===\n");
    std::printf("GLFW:        %s\n", glfwGetVersionString());

    // Same platform choice as the library (src/renderer/gl_context.cpp).
#if defined(__linux__)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) {
        std::printf("RESULT: glfwInit failed\n");
        return 1;
    }
    std::printf("platform:    %s\n", platform_name());

    std::printf("\n-- requested: 4.1 core, forward-compatible --\n");
    GLFWwindow* window = make_window(true);
    bool granted = window != nullptr;
    if (!granted) {
        std::printf("4.1 core context refused; falling back to a default context\n");
        window = make_window(false);
        if (!window) {
            std::printf("RESULT: no OpenGL context at all\n");
            glfwTerminate();
            return 1;
        }
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::printf("RESULT: context created but GLAD could not load it\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    std::printf("GLAD:        %d.%d\n", GLVersion.major, GLVersion.minor);
    print_context();

    granted = granted && (GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 1));
    const bool renders = GLVersion.major >= 3 && fbo_readback_ok();

    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    std::printf("\nRESULT: 4.1 core %s, rendering %s\n",
                granted ? "granted" : "NOT granted", renders ? "ok" : "FAILED");
    return granted && renders ? 0 : 1;
}
