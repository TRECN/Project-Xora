#pragma once

#include "Xora2D/Physics/PhysicsTypes.h"

#include <glm/vec2.hpp>

namespace Xora2D {

// ECS components are plain data; PhysicsWorld2D owns runtime Box2D bodies.
struct RigidBody2DComponent {
    RigidBodyType2D Type = RigidBodyType2D::Dynamic;
    bool FixedRotation = false;
    float GravityScale = 1.0f;
    float LinearDamping = 0.0f;
    float AngularDamping = 0.05f;
    bool AllowSleep = true;
    bool Bullet = false;
    bool Enabled = true;

    // When true, TransformComponent drives the body before each step (Static/Kinematic).
    bool DriveTransformFromECS = false;
};

struct BoxCollider2DComponent {
    glm::vec2 Offset{0.0f};
    glm::vec2 Size{50.0f, 50.0f};
    float Density = 1.0f;
    float Friction = 0.4f;
    float Restitution = 0.0f;
    float RestitutionThreshold = 2.0f;
    bool IsSensor = false;
};

struct CircleCollider2DComponent {
    glm::vec2 Offset{0.0f};
    float Radius = 25.0f;
    float Density = 1.0f;
    float Friction = 0.4f;
    float Restitution = 0.0f;
    float RestitutionThreshold = 2.0f;
    bool IsSensor = false;
};

} // namespace Xora2D