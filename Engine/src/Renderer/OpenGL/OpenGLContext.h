#pragma once

#include "Xora2D/Renderer/GraphicsContext.h"

#include <SDL3/SDL.h>

namespace Xora2D {

class OpenGLContext : public GraphicsContext {
public:
    explicit OpenGLContext(SDL_Window* window);
    ~OpenGLContext() override;

    OpenGLContext(const OpenGLContext&) = delete;
    OpenGLContext& operator=(const OpenGLContext&) = delete;

    void MakeCurrent() override;
    void SwapBuffers() override;
    bool SetVSync(bool enabled) override;
    bool IsVSyncEnabled() const override;

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    bool m_vsync = false;
};

} // namespace Xora2D
