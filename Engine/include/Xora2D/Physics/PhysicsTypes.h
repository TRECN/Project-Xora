#pragma once

#include <cstdint>

namespace Xora2D {

enum class RigidBodyType2D : std::uint8_t {
    Static = 0,
    Dynamic,
    Kinematic,
};

enum class PhysicsCollisionKind : std::uint8_t {
    Collision = 0,
    Sensor,
};

enum class PhysicsCollisionPhase : std::uint8_t {
    Begin = 0,
    End,
};

} // namespace Xora2D