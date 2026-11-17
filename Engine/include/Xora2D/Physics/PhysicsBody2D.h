#pragma once

// Physics bodies are created and driven through ECS components (RigidBody2DComponent,
// BoxCollider2DComponent, CircleCollider2DComponent) and PhysicsWorld2D on Scene.
// Box2D types remain private to the engine implementation.

namespace Xora2D {

class PhysicsWorld2D;

} // namespace Xora2D