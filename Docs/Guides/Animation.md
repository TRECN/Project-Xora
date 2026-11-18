# Xora2D Animation System (Milestone 12)

## Architecture

- **Animation clips** are versioned YAML assets (`*.anim.yaml`) loaded through `AssetManager`.
- **Sprite-sheet UVs** are baked at clip load time from layout metadata (or from explicit per-frame UVs).
- **`AnimationComponent`** on entities tracks playback state; **`SceneAnimationSystem`** advances clips in `Scene::OnUpdate`.
- **`Renderer2D`** draws animated sprites with sub-rect UVs using the same textured batch as static sprites.
- **Events** use `Scene::SetAnimationEventCallback` (same pattern as physics collision callbacks).

## Creating a clip asset

```yaml
Version: 1
Name: Walk
TextureRelativePath: Textures/DemoSpriteSheet.png
Loop: true
DefaultFrameDuration: 0.12
SpriteSheet:
  Columns: 4
  Rows: 1
  FrameWidth: 32
  FrameHeight: 32
  FrameCount: 4
```

Register with `AssetManager::RegisterAnimationClipAtRelativePath`.

## ECS usage (C++)

```cpp
auto& animation = entity.AddComponent<AnimationComponent>();
animation.ClipAssets = {walkHandle, idleHandle};
animation.DefaultClip = walkHandle;
animation.Autoplay = true;
entity.AddComponent<SpriteRendererComponent>().Size = {64.0f, 64.0f};
```

Runtime control: `AnimationPlayByName(entity, assets, "Walk")`, `AnimationPause`, `AnimationResume`, `AnimationStop`.

## Python (optional)

```python
self.entity.animation_play("Walk")
self.entity.animation_pause()
print(self.entity.animation_current_frame())
```

## Sandbox controls

- **5** — Walk (looping sheet)
- **6** — Idle (looping)
- **7** — Cheer (one-shot)
- **8** — Pause / resume toggle

Static and animated sprites render in the same batch when sharing a texture atlas.