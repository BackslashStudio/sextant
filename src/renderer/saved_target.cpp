#include "saved_target.h"
#include <glad/glad.h>

namespace sextant {
    void SavedTarget::save() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_VIEWPORT, viewport);
        held = true;
    }

    void SavedTarget::restore() {
        if (!held) return;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        held = false;
    }
} // namespace sextant
