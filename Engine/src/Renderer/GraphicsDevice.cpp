#include "Xora2D/Renderer/GraphicsDevice.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Renderer/GraphicsContext.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <stdexcept>

namespace Xora2D {

GraphicsDevice::GraphicsDevice(GraphicsContext& context) {
    context.MakeCurrent();

    const auto loader = reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress);
    if (gladLoadGL(loader) == 0) {
        throw std::runtime_error("Failed to initialize GLAD");
    }
    if (!GLAD_GL_VERSION_3_3) {
        throw std::runtime_error("OpenGL 3.3 Core is required");
    }

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    m_info.vendor = vendor != nullptr ? vendor : "unknown";
    m_info.renderer = renderer != nullptr ? renderer : "unknown";
    m_info.version = version != nullptr ? version : "unknown";

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    m_info.majorVersion = static_cast<int>(major);
    m_info.minorVersion = static_cast<int>(minor);

    XORA_LOG_INFO("OpenGL vendor: {}", m_info.vendor);
    XORA_LOG_INFO("OpenGL renderer: {}", m_info.renderer);
    XORA_LOG_INFO("OpenGL version: {}", m_info.version);

    if (m_info.majorVersion < 3 || (m_info.majorVersion == 3 && m_info.minorVersion < 3)) {
        throw std::runtime_error("OpenGL 3.3 or newer is required");
    }
}

} // namespace Xora2D