#pragma once

#include "Xora2D/Input/InputCodes.h"

#include <glm/vec2.hpp>

namespace Xora2D {

class Event;

// Per-frame input state updated from engine events (not SDL).
class InputManager {
public:
    void BeginFrame();
    void OnEvent(Event& event);
    void ClearState();
    void PrepareShutdown();

    bool IsKeyPressed(Key key) const;
    bool IsKeyReleased(Key key) const;
    bool IsKeyDown(Key key) const;
    bool IsKeyUp(Key key) const;

    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonReleased(MouseButton button) const;
    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonUp(MouseButton button) const;

    glm::vec2 GetMousePosition() const { return m_mousePosition; }
    float GetMouseX() const { return m_mousePosition.x; }
    float GetMouseY() const { return m_mousePosition.y; }
    glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
    glm::vec2 GetScrollDelta() const { return m_scrollDelta; }

private:
    void SetKeyDown(Key key, bool repeat);
    void SetKeyUp(Key key);
    void SetMouseDown(MouseButton button);
    void SetMouseUp(MouseButton button);
    void OnFocusLost();

    bool m_keyDown[kKeyCount]{};
    bool m_keyPressed[kKeyCount]{};
    bool m_keyReleased[kKeyCount]{};

    bool m_mouseDown[kMouseButtonCount]{};
    bool m_mousePressed[kMouseButtonCount]{};
    bool m_mouseReleased[kMouseButtonCount]{};

    glm::vec2 m_mousePosition{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    glm::vec2 m_scrollDelta{0.0f};
    bool m_enabled = true;
};

} // namespace Xora2D