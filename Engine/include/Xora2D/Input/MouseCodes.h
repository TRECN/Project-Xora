#pragma once

#include <cstdint>

namespace Xora2D {

enum class MouseButton : std::uint8_t {
    Unknown = 0,
    Left,
    Middle,
    Right,
    X1,
    X2,
    Count
};

} // namespace Xora2D
