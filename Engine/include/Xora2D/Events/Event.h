#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Xora2D {

enum class EventType {
    None = 0,
    AppQuit,
    WindowClose,
    WindowResize,
    KeyPressed,
    KeyReleased,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseMoved,
    MouseScrolled,
};

enum class EventCategory : std::uint16_t {
    None = 0,
    Application = 1 << 0,
    Window = 1 << 1,
    Input = 1 << 2,
    Keyboard = 1 << 3,
    Mouse = 1 << 4,
    MouseButton = 1 << 5,
};

constexpr EventCategory operator|(EventCategory left, EventCategory right) {
    return static_cast<EventCategory>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
}

constexpr EventCategory operator&(EventCategory left, EventCategory right) {
    return static_cast<EventCategory>(static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
}

constexpr bool HasCategory(EventCategory flags, EventCategory category) {
    return (static_cast<std::uint16_t>(flags) & static_cast<std::uint16_t>(category)) != 0;
}

class Event {
public:
    virtual ~Event() = default;

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    virtual EventType GetType() const = 0;
    virtual std::string_view GetName() const = 0;
    virtual EventCategory GetCategoryFlags() const = 0;
    virtual std::string ToString() const { return std::string(GetName()); }

    bool IsInCategory(EventCategory category) const {
        return HasCategory(GetCategoryFlags(), category);
    }

    bool IsHandled() const { return m_handled; }
    void SetHandled(bool handled) { m_handled = handled; }

protected:
    Event() = default;

private:
    bool m_handled = false;
};

// Binds a concrete event to its EventType so the dispatcher can match it
// without a hand-written type tag on every method.
#define XORA_EVENT_CLASS_TYPE(type)                                                    \
    static EventType GetStaticType() { return EventType::type; }                      \
    EventType GetType() const override { return GetStaticType(); }                    \
    std::string_view GetName() const override { return #type; }

#define XORA_EVENT_CLASS_CATEGORY(flags)                                               \
    EventCategory GetCategoryFlags() const override { return flags; }

template <typename T>
class EventDispatcher {
public:
    explicit EventDispatcher(Event& event)
        : m_event(event) {}

    // Invokes callback only when the event is a T. The callback returns true
    // when it has consumed the event.
    template <typename Callback>
    bool Dispatch(Callback&& callback) {
        if (m_event.GetType() != T::GetStaticType()) {
            return false;
        }

        if (callback(static_cast<T&>(m_event))) {
            m_event.SetHandled(true);
        }
        return true;
    }

private:
    Event& m_event;
};

} // namespace Xora2D
