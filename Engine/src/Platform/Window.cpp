#include "Xora2D/Platform/Window.h"

#include "Platform/SdlEventTranslation.h"
#include "Renderer/OpenGL/OpenGLContext.h"

#include "Xora2D/Events/ApplicationEvent.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/MouseEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Input/KeyCodes.h"
#include "Xora2D/Input/MouseCodes.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace Xora2D {
namespace {

class SdlVideoSession {
public:
    SdlVideoSession() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("Failed to initialize SDL: ") + SDL_GetError());
        }
    }

    ~SdlVideoSession() {
        // SDL_Quit tears down every subsystem. One session is shared by every
        // live window, so this runs only after the last window is destroyed.
        SDL_Quit();
    }

    SdlVideoSession(const SdlVideoSession&) = delete;
    SdlVideoSession& operator=(const SdlVideoSession&) = delete;
};

std::shared_ptr<SdlVideoSession> AcquireSdlVideo() {
    static std::mutex mutex;
    static std::weak_ptr<SdlVideoSession> existing;
    std::lock_guard lock(mutex);
    if (std::shared_ptr<SdlVideoSession> session = existing.lock()) {
        return session;
    }

    auto session = std::make_shared<SdlVideoSession>();
    existing = session;
    return session;
}

Key KeyFromScanCode(SDL_Scancode scanCode) {
    if (scanCode >= SDL_SCANCODE_A && scanCode <= SDL_SCANCODE_Z) {
        return static_cast<Key>(static_cast<std::uint16_t>(Key::A) +
                                static_cast<std::uint16_t>(scanCode - SDL_SCANCODE_A));
    }
    if (scanCode >= SDL_SCANCODE_1 && scanCode <= SDL_SCANCODE_0) {
        return static_cast<Key>(static_cast<std::uint16_t>(Key::Digit1) +
                                static_cast<std::uint16_t>(scanCode - SDL_SCANCODE_1));
    }
    if (scanCode >= SDL_SCANCODE_F1 && scanCode <= SDL_SCANCODE_F12) {
        return static_cast<Key>(static_cast<std::uint16_t>(Key::F1) +
                                static_cast<std::uint16_t>(scanCode - SDL_SCANCODE_F1));
    }

    switch (scanCode) {
    case SDL_SCANCODE_RETURN:
        return Key::Return;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftControl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_RCTRL:
        return Key::RightControl;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_INSERT:
        return Key::Insert;
    case SDL_SCANCODE_DELETE:
        return Key::Delete;
    case SDL_SCANCODE_HOME:
        return Key::Home;
    case SDL_SCANCODE_END:
        return Key::End;
    case SDL_SCANCODE_PAGEUP:
        return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return Key::PageDown;
    case SDL_SCANCODE_LGUI:
        return Key::LeftSuper;
    case SDL_SCANCODE_RGUI:
        return Key::RightSuper;
    case SDL_SCANCODE_SEMICOLON:
        return Key::Semicolon;
    case SDL_SCANCODE_APOSTROPHE:
        return Key::Apostrophe;
    case SDL_SCANCODE_COMMA:
        return Key::Comma;
    case SDL_SCANCODE_PERIOD:
        return Key::Period;
    case SDL_SCANCODE_SLASH:
        return Key::Slash;
    case SDL_SCANCODE_BACKSLASH:
        return Key::Backslash;
    case SDL_SCANCODE_MINUS:
        return Key::Minus;
    case SDL_SCANCODE_EQUALS:
        return Key::Equals;
    case SDL_SCANCODE_GRAVE:
        return Key::Grave;
    case SDL_SCANCODE_LEFTBRACKET:
        return Key::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET:
        return Key::RightBracket;
    case SDL_SCANCODE_KP_0:
        return Key::Keypad0;
    case SDL_SCANCODE_KP_1:
        return Key::Keypad1;
    case SDL_SCANCODE_KP_2:
        return Key::Keypad2;
    case SDL_SCANCODE_KP_3:
        return Key::Keypad3;
    case SDL_SCANCODE_KP_4:
        return Key::Keypad4;
    case SDL_SCANCODE_KP_5:
        return Key::Keypad5;
    case SDL_SCANCODE_KP_6:
        return Key::Keypad6;
    case SDL_SCANCODE_KP_7:
        return Key::Keypad7;
    case SDL_SCANCODE_KP_8:
        return Key::Keypad8;
    case SDL_SCANCODE_KP_9:
        return Key::Keypad9;
    case SDL_SCANCODE_KP_ENTER:
        return Key::KeypadEnter;
    default:
        return Key::Unknown;
    }
}

MouseButton MouseButtonFromIndex(std::uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return MouseButton::Unknown;
    }
}

bool ConfigureOpenGLAttributes() {
    return SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) &&
           SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) &&
           SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) &&
           SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) &&
           SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

void RefreshFramebufferSize(SDL_Window* window, int& framebufferWidth, int& framebufferHeight) {
    if (!SDL_GetWindowSizeInPixels(window, &framebufferWidth, &framebufferHeight)) {
        SDL_GetWindowSize(window, &framebufferWidth, &framebufferHeight);
    }
}

bool IsForWindow(const SDL_Event& event, SDL_WindowID windowId) {
    const auto matches = [windowId](SDL_WindowID eventWindow) {
        return eventWindow == 0 || eventWindow == windowId;
    };

    switch (event.type) {
    case SDL_EVENT_QUIT:
        return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return matches(event.window.windowID);
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return matches(event.key.windowID);
    case SDL_EVENT_MOUSE_MOTION:
        return matches(event.motion.windowID);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return matches(event.button.windowID);
    case SDL_EVENT_MOUSE_WHEEL:
        return matches(event.wheel.windowID);
    default:
        return false;
    }
}

} // namespace

