#include "Xora2D/Core/Application.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Events/ApplicationEvent.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Renderer/GraphicsDevice.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/RendererAPI.h"
#include "Xora2D/Input/Input.h"

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
    m_renderer2D = std::make_unique<Renderer2D>();
    m_renderer2D->Init();
    m_renderer->SetClearColor(m_clearColor);
    if (!m_context->SetVSync(true)) {
        XORA_LOG_WARN("VSync could not be enabled");
    }
    Input::Attach(&m_input);
    const AudioInitResult audioInit = m_audioEngine.Initialize();
    if (!audioInit.Success) {
        XORA_LOG_WARN("Audio engine failed to initialize: {}", audioInit.Message);
    } else if (!audioInit.DeviceAvailable) {
        XORA_LOG_WARN("Audio output device unavailable; continuing without audible output");
    }
    XORA_LOG_INFO("Xora2D Engine initialized");
}

Application::~Application() {
    m_audioEngine.PrepareShutdown();
    m_input.PrepareShutdown();
    Input::Attach(nullptr);
    // Release the renderer and device before the context, and the context
    // before the window that owns the native drawable.
    m_renderer2D.reset();
    m_renderer.reset();
    m_device.reset();
    m_context.reset();
    XORA_LOG_INFO("Xora2D Engine shutdown");
}

int Application::Run() {
    while (m_running) {
        m_input.BeginFrame();
        m_window->PollEvents();
        if (!m_running) {
            break;
        }
        const double deltaSeconds = m_time.Tick();
        m_audioEngine.Update(static_cast<float>(deltaSeconds));
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

void Application::OnShutdownRequested() {
    m_audioEngine.StopAll();
    m_input.ClearState();
}

Renderer2D& Application::GetRenderer2D() {
    return *m_renderer2D;
}

const Renderer2D& Application::GetRenderer2D() const {
    return *m_renderer2D;
}

Window& Application::GetWindow() {
    return *m_window;
}

const Window& Application::GetWindow() const {
    return *m_window;
}

void Application::Close() {
    m_running = false;
}

void Application::HandleEvent(Event& event) {
    EventDispatcher<WindowCloseEvent> closeDispatcher(event);
    closeDispatcher.Dispatch([this](WindowCloseEvent&) {
        XORA_LOG_INFO("Window close requested");
        OnShutdownRequested();
        m_running = false;
        return true;
    });

    EventDispatcher<AppQuitEvent> quitDispatcher(event);
    quitDispatcher.Dispatch([this](AppQuitEvent&) {
        XORA_LOG_INFO("Application quit requested");
        OnShutdownRequested();
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

    m_input.OnEvent(event);
    OnEvent(event);
}

} // namespace Xora2D
