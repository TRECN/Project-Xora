#pragma once

#include "Xora2D/Events/Event.h"
#include "Xora2D/Input/MouseCodes.h"

#include <cstdint>
#include <format>

namespace Xora2D {

class MouseMovedEvent : public Event {
public:
    MouseMovedEvent(float x, float y)
        : m_x(x), m_y(y) {}

    float GetX() const { return m_x; }
    float GetY() const { return m_y; }

    std::string ToString() const override { return std::format("MouseMoved {},{}", m_x, m_y); }

    XORA_EVENT_CLASS_TYPE(MouseMoved)
    XORA_EVENT_CLASS_CATEGORY(EventCategory::Input | EventCategory::Mouse)

private:
    float m_x = 0.0f;
    float m_y = 0.0f;
};

class MouseScrolledEvent : public Event {
public:
    MouseScrolledEvent(float offsetX, float offsetY)
        : m_offsetX(offsetX), m_offsetY(offsetY) {}

    float GetOffsetX() const { return m_offsetX; }
    float GetOffsetY() const { return m_offsetY; }

    std::string ToString() const override {
        return std::format("MouseScrolled {},{}", m_offsetX, m_offsetY);
    }

    XORA_EVENT_CLASS_TYPE(MouseScrolled)
    XORA_EVENT_CLASS_CATEGORY(EventCategory::Input | EventCategory::Mouse)

private:
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
};

class MouseButtonEvent : public Event {
public:
    MouseButton GetButton() const { return m_button; }
    std::uint8_t GetButtonIndex() const { return m_buttonIndex; }
    float GetX() const { return m_x; }
    float GetY() const { return m_y; }

    XORA_EVENT_CLASS_CATEGORY(EventCategory::Input | EventCategory::Mouse | EventCategory::MouseButton)

protected:
    MouseButtonEvent(MouseButton button, std::uint8_t buttonIndex, float x, float y)
        : m_button(button), m_buttonIndex(buttonIndex), m_x(x), m_y(y) {}

private:
    MouseButton m_button = MouseButton::Unknown;
    std::uint8_t m_buttonIndex = 0;
    float m_x = 0.0f;
    float m_y = 0.0f;
};

class MouseButtonPressedEvent : public MouseButtonEvent {
public:
    MouseButtonPressedEvent(MouseButton button, std::uint8_t buttonIndex, float x, float y)
        : MouseButtonEvent(button, buttonIndex, x, y) {}

    std::string ToString() const override {
        return std::format("MouseButtonPressed button={} at {},{}", GetButtonIndex(), GetX(), GetY());
    }

    XORA_EVENT_CLASS_TYPE(MouseButtonPressed)
};

class MouseButtonReleasedEvent : public MouseButtonEvent {
public:
    MouseButtonReleasedEvent(MouseButton button, std::uint8_t buttonIndex, float x, float y)
        : MouseButtonEvent(button, buttonIndex, x, y) {}

    std::string ToString() const override {
        return std::format("MouseButtonReleased button={} at {},{}", GetButtonIndex(), GetX(), GetY());
    }

    XORA_EVENT_CLASS_TYPE(MouseButtonReleased)
};

} // namespace Xora2D
