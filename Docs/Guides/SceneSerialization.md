# Scene serialization (Milestone 7)

Xora2D scenes can be saved and loaded as **YAML** files via `SceneSerializer`. Serialization stores **ECS component data** and stable **UUIDs** only — never GPU textures, SDL objects, or Box2D bodies.

## Quick usage

```cpp
#include "Xora2D/Serialization/SceneSerializer.h"
#include "Xora2D/Scene/Scene.h"

auto scene = Xora2D::Scene::Create();
scene->SetSceneName("MyLevel");

Xora2D::SceneSerializeResult saved = Xora2D::SceneSerializer::Serialize(
    *scene, "Resources/Scenes/MyLevel.scene.yaml");
if (!saved.Success) {
    // saved.Message explains the failure
}

Xora2D::SceneSerializeResult loaded = Xora2D::SceneSerializer::Deserialize(
    *scene, "Resources/Scenes/MyLevel.scene.yaml");
```

`Deserialize` validates the file, builds a staging scene, then **replaces** the destination scene only after success. A failed load leaves the original scene untouched.

Call `scene->PrepareForShutdown()` before tearing down an application; loaded scenes get a **fresh** `PhysicsWorld2D` and recreate bodies on the next `OnUpdate`.

## File layout

```yaml
Version: 1
Name: ExampleScene
Entities:
  - Entity: <hex-high>-<hex-low>   # matches UUID::ToString()
    TagComponent:
      Tag: Player
    TransformComponent:
      Translation: [x, y]         # pixels, Y-up
      RotationRadians: 0
      Scale: [1, 1]
    SpriteRendererComponent:
      Color: [r, g, b, a]
      Size: [w, h]
      TexturePath: Textures/Xora2D_Logo.png   # optional; relative to Resources/
      TilingFactor: 1
      Visible: true
    CameraComponent:
      Enabled: true
      Primary: true
      Zoom: 1
    RigidBody2DComponent:
      Type: Dynamic                 # Static | Dynamic | Kinematic
      # ... damping, flags, etc.
    BoxCollider2DComponent:
      Size: [w, h]
      # ...
    CircleCollider2DComponent:
      Radius: 25
      # ...
```

## Tilemaps (Milestone 19)

`TilemapComponent` stores its tileset handle, tile size, layers and run-length-encoded 32x32 chunks (ids and flags
only). Malformed tile data fails the load transactionally. Unknown tile ids and missing tilesets are reported in
`Warnings`. See [Tilemaps.md](Tilemaps.md).

## Prefab instances (Milestone 18)

An entity created from a prefab carries an optional `PrefabInstance` block: the prefab handle, the instance id, the
source entity, the revision and its overrides. Scenes without it load unchanged. A malformed block is an error. A
prefab missing from the registry is reported in `SceneSerializeResult::Warnings`, and the entity keeps its data. The
per-entity parse, emit and apply code is shared with `PrefabSerializer` through
`Engine/src/Serialization/EntitySerialization.h`. See [Prefabs.md](Prefabs.md).

## Format version

`SceneSerializer::kFormatVersion` is **2** (`TextureAssetHandle` on sprites). Version **1** files with `TexturePath` remain readable; provide an `AssetManager` on `Deserialize` to migrate paths to handles automatically.

## Supported components

| Component | Notes |
|-----------|--------|
| `IDComponent` | Stored as top-level `Entity` UUID (not a separate block) |
| `TagComponent` | Required `Tag` string |
| `TransformComponent` | Required `Translation`; optional `RotationRadians`, `Scale` |
| `SpriteRendererComponent` | Optional; `TexturePath` is project-relative under `Resources/` |
| `CameraComponent` | Optional |
| `RigidBody2DComponent` | Optional; no runtime velocity/forces |
| `BoxCollider2DComponent` | Optional |
| `CircleCollider2DComponent` | Optional |

## Asset paths

- Store paths like `Textures/MySprite.png` (no absolute paths).
- Resolve at load time with `ResolveResourcePath`.
- Missing files fail **before** the destination scene is modified.

## Physics

Saved data is **editor-style configuration**: transforms and collider settings, not Box2D state. After load, `Scene::OnUpdate` runs the normal `PhysicsWorld2D` sync and creates bodies once per entity.

## Adding a new component serializer

1. Add read/write handling in `Engine/src/Serialization/SceneSerializer.cpp` (`ParseEntity` + `EmitEntity`).
2. Bump `kFormatVersion` if the on-disk layout changes.
3. Add round-trip and validation tests in `Tests/src/SerializationTests.cpp`.

## Known limitations

- No hierarchy/parent transforms (matches Milestone 5).
- Texture assignment on save uses `SpriteRendererComponent::TexturePath` (not inferred from GPU objects).
- No joint, material, or layer serialization yet.
- Circle + box colliders on one entity are supported as in Milestone 6.

See also: [`Resources/Scenes/ExampleScene.scene.yaml`](../../Resources/Scenes/ExampleScene.scene.yaml)