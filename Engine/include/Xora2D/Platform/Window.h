#pragma once

#include "Xora2D/Events/Event.h"
#include "Xora2D/Renderer/GraphicsContext.h"

#include <functional>
#include <memory>
#include <string>

namespace Xora2D {

struct WindowProperties {
    std::string title = "Xora2D Engine";
    int width = 1280;
    int height = 720;
    // Context attributes are applied before the window is created.
    bool enableOpenGL = false;
};

// Desktop window. SDL stays inside the implementation, so including this
// header does not include SDL.
class Window {
public:
    using EventCallback = std::function<void(Event&)>;

    explicit Window(const WindowProperties& properties = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    // Drains the platform queue and turns each relevant event into an
    // Xora2D event. Returns immediately when the queue is empty.
    void PollEvents();

    void SetEventCallback(EventCallback callback);
    void SetTitle(const std::string& title);

    // Creates an OpenGL 3.3 core context for this window. The window must
    // outlive the returned context.
    std::unique_ptr<GraphicsContext> CreateGraphicsContext();

    const std::string& GetTitle() const;
    int GetWidth() const;
    int GetHeight() const;

    // Drawable size in pixels. This differs from GetWidth/GetHeight on a
    // high-density display such as a Retina screen.
    void GetFramebufferSize(int& width, int& height) const;
    int GetFramebufferWidth() const;
    int GetFramebufferHeight() const;

    // Opaque native window handle (SDL_Window*). For editor platform backends only.
    void* GetNativeWindowHandle() const;

    using SdlEventHook = std::function<void(const void* sdlEvent)>;
    void SetSdlEventHook(SdlEventHook hook);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Xora2D
