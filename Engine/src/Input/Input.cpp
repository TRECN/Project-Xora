#include "Xora2D/Input/Input.h"

#include "Xora2D/Input/InputManager.h"

namespace Xora2D {

InputManager* Input::s_manager = nullptr;

void Input::Attach(InputManager* manager) {
    s_manager = manager;
}

bool Input::IsKeyPressed(Key key) {
    return s_manager != nullptr && s_manager->IsKeyPressed(key);
}

bool Input::IsKeyReleased(Key key) {
    return s_manager != nullptr && s_manager->IsKeyReleased(key);
}

bool Input::IsKeyDown(Key key) {
    return s_manager != nullptr && s_manager->IsKeyDown(key);
}

bool Input::IsKeyUp(Key key) {
    return s_manager == nullptr || s_manager->IsKeyUp(key);
}

bool Input::IsMouseButtonPressed(MouseButton button) {
    return s_manager != nullptr && s_manager->IsMouseButtonPressed(button);
}

bool Input::IsMouseButtonReleased(MouseButton button) {
    return s_manager != nullptr && s_manager->IsMouseButtonReleased(button);
}

bool Input::IsMouseButtonDown(MouseButton button) {
    return s_manager != nullptr && s_manager->IsMouseButtonDown(button);
}

bool Input::IsMouseButtonUp(MouseButton button) {
    return s_manager == nullptr || s_manager->IsMouseButtonUp(button);
}

glm::vec2 Input::GetMousePosition() {
    return s_manager != nullptr ? s_manager->GetMousePosition() : glm::vec2(0.0f);
}

float Input::GetMouseX() {
    return s_manager != nullptr ? s_manager->GetMouseX() : 0.0f;
}

float Input::GetMouseY() {
    return s_manager != nullptr ? s_manager->GetMouseY() : 0.0f;
}

glm::vec2 Input::GetMouseDelta() {
    return s_manager != nullptr ? s_manager->GetMouseDelta() : glm::vec2(0.0f);
}

glm::vec2 Input::GetScrollDelta() {
    return s_manager != nullptr ? s_manager->GetScrollDelta() : glm::vec2(0.0f);
}

} // namespace Xora2D