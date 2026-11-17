#include "Xora2D/Input/InputMapping.h"

#include "Xora2D/Input/InputManager.h"

#include <algorithm>

namespace Xora2D {
namespace {

bool BindingEquals(const InputBinding& a, const InputBinding& b) {
    if (a.BindingType != b.BindingType) {
        return false;
    }
    if (a.BindingType == InputBinding::Type::Key) {
        return a.KeyCode == b.KeyCode;
    }
    return a.MouseButtonCode == b.MouseButtonCode;
}

bool QueryBindingPressed(const InputManager& input, const InputBinding& binding) {
    if (binding.BindingType == InputBinding::Type::Key) {
        return input.IsKeyPressed(binding.KeyCode);
    }
    return input.IsMouseButtonPressed(binding.MouseButtonCode);
}

bool QueryBindingReleased(const InputManager& input, const InputBinding& binding) {
    if (binding.BindingType == InputBinding::Type::Key) {
        return input.IsKeyReleased(binding.KeyCode);
    }
    return input.IsMouseButtonReleased(binding.MouseButtonCode);
}

bool QueryBindingDown(const InputManager& input, const InputBinding& binding) {
    if (binding.BindingType == InputBinding::Type::Key) {
        return input.IsKeyDown(binding.KeyCode);
    }
    return input.IsMouseButtonDown(binding.MouseButtonCode);
}

bool QueryBindingUp(const InputManager& input, const InputBinding& binding) {
    if (binding.BindingType == InputBinding::Type::Key) {
        return input.IsKeyUp(binding.KeyCode);
    }
    return input.IsMouseButtonUp(binding.MouseButtonCode);
}

} // namespace

bool InputMapping::BindKey(const std::string& action, Key key) {
    if (!IsValidKey(key) || action.empty()) {
        return false;
    }
    InputBinding binding{};
    binding.BindingType = InputBinding::Type::Key;
    binding.KeyCode = key;
    auto& list = m_actions[action];
    if (std::any_of(list.begin(), list.end(), [&](const InputBinding& existing) {
            return BindingEquals(existing, binding);
        })) {
        return true;
    }
    list.push_back(binding);
    return true;
}

bool InputMapping::BindMouseButton(const std::string& action, MouseButton button) {
    if (!IsValidMouseButton(button) || action.empty()) {
        return false;
    }
    InputBinding binding{};
    binding.BindingType = InputBinding::Type::MouseButton;
    binding.MouseButtonCode = button;
    auto& list = m_actions[action];
    if (std::any_of(list.begin(), list.end(), [&](const InputBinding& existing) {
            return BindingEquals(existing, binding);
        })) {
        return true;
    }
    list.push_back(binding);
    return true;
}

bool InputMapping::RemoveBinding(const std::string& action, const InputBinding& binding) {
    const auto actionIt = m_actions.find(action);
    if (actionIt == m_actions.end()) {
        return false;
    }
    auto& list = actionIt->second;
    const auto bindingIt =
        std::remove_if(list.begin(), list.end(), [&](const InputBinding& existing) {
            return BindingEquals(existing, binding);
        });
    if (bindingIt == list.end()) {
        return false;
    }
    list.erase(bindingIt, list.end());
    if (list.empty()) {
        m_actions.erase(actionIt);
    }
    return true;
}

void InputMapping::ClearAction(const std::string& action) {
    m_actions.erase(action);
}

void InputMapping::Clear() {
    m_actions.clear();
}

bool InputMapping::HasAction(const std::string& action) const {
    return m_actions.find(action) != m_actions.end();
}

bool InputMapping::IsActionPressed(const InputManager& input, const std::string& action) const {
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const InputBinding& binding) {
        return QueryBindingPressed(input, binding);
    });
}

bool InputMapping::IsActionReleased(const InputManager& input, const std::string& action) const {
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const InputBinding& binding) {
        return QueryBindingReleased(input, binding);
    });
}

bool InputMapping::IsActionDown(const InputManager& input, const std::string& action) const {
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const InputBinding& binding) {
        return QueryBindingDown(input, binding);
    });
}

bool InputMapping::IsActionUp(const InputManager& input, const std::string& action) const {
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        return true;
    }
    return std::all_of(it->second.begin(), it->second.end(), [&](const InputBinding& binding) {
        return QueryBindingUp(input, binding);
    });
}

const std::vector<InputBinding>* InputMapping::GetBindings(const std::string& action) const {
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace Xora2D