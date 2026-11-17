#pragma once

#include <cstdint>

namespace Xora2D {

// Physical keys. Values are engine identifiers. The platform layer maps the
// operating-system scancode onto this enum, so public headers never include SDL.
enum class Key : std::uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit1, Digit2, Digit3, Digit4, Digit5,
    Digit6, Digit7, Digit8, Digit9, Digit0,
    Return, Escape, Backspace, Tab, Space,
    Left, Right, Up, Down,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift, LeftControl, LeftAlt,
    RightShift, RightControl, RightAlt,
};

} // namespace Xora2D
