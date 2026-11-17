#pragma once

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Physics/PhysicsTypes.h"

namespace Xora2D {

// Deferred physics contact notification delivered after the simulation step.
struct PhysicsCollisionEvent {
    PhysicsCollisionKind Kind = PhysicsCollisionKind::Collision;
    PhysicsCollisionPhase Phase = PhysicsCollisionPhase::Begin;
    UUID EntityA;
    UUID EntityB;
};

} // namespace Xora2D