#include "Xora2D/Renderer/RendererAPI.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Renderer/GraphicsContext.h"

#include <glad/gl.h>

namespace Xora2D {
namespace {

void CheckOpenGLError(const char* operation) {
#ifndef NDEBUG
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        XORA_LOG_ERROR("OpenGL error 0x{:X} during {}", error, operation);
    }
#else
    (void)operation;
#endif
}

class OpenGLRendererAPI final : public RendererAPI {
public:
    explicit OpenGLRendererAPI(GraphicsContext& context)
        : m_context(context) {}

    void SetClearColor(const glm::vec4& color) override {
        glClearColor(color.r, color.g, color.b, color.a);
        CheckOpenGLError("glClearColor");
    }

    void Clear() override {
        glClear(GL_COLOR_BUFFER_BIT);
        CheckOpenGLError("glClear");
    }

    void SetViewport(int x, int y, int width, int height) override {
        glViewport(x, y, width, height);
        CheckOpenGLError("glViewport");
    }

    void SwapBuffers() override {
        // macOS presents the default framebuffer. Binding it keeps a later
        // offscreen target from swallowing the swap.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CheckOpenGLError("glBindFramebuffer");
        m_context.SwapBuffers();
    }

private:
    GraphicsContext& m_context;
};

} // namespace

std::unique_ptr<RendererAPI> RendererAPI::Create(GraphicsContext& context) {
    return std::make_unique<OpenGLRendererAPI>(context);
}

} // namespace Xora2D
