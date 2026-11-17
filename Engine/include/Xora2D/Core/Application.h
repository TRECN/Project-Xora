#pragma once

#include "Xora2D/Core/Time.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/GraphicsDevice.h"

#include <glm/vec4.hpp>

#include <memory>

namespace Xora2D {

class Event;
class GraphicsContext;
class Renderer2D;
class RendererAPI;

// Owns one window, its graphics device, and the frame loop.
class Application {
public:
    Application();
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Blocks until the window is closed or the process is asked to quit.
    // Returns EXIT_SUCCESS after a clean shutdown.
    int Run();

    void SetClearColor(const glm::vec4& color);
    const GraphicsInfo& GetGraphicsInfo() const;

protected:
    virtual void OnUpdate(double deltaSeconds);
    virtual void OnRender();

    // Invoked when the window close or application quit event is received.
    virtual void OnShutdownRequested();

    Renderer2D& GetRenderer2D();
    const Renderer2D& GetRenderer2D() const;

    Window& GetWindow();
    const Window& GetWindow() const;

    // Called for every engine event after the application has recorded
    // close and quit requests. Overrides can inspect the event without
    // replacing shutdown behavior.
    virtual void OnEvent(Event& event);

    void Close();

private:
    void HandleEvent(Event& event);

    bool m_running = true;
    glm::vec4 m_clearColor{0.08f, 0.09f, 0.12f, 1.0f};
    std::unique_ptr<Window> m_window;
    std::unique_ptr<GraphicsContext> m_context;
    std::unique_ptr<GraphicsDevice> m_device;
    std::unique_ptr<RendererAPI> m_renderer;
    std::unique_ptr<Renderer2D> m_renderer2D;
    Time m_time;
};

} // namespace Xora2D
