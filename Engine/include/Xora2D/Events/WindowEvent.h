#pragma once

#include "Xora2D/Events/Event.h"

#include <format>

namespace Xora2D {

class WindowCloseEvent : public Event {
public:
    XORA_EVENT_CLASS_TYPE(WindowClose)
    XORA_EVENT_CLASS_CATEGORY(EventCategory::Application | EventCategory::Window)
};

class WindowResizeEvent : public Event {
public:
    WindowResizeEvent(int width, int height)
        : m_width(width), m_height(height) {}

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    std::string ToString() const override {
        return std::format("WindowResize {}x{}", m_width, m_height);
    }

    XORA_EVENT_CLASS_TYPE(WindowResize)
    XORA_EVENT_CLASS_CATEGORY(EventCategory::Application | EventCategory::Window)

private:
    int m_width = 0;
    int m_height = 0;
};

} // namespace Xora2D
