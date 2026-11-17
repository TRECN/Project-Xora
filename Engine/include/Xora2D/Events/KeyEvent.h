#pragma once

#include "Xora2D/Events/Event.h"
#include "Xora2D/Input/KeyCodes.h"

#include <cstdint>
#include <format>

namespace Xora2D {

class KeyEvent : public Event {
public:
    Key GetKey() const { return m_key; }

    // USB HID scancode reported by the platform. Kept so an unmapped key is
    // still identifiable when GetKey() is Key::Unknown.
    std::uint32_t GetScanCode() const { return m_scanCode; }

    XORA_EVENT_CLASS_CATEGORY(EventCategory::Input | EventCategory::Keyboard)

protected:
    KeyEvent(Key key, std::uint32_t scanCode)
        : m_key(key), m_scanCode(scanCode) {}

private:
    Key m_key = Key::Unknown;
    std::uint32_t m_scanCode = 0;
};

class KeyPressedEvent : public KeyEvent {
public:
    KeyPressedEvent(Key key, std::uint32_t scanCode, bool repeat)
        : KeyEvent(key, scanCode), m_repeat(repeat) {}

    bool IsRepeat() const { return m_repeat; }

    std::string ToString() const override {
        return std::format("KeyPressed key={} scan={} repeat={}", static_cast<int>(GetKey()), GetScanCode(),
                           m_repeat);
    }

    XORA_EVENT_CLASS_TYPE(KeyPressed)

private:
    bool m_repeat = false;
};

class KeyReleasedEvent : public KeyEvent {
public:
    KeyReleasedEvent(Key key, std::uint32_t scanCode)
        : KeyEvent(key, scanCode) {}

    std::string ToString() const override {
        return std::format("KeyReleased key={} scan={}", static_cast<int>(GetKey()), GetScanCode());
    }

    XORA_EVENT_CLASS_TYPE(KeyReleased)
};

} // namespace Xora2D
