#include "Xora2D/Input/InputCodes.h"

#include <cctype>

namespace Xora2D {

bool IsValidKey(Key key) {
    const auto value = static_cast<std::uint16_t>(key);
    return value > static_cast<std::uint16_t>(Key::Unknown) &&
           value < static_cast<std::uint16_t>(Key::Count);
}

bool IsValidMouseButton(MouseButton button) {
    const auto value = static_cast<std::uint8_t>(button);
    return value > static_cast<std::uint8_t>(MouseButton::Unknown) &&
           value < static_cast<std::uint8_t>(MouseButton::Count);
}

std::optional<Key> KeyFromName(const std::string& name) {
    if (name.size() == 1) {
        const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (letter >= 'A' && letter <= 'Z') {
            return static_cast<Key>(static_cast<std::uint16_t>(Key::A) + (letter - 'A'));
        }
        if (letter == '0') {
            return Key::Digit0;
        }
        if (letter >= '1' && letter <= '9') {
            return static_cast<Key>(static_cast<std::uint16_t>(Key::Digit1) + (letter - '1'));
        }
    }

    static const std::pair<const char*, Key> kNamedKeys[] = {
        {"Return", Key::Return},
        {"Enter", Key::Return},
        {"Escape", Key::Escape},
        {"Backspace", Key::Backspace},
        {"Tab", Key::Tab},
        {"Space", Key::Space},
        {"Left", Key::Left},
        {"Right", Key::Right},
        {"Up", Key::Up},
        {"Down", Key::Down},
        {"F1", Key::F1},
        {"F2", Key::F2},
        {"F3", Key::F3},
        {"F4", Key::F4},
        {"F5", Key::F5},
        {"F6", Key::F6},
        {"F7", Key::F7},
        {"F8", Key::F8},
        {"F9", Key::F9},
        {"F10", Key::F10},
        {"F11", Key::F11},
        {"F12", Key::F12},
        {"LeftShift", Key::LeftShift},
        {"LeftControl", Key::LeftControl},
        {"LeftAlt", Key::LeftAlt},
        {"RightShift", Key::RightShift},
        {"RightControl", Key::RightControl},
        {"RightAlt", Key::RightAlt},
        {"Insert", Key::Insert},
        {"Delete", Key::Delete},
        {"Home", Key::Home},
        {"End", Key::End},
        {"PageUp", Key::PageUp},
        {"PageDown", Key::PageDown},
        {"LeftSuper", Key::LeftSuper},
        {"RightSuper", Key::RightSuper},
        {"Semicolon", Key::Semicolon},
        {"Apostrophe", Key::Apostrophe},
        {"Comma", Key::Comma},
        {"Period", Key::Period},
        {"Slash", Key::Slash},
        {"Backslash", Key::Backslash},
        {"Minus", Key::Minus},
        {"Equals", Key::Equals},
        {"Grave", Key::Grave},
        {"LeftBracket", Key::LeftBracket},
        {"RightBracket", Key::RightBracket},
        {"Keypad0", Key::Keypad0},
        {"Keypad1", Key::Keypad1},
        {"Keypad2", Key::Keypad2},
        {"Keypad3", Key::Keypad3},
        {"Keypad4", Key::Keypad4},
        {"Keypad5", Key::Keypad5},
        {"Keypad6", Key::Keypad6},
        {"Keypad7", Key::Keypad7},
        {"Keypad8", Key::Keypad8},
        {"Keypad9", Key::Keypad9},
        {"KeypadEnter", Key::KeypadEnter},
        {"Digit0", Key::Digit0},
        {"Digit1", Key::Digit1},
        {"Digit2", Key::Digit2},
        {"Digit3", Key::Digit3},
        {"Digit4", Key::Digit4},
        {"Digit5", Key::Digit5},
        {"Digit6", Key::Digit6},
        {"Digit7", Key::Digit7},
        {"Digit8", Key::Digit8},
        {"Digit9", Key::Digit9},
    };

    for (const auto& [text, key] : kNamedKeys) {
        if (text != nullptr && name == text) {
            return key;
        }
    }
    return std::nullopt;
}

std::string KeyToName(Key key) {
    if (!IsValidKey(key)) {
        return "Unknown";
    }
    if (key >= Key::A && key <= Key::Z) {
        return std::string(1, static_cast<char>('A' + (static_cast<std::uint16_t>(key) -
                                                        static_cast<std::uint16_t>(Key::A))));
    }
    if (key == Key::Digit0) {
        return "0";
    }
    if (key >= Key::Digit1 && key <= Key::Digit9) {
        return std::string(
            1, static_cast<char>('0' + (static_cast<std::uint16_t>(key) - static_cast<std::uint16_t>(Key::Digit1) + 1)));
    }

    switch (key) {
    case Key::Return:
        return "Return";
    case Key::Escape:
        return "Escape";
    case Key::Backspace:
        return "Backspace";
    case Key::Tab:
        return "Tab";
    case Key::Space:
        return "Space";
    case Key::Left:
        return "Left";
    case Key::Right:
        return "Right";
    case Key::Up:
        return "Up";
    case Key::Down:
        return "Down";
    case Key::F1:
        return "F1";
    case Key::F2:
        return "F2";
    case Key::F3:
        return "F3";
    case Key::F4:
        return "F4";
    case Key::F5:
        return "F5";
    case Key::F6:
        return "F6";
    case Key::F7:
        return "F7";
    case Key::F8:
        return "F8";
    case Key::F9:
        return "F9";
    case Key::F10:
        return "F10";
    case Key::F11:
        return "F11";
    case Key::F12:
        return "F12";
    case Key::LeftShift:
        return "LeftShift";
    case Key::LeftControl:
        return "LeftControl";
    case Key::LeftAlt:
        return "LeftAlt";
    case Key::RightShift:
        return "RightShift";
    case Key::RightControl:
        return "RightControl";
    case Key::RightAlt:
        return "RightAlt";
    case Key::Insert:
        return "Insert";
    case Key::Delete:
        return "Delete";
    case Key::Home:
        return "Home";
    case Key::End:
        return "End";
    case Key::PageUp:
        return "PageUp";
    case Key::PageDown:
        return "PageDown";
    case Key::LeftSuper:
        return "LeftSuper";
    case Key::RightSuper:
        return "RightSuper";
    case Key::Semicolon:
        return "Semicolon";
    case Key::Apostrophe:
        return "Apostrophe";
    case Key::Comma:
        return "Comma";
    case Key::Period:
        return "Period";
    case Key::Slash:
        return "Slash";
    case Key::Backslash:
        return "Backslash";
    case Key::Minus:
        return "Minus";
    case Key::Equals:
        return "Equals";
    case Key::Grave:
        return "Grave";
    case Key::LeftBracket:
        return "LeftBracket";
    case Key::RightBracket:
        return "RightBracket";
    case Key::Keypad0:
        return "Keypad0";
    case Key::Keypad1:
        return "Keypad1";
    case Key::Keypad2:
        return "Keypad2";
    case Key::Keypad3:
        return "Keypad3";
    case Key::Keypad4:
        return "Keypad4";
    case Key::Keypad5:
        return "Keypad5";
    case Key::Keypad6:
        return "Keypad6";
    case Key::Keypad7:
        return "Keypad7";
    case Key::Keypad8:
        return "Keypad8";
    case Key::Keypad9:
        return "Keypad9";
    case Key::KeypadEnter:
        return "KeypadEnter";
    default:
        return "Unknown";
    }
}

std::optional<MouseButton> MouseButtonFromName(const std::string& name) {
    if (name == "Left") {
        return MouseButton::Left;
    }
    if (name == "Middle") {
        return MouseButton::Middle;
    }
    if (name == "Right") {
        return MouseButton::Right;
    }
    if (name == "X1") {
        return MouseButton::X1;
    }
    if (name == "X2") {
        return MouseButton::X2;
    }
    return std::nullopt;
}

std::string MouseButtonToName(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return "Left";
    case MouseButton::Middle:
        return "Middle";
    case MouseButton::Right:
        return "Right";
    case MouseButton::X1:
        return "X1";
    case MouseButton::X2:
        return "X2";
    default:
        return "Unknown";
    }
}

} // namespace Xora2D