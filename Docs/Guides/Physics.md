# Xora2D physics (Milestone 6)

Xora2D integrates **Box2D 2.4.1** behind the public `PhysicsWorld2D` and ECS components API. Box2D types never appear in public headers.

## Coordinates

| Space | Convention |
|-------|------------|
| Rendering / `TransformComponent` | Pixel space, **Y-up**, aligned with `Renderer2D` and the orthographic camera |
| Physics simulation | **Meters**, same Y-up orientation |

Conversion uses `PhysicsWorld2D::Settings::PixelsPerMeter` (default **50** pixels per meter). Collider sizes and offsets on components are specified in **pixels**; the physics module converts internally.

Forces, impulses, and velocities use **SI-style physics units** (newtons, N·s, m/s).

## Scene integration

Each `Scene` owns a `PhysicsWorld2D`. Call `scene->OnUpdate(deltaSeconds)` from your game loop (the Sandbox does this via `Application::OnUpdate`).

Lifecycle:

1. Create entities and add `RigidBody2DComponent` plus at least one collider.
2. `OnUpdate` syncs ECS transforms into physics, steps the fixed-timestep world, then writes **dynamic** body transforms back into `TransformComponent` (scale is not overwritten).
3. `DestroyEntity` removes the associated Box2D body.
4. Destroying the scene tears down the entire physics world.

## Components

### `RigidBody2DComponent`

- `RigidBodyType2D`: `Static`, `Dynamic`, or `Kinematic`
- `FixedRotation`, `GravityScale`, damping, `AllowSleep`, `Bullet`, `Enabled`
- `DriveTransformFromECS`: when true (or for static/kinematic bodies), `TransformComponent` drives the body before each step

### `BoxCollider2DComponent` / `CircleCollider2DComponent`

Size/radius and offset are in **pixels**. Configure `Density`, `Friction`, `Restitution`, `RestitutionThreshold` (pixels), and `IsSensor`.

An entity may have **one** box collider and **one** circle collider; both attach as fixtures on the same body if added before the first simulation step (or while the body has no fixtures yet).

## Fixed timestep

Default fixed step: **1/60 s**. The physics world accumulates real `deltaSeconds`, runs up to `MaxSubSteps` (default **8**) per frame, and clamps the accumulator to avoid spiral-of-death catch-up.

Pause simulation: `scene->GetPhysicsWorld().SetSimulationEnabled(false)`.

Call `scene->PrepareForShutdown()` when the application is quitting so physics stops stepping and gameplay collision callbacks are not dispatched during teardown. Debug Sandbox builds may enable `PhysicsWorld2D::Settings::EnableContactDiagnostics` for per-fixture contact tracing.

## Collision events

```cpp
scene->SetPhysicsEventCallback([](const Xora2D::PhysicsCollisionEvent& event) {
    if (event.Phase == Xora2D::PhysicsCollisionPhase::Begin &&
        event.Kind == Xora2D::PhysicsCollisionKind::Collision) {
        // event.EntityA / event.EntityB are UUIDs
    }
});
```

Events are delivered **after** each physics sub-step at **entity pair** granularity: multiple fixture contacts between the same two entities count as one logical touch until all fixture contacts end. Callbacks must not destroy physics bodies or mutate the Box2D world unsafely—destroy entities after the update if needed.

## Example (public API only)

```cpp
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Scene/Components.h"

auto scene = Xora2D::Scene::Create();

Xora2D::Entity ground = scene->CreateEntity("Ground");
ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {800.0f, 40.0f};
ground.GetComponent<Xora2D::TransformComponent>().Translation = {400.0f, 20.0f};

Xora2D::Entity crate = scene->CreateEntity("Crate");
crate.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
crate.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {48.0f, 48.0f};
crate.GetComponent<Xora2D::TransformComponent>().Translation = {400.0f, 400.0f};

for (int i = 0; i < 120; ++i) {
    scene->OnUpdate(1.0f / 60.0f);
}

scene->GetPhysicsWorld().ApplyLinearImpulse(crate, {1.0f, 0.0f});
```

## Applying forces

Use methods on `PhysicsWorld2D` via `scene->GetPhysicsWorld()`:

- `ApplyForce`, `ApplyForceAtPoint` (newtons)
- `ApplyLinearImpulse`, `ApplyAngularImpulse`
- `SetLinearVelocity`, `GetLinearVelocity`, `SetAngularVelocity`, `ApplyTorque`
- `SetBodyType`, `SetBodyEnabled`

## Current limitations

- No joints, character controller, or tilemap collision
- One box and one circle collider component per entity (EnTT single-storage); not a list of arbitrary colliders
- Adding colliders after fixtures were built requires destroying/recreating the body (or re-adding components before the first step)
- No parent-child transform hierarchy; physics sync is root-level only
- No serialization of physics state

## Suggested commit message

```
Add Box2D physics module with ECS components, scene integration, and tests.
```