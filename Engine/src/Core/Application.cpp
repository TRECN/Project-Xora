#include "Xora2D/Core/Application.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Events/ApplicationEvent.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Renderer/GraphicsDevice.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/RendererAPI.h"

#include <cstdlib>

namespace Xora2D {

Application::Application()
    : m_window(std::make_unique<Window>(WindowProperties{
          .title = "Xora2D Engine",
          .width = 1280,
          .height = 720,
          .enableOpenGL = true,
      })) {
    m_window->SetEventCallback([this](Event& event) { HandleEvent(event); });
    m_context = m_window->CreateGraphicsContext();
    m_context->MakeCurrent();
    m_device = std::make_unique<GraphicsDevice>(*m_context);
    m_renderer = RendererAPI::Create(*m_context);
    m_renderer->SetClearColor(m_clearColor);
    if (!m_context->SetVSync(true)) {
        XORA_LOG_WARN("VSync could not be enabled");
    }
    XORA_LOG_INFO("Xora2D Engine initialized");
}

Application::~Application() {
    // Release the renderer and device before the context, and the context
    // before the window that owns the native drawable.
    m_renderer.reset();
    m_device.reset();
    m_context.reset();
    XORA_LOG_INFO("Xora2D Engine shutdown");
}

int Application::Run() {
    while (m_running) {
        m_window->PollEvents();
        const double deltaSeconds = m_time.Tick();
        OnUpdate(deltaSeconds);

        const int framebufferWidth = m_window->GetFramebufferWidth();
        const int framebufferHeight = m_window->GetFramebufferHeight();
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            m_renderer->SetViewport(0, 0, framebufferWidth, framebufferHeight);
        }
        m_renderer->SetClearColor(m_clearColor);
        m_renderer->Clear();
        OnRender();
        m_renderer->SwapBuffers();
    }

    return EXIT_SUCCESS;
}

void Application::SetClearColor(const glm::vec4& color) {
    m_clearColor = color;
    if (m_renderer) {
        m_renderer->SetClearColor(color);
    }
}

const GraphicsInfo& Application::GetGraphicsInfo() const {
    return m_device->GetInfo();
}

void Application::OnUpdate(double /*deltaSeconds*/) {}

void Application::OnRender() {}

void Application::OnEvent(Event& /*event*/) {}

void Application::Close() {
    m_running = false;
}

void Application::HandleEvent(Event& event) {
    EventDispatcher<WindowCloseEvent> closeDispatcher(event);
    closeDispatcher.Dispatch([this](WindowCloseEvent&) {
        XORA_LOG_INFO("Window close requested");
        m_running = false;
        return true;
    });

    EventDispatcher<AppQuitEvent> quitDispatcher(event);
    quitDispatcher.Dispatch([this](AppQuitEvent&) {
        XORA_LOG_INFO("Application quit requested");
        m_running = false;
        return true;
    });

    EventDispatcher<WindowResizeEvent> resizeDispatcher(event);
    resizeDispatcher.Dispatch([this](WindowResizeEvent& resize) {
        XORA_LOG_INFO("Window resized to {}x{}", resize.GetWidth(), resize.GetHeight());
        XORA_LOG_INFO("Framebuffer size {}x{}", m_window->GetFramebufferWidth(),
                      m_window->GetFramebufferHeight());
        return false;
    });

    OnEvent(event);
}

} // namespace Xora2D
