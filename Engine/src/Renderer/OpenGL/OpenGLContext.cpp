#include "Renderer/OpenGL/OpenGLContext.h"

#include "Xora2D/Core/Logger.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace Xora2D {

OpenGLContext::OpenGLContext(SDL_Window* window)
    : m_window(window) {
    if (m_window == nullptr) {
        throw std::runtime_error("Cannot create an OpenGL context without a window");
    }

    m_context = SDL_GL_CreateContext(m_window);
    if (m_context == nullptr) {
        throw std::runtime_error(std::string("Failed to create OpenGL context: ") + SDL_GetError());
    }
}

OpenGLContext::~OpenGLContext() {
    if (m_context != nullptr) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
}

void OpenGLContext::MakeCurrent() {
    if (!SDL_GL_MakeCurrent(m_window, m_context)) {
        throw std::runtime_error(std::string("Failed to make OpenGL context current: ") + SDL_GetError());
    }
}

void OpenGLContext::SwapBuffers() {
    if (!SDL_GL_SwapWindow(m_window)) {
        throw std::runtime_error(std::string("Failed to swap OpenGL buffers: ") + SDL_GetError());
    }
}

bool OpenGLContext::SetVSync(bool enabled) {
    const int interval = enabled ? 1 : 0;
    if (!SDL_GL_SetSwapInterval(interval)) {
        XORA_LOG_WARN("Failed to set the OpenGL swap interval to {}: {}", interval, SDL_GetError());
        m_vsync = false;
        return false;
    }

    int actual = 0;
    if (SDL_GL_GetSwapInterval(&actual)) {
        m_vsync = actual != 0;
        return m_vsync == enabled;
    }

    m_vsync = enabled;
    return true;
}

bool OpenGLContext::IsVSyncEnabled() const {
    return m_vsync;
}

} // namespace Xora2D
