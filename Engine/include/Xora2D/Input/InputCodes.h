#pragma once

#include "Xora2D/Input/KeyCodes.h"
#include "Xora2D/Input/MouseCodes.h"

#include <optional>
#include <string>

namespace Xora2D {

using KeyCode = Key;

constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

constexpr std::size_t KeyIndex(Key key) {
    return static_cast<std::size_t>(key);
}

constexpr std::size_t MouseButtonIndex(MouseButton button) {
    return static_cast<std::size_t>(button);
}

bool IsValidKey(Key key);
bool IsValidMouseButton(MouseButton button);

std::optional<Key> KeyFromName(const std::string& name);
std::string KeyToName(Key key);

std::optional<MouseButton> MouseButtonFromName(const std::string& name);
std::string MouseButtonToName(MouseButton button);

} // namespace Xora2D