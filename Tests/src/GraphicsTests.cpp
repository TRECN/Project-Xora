#include "Xora2D/Core/Application.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/GraphicsDevice.h"
#include "Xora2D/Renderer/RendererAPI.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

int g_graphicsFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_graphicsFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

bool NearByte(int actual, float channel) {
    const int expected = static_cast<int>(std::lround(channel * 255.0f));
    return std::abs(actual - expected) <= 1;
}

class FrameProbe : public Xora2D::Application {
protected:
    void OnRender() override {
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        m_viewportWidth = viewport[2];
        m_viewportHeight = viewport[3];
        ++m_frames;
        if (m_frames >= 2) {
            Close();
        }
    }

public:
    int m_frames = 0;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
};

} // namespace

int RunGraphicsTests() {
    const int failuresBefore = g_graphicsFailures;
    XORA_LOG_INFO("TEST graphics context started");

    try {
        Xora2D::WindowProperties properties;
        properties.title = "Xora2D Graphics Test";
        properties.width = 320;
        properties.height = 240;
        properties.enableOpenGL = true;

        Xora2D::Window window(properties);
        std::unique_ptr<Xora2D::GraphicsContext> context = window.CreateGraphicsContext();
        context->MakeCurrent();

        Xora2D::GraphicsDevice device(*context);
        const Xora2D::GraphicsInfo& info = device.GetInfo();
        XORA_LOG_INFO("  graphics vendor: {}", info.vendor);
        XORA_LOG_INFO("  graphics renderer: {}", info.renderer);
        XORA_LOG_INFO("  graphics version: {}", info.version);
        CHECK(info.majorVersion > 3 || (info.majorVersion == 3 && info.minorVersion >= 3),
              "OpenGL version is at least 3.3");
        CHECK(!info.vendor.empty() && info.vendor != "unknown", "OpenGL vendor is reported");
        CHECK(!info.renderer.empty() && info.renderer != "unknown", "OpenGL renderer is reported");
        CHECK(!info.version.empty() && info.version != "unknown", "OpenGL version string is reported");

        CHECK(context->SetVSync(true), "VSync can be enabled");
        CHECK(context->IsVSyncEnabled(), "VSync stays enabled");
        CHECK(context->SetVSync(false), "VSync can be disabled");
        CHECK(!context->IsVSyncEnabled(), "VSync stays disabled");
        CHECK(context->SetVSync(true), "VSync can be enabled again");

        auto renderer = Xora2D::RendererAPI::Create(*context);
        const glm::vec4 clearColor{0.25f, 0.50f, 0.75f, 1.0f};
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        window.GetFramebufferSize(framebufferWidth, framebufferHeight);
        CHECK(framebufferWidth >= properties.width && framebufferHeight >= properties.height,
              "framebuffer is at least the requested window size");

        renderer->SetViewport(0, 0, framebufferWidth, framebufferHeight);
        renderer->SetClearColor(clearColor);
        renderer->Clear();

        std::uint8_t pixel[4] = {};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        XORA_LOG_INFO("  cleared pixel: {}, {}, {}, {}", pixel[0], pixel[1], pixel[2], pixel[3]);
        CHECK(NearByte(pixel[0], clearColor.r) && NearByte(pixel[1], clearColor.g) &&
                  NearByte(pixel[2], clearColor.b) && NearByte(pixel[3], clearColor.a),
              "framebuffer pixel matches the clear color");

        SDL_Window* nativeWindow = SDL_GL_GetCurrentWindow();
        CHECK(nativeWindow != nullptr, "OpenGL context has a current window");
        CHECK(SDL_SetWindowSize(nativeWindow, 640, 360), "window resize was requested");
        CHECK(SDL_SyncWindow(nativeWindow), "window resize completed");
        window.PollEvents();

        int resizedWidth = 0;
        int resizedHeight = 0;
        window.GetFramebufferSize(resizedWidth, resizedHeight);
        renderer->SetViewport(0, 0, resizedWidth, resizedHeight);
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        XORA_LOG_INFO("  framebuffer after resize: {}x{}", resizedWidth, resizedHeight);
        XORA_LOG_INFO("  viewport after resize: {}x{}", viewport[2], viewport[3]);
        CHECK(resizedWidth >= 640 && resizedHeight >= 360, "resized framebuffer grew to the new size");
        CHECK(viewport[2] == resizedWidth && viewport[3] == resizedHeight,
              "viewport matches the framebuffer after resize");
        renderer->SwapBuffers();
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("  fail: graphics context test threw {}", exception.what());
        ++g_graphicsFailures;
    }

    try {
        FrameProbe probe;
        const int exitCode = probe.Run();
        CHECK(exitCode == EXIT_SUCCESS, "application frame loop exits cleanly");
        CHECK(probe.m_frames >= 2, "application rendered at least two frames");
        CHECK(probe.m_viewportWidth > 0 && probe.m_viewportHeight > 0,
              "application viewport follows the framebuffer");
        XORA_LOG_INFO("  application viewport: {}x{}", probe.m_viewportWidth, probe.m_viewportHeight);
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("  fail: application frame loop threw {}", exception.what());
        ++g_graphicsFailures;
    }

    const int failedHere = g_graphicsFailures - failuresBefore;
    if (failedHere == 0) {
        XORA_LOG_INFO("TEST graphics context passed");
    } else {
        XORA_LOG_ERROR("TEST graphics context failed ({} checks)", failedHere);
    }
    return failedHere;
}