TranslatedEvent TranslateSdlEvent(const SDL_Event& sdlEvent) {
    TranslatedEvent translated;

    switch (sdlEvent.type) {
    case SDL_EVENT_QUIT:
        translated.event = std::make_unique<AppQuitEvent>();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        translated.event = std::make_unique<WindowCloseEvent>();
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        translated.resized = true;
        translated.width = sdlEvent.window.data1;
        translated.height = sdlEvent.window.data2;
        translated.event = std::make_unique<WindowResizeEvent>(translated.width, translated.height);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        translated.event = std::make_unique<WindowFocusLostEvent>();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        translated.event = std::make_unique<WindowFocusGainedEvent>();
        break;
    case SDL_EVENT_KEY_DOWN:
        translated.event = std::make_unique<KeyPressedEvent>(
            KeyFromScanCode(sdlEvent.key.scancode), static_cast<std::uint32_t>(sdlEvent.key.scancode),
            sdlEvent.key.repeat);
        break;
    case SDL_EVENT_KEY_UP:
        translated.event = std::make_unique<KeyReleasedEvent>(
            KeyFromScanCode(sdlEvent.key.scancode), static_cast<std::uint32_t>(sdlEvent.key.scancode));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        translated.event = std::make_unique<MouseMovedEvent>(sdlEvent.motion.x, sdlEvent.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        translated.event = std::make_unique<MouseButtonPressedEvent>(
            MouseButtonFromIndex(sdlEvent.button.button), sdlEvent.button.button, sdlEvent.button.x,
            sdlEvent.button.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        translated.event = std::make_unique<MouseButtonReleasedEvent>(
            MouseButtonFromIndex(sdlEvent.button.button), sdlEvent.button.button, sdlEvent.button.x,
            sdlEvent.button.y);
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        // Natural scrolling reports the opposite sign. Normalize it here so
        // engine code sees positive Y as scrolling away from the user.
        const float direction = sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        translated.event = std::make_unique<MouseScrolledEvent>(sdlEvent.wheel.x * direction,
                                                                sdlEvent.wheel.y * direction);
        break;
    }
    default:
        break;
    }

    return translated;
}

struct Window::Impl {
    std::shared_ptr<SdlVideoSession> sdl;
    SDL_Window* window = nullptr;
    SDL_WindowID windowId = 0;
    std::string title;
    int width = 0;
    int height = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    bool opengl = false;
    EventCallback callback;

    ~Impl() {
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    }
};

Window::Window(const WindowProperties& props)
    : m_impl(std::make_unique<Impl>()) {
    if (props.width <= 0 || props.height <= 0) {
        throw std::runtime_error("Window width and height must be positive");
    }

    m_impl->sdl = AcquireSdlVideo();
    m_impl->title = props.title;

    m_impl->opengl = props.enableOpenGL;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (props.enableOpenGL) {
        if (!ConfigureOpenGLAttributes()) {
            throw std::runtime_error(std::string("Failed to configure OpenGL attributes: ") + SDL_GetError());
        }
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    }

    m_impl->window = SDL_CreateWindow(props.title.c_str(), props.width, props.height, flags);
    if (m_impl->window == nullptr) {
        throw std::runtime_error(std::string("Failed to create window: ") + SDL_GetError());
    }

    m_impl->windowId = SDL_GetWindowID(m_impl->window);
    if (!SDL_GetWindowSize(m_impl->window, &m_impl->width, &m_impl->height)) {
        m_impl->width = props.width;
        m_impl->height = props.height;
    }
    RefreshFramebufferSize(m_impl->window, m_impl->framebufferWidth, m_impl->framebufferHeight);
}

Window::~Window() = default;

void Window::PollEvents() {
    SDL_Event sdlEvent{};
    while (SDL_PollEvent(&sdlEvent)) {
        if (!IsForWindow(sdlEvent, m_impl->windowId)) {
            continue;
        }

        TranslatedEvent translated = TranslateSdlEvent(sdlEvent);
        if (translated.resized) {
            m_impl->width = translated.width;
            m_impl->height = translated.height;
            RefreshFramebufferSize(m_impl->window, m_impl->framebufferWidth, m_impl->framebufferHeight);
        } else if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
                   IsForWindow(sdlEvent, m_impl->windowId)) {
            RefreshFramebufferSize(m_impl->window, m_impl->framebufferWidth, m_impl->framebufferHeight);
        }
        if (translated.event && m_impl->callback) {
            m_impl->callback(*translated.event);
        }
    }
}

std::unique_ptr<GraphicsContext> Window::CreateGraphicsContext() {
    if (!m_impl->opengl) {
        throw std::runtime_error("Cannot create a graphics context for a window opened without OpenGL");
    }
    return std::make_unique<OpenGLContext>(m_impl->window);
}

void Window::SetEventCallback(EventCallback callback) {
    m_impl->callback = std::move(callback);
}

void Window::SetTitle(const std::string& title) {
    m_impl->title = title;
    SDL_SetWindowTitle(m_impl->window, m_impl->title.c_str());
}

const std::string& Window::GetTitle() const {
    return m_impl->title;
}

int Window::GetWidth() const {
    return m_impl->width;
}

int Window::GetHeight() const {
    return m_impl->height;
}

void Window::GetFramebufferSize(int& width, int& height) const {
    if (!SDL_GetWindowSizeInPixels(m_impl->window, &width, &height)) {
        width = m_impl->width;
        height = m_impl->height;
    }
}

int Window::GetFramebufferWidth() const {
    return m_impl->framebufferWidth;
}

int Window::GetFramebufferHeight() const {
    return m_impl->framebufferHeight;
}

} // namespace Xora2D
