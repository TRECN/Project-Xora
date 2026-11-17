#include "Xora2D/Input/InputManager.h"

#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/MouseEvent.h"
#include "Xora2D/Events/WindowEvent.h"

#include <algorithm>
#include <cstring>

namespace Xora2D {

namespace {

void ResetTransitions(bool* pressed, bool* released, std::size_t count) {
    std::fill(pressed, pressed + count, false);
    std::fill(released, released + count, false);
}

} // namespace

void InputManager::BeginFrame() {
    if (!m_enabled) {
        return;
    }
    ResetTransitions(m_keyPressed, m_keyReleased, kKeyCount);
    ResetTransitions(m_mousePressed, m_mouseReleased, kMouseButtonCount);
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = glm::vec2(0.0f);
}

void InputManager::OnEvent(Event& event) {
    if (!m_enabled) {
        return;
    }

    EventDispatcher<KeyPressedEvent> keyPressed(event);
    keyPressed.Dispatch([this](KeyPressedEvent& pressed) {
        SetKeyDown(pressed.GetKey(), pressed.IsRepeat());
        return false;
    });

    EventDispatcher<KeyReleasedEvent> keyReleased(event);
    keyReleased.Dispatch([this](KeyReleasedEvent& released) {
        SetKeyUp(released.GetKey());
        return false;
    });

    EventDispatcher<MouseButtonPressedEvent> mousePressed(event);
    mousePressed.Dispatch([this](MouseButtonPressedEvent& pressed) {
        SetMouseDown(pressed.GetButton());
        m_mousePosition = glm::vec2(pressed.GetX(), pressed.GetY());
        return false;
    });

    EventDispatcher<MouseButtonReleasedEvent> mouseReleased(event);
    mouseReleased.Dispatch([this](MouseButtonReleasedEvent& released) {
        SetMouseUp(released.GetButton());
        m_mousePosition = glm::vec2(released.GetX(), released.GetY());
        return false;
    });

    EventDispatcher<MouseMovedEvent> mouseMoved(event);
    mouseMoved.Dispatch([this](MouseMovedEvent& moved) {
        const glm::vec2 next(moved.GetX(), moved.GetY());
        m_mouseDelta += next - m_mousePosition;
        m_mousePosition = next;
        return false;
    });

    EventDispatcher<MouseScrolledEvent> mouseScrolled(event);
    mouseScrolled.Dispatch([this](MouseScrolledEvent& scrolled) {
        m_scrollDelta.x += scrolled.GetOffsetX();
        m_scrollDelta.y += scrolled.GetOffsetY();
        return false;
    });

    EventDispatcher<WindowFocusLostEvent> focusLost(event);
    focusLost.Dispatch([this](WindowFocusLostEvent&) {
        OnFocusLost();
        return false;
    });
}

void InputManager::ClearState() {
    OnFocusLost();
    m_mousePosition = glm::vec2(0.0f);
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = glm::vec2(0.0f);
    std::memset(m_keyPressed, 0, sizeof(m_keyPressed));
    std::memset(m_keyReleased, 0, sizeof(m_keyReleased));
    std::memset(m_mousePressed, 0, sizeof(m_mousePressed));
    std::memset(m_mouseReleased, 0, sizeof(m_mouseReleased));
}

void InputManager::PrepareShutdown() {
    m_enabled = false;
    ClearState();
}

bool InputManager::IsKeyPressed(Key key) const {
    if (!IsValidKey(key)) {
        return false;
    }
    return m_keyPressed[KeyIndex(key)];
}

bool InputManager::IsKeyReleased(Key key) const {
    if (!IsValidKey(key)) {
        return false;
    }
    return m_keyReleased[KeyIndex(key)];
}

bool InputManager::IsKeyDown(Key key) const {
    if (!IsValidKey(key)) {
        return false;
    }
    return m_keyDown[KeyIndex(key)];
}

bool InputManager::IsKeyUp(Key key) const {
    return !IsKeyDown(key);
}

bool InputManager::IsMouseButtonPressed(MouseButton button) const {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    return m_mousePressed[MouseButtonIndex(button)];
}

bool InputManager::IsMouseButtonReleased(MouseButton button) const {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    return m_mouseReleased[MouseButtonIndex(button)];
}

bool InputManager::IsMouseButtonDown(MouseButton button) const {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    return m_mouseDown[MouseButtonIndex(button)];
}

bool InputManager::IsMouseButtonUp(MouseButton button) const {
    return !IsMouseButtonDown(button);
}

void InputManager::SetKeyDown(Key key, bool repeat) {
    if (!IsValidKey(key)) {
        return;
    }
    const std::size_t index = KeyIndex(key);
    if (repeat && m_keyDown[index]) {
        return;
    }
    if (!m_keyDown[index]) {
        m_keyPressed[index] = true;
    }
    m_keyDown[index] = true;
}

void InputManager::SetKeyUp(Key key) {
    if (!IsValidKey(key)) {
        return;
    }
    const std::size_t index = KeyIndex(key);
    if (m_keyDown[index]) {
        m_keyReleased[index] = true;
    }
    m_keyDown[index] = false;
}

void InputManager::SetMouseDown(MouseButton button) {
    if (!IsValidMouseButton(button)) {
        return;
    }
    const std::size_t index = MouseButtonIndex(button);
    if (!m_mouseDown[index]) {
        m_mousePressed[index] = true;
    }
    m_mouseDown[index] = true;
}

void InputManager::SetMouseUp(MouseButton button) {
    if (!IsValidMouseButton(button)) {
        return;
    }
    const std::size_t index = MouseButtonIndex(button);
    if (m_mouseDown[index]) {
        m_mouseReleased[index] = true;
    }
    m_mouseDown[index] = false;
}

void InputManager::OnFocusLost() {
    for (std::size_t index = 0; index < kKeyCount; ++index) {
        if (m_keyDown[index]) {
            m_keyReleased[index] = true;
        }
        m_keyDown[index] = false;
        m_keyPressed[index] = false;
    }
    for (std::size_t index = 0; index < kMouseButtonCount; ++index) {
        if (m_mouseDown[index]) {
            m_mouseReleased[index] = true;
        }
        m_mouseDown[index] = false;
        m_mousePressed[index] = false;
    }
}

} // namespace Xora2D