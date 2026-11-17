#pragma once

#include "Xora2D/Input/InputCodes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2D {

class InputManager;

struct InputBinding {
    enum class Type : std::uint8_t {
        Key,
        MouseButton,
    };

    Type BindingType = Type::Key;
    Key KeyCode = Key::Unknown;
    MouseButton MouseButtonCode = MouseButton::Unknown;
};

class InputMappingSerializer;

class InputMapping {
    friend class InputMappingSerializer;

public:
    bool BindKey(const std::string& action, Key key);
    bool BindMouseButton(const std::string& action, MouseButton button);
    bool RemoveBinding(const std::string& action, const InputBinding& binding);
    void ClearAction(const std::string& action);
    void Clear();

    [[nodiscard]] bool HasAction(const std::string& action) const;

    bool IsActionPressed(const InputManager& input, const std::string& action) const;
    bool IsActionReleased(const InputManager& input, const std::string& action) const;
    bool IsActionDown(const InputManager& input, const std::string& action) const;
    bool IsActionUp(const InputManager& input, const std::string& action) const;

    [[nodiscard]] const std::vector<InputBinding>* GetBindings(const std::string& action) const;

private:
    std::unordered_map<std::string, std::vector<InputBinding>> m_actions;
};

} // namespace Xora2D