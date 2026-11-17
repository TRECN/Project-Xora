#pragma once

#include "Xora2D/Input/InputCodes.h"

#include <glm/vec2.hpp>

namespace Xora2D {

class InputManager;

// Static facade for the active frame's input (set by Application).
class Input {
public:
    static void Attach(InputManager* manager);

    static bool IsKeyPressed(Key key);
    static bool IsKeyReleased(Key key);
    static bool IsKeyDown(Key key);
    static bool IsKeyUp(Key key);

    static bool IsMouseButtonPressed(MouseButton button);
    static bool IsMouseButtonReleased(MouseButton button);
    static bool IsMouseButtonDown(MouseButton button);
    static bool IsMouseButtonUp(MouseButton button);

    static glm::vec2 GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();
    static glm::vec2 GetMouseDelta();
    static glm::vec2 GetScrollDelta();

private:
    static InputManager* s_manager;
};

} // namespace Xora2D