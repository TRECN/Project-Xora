# Asset management (Milestone 8)

Xora2D centralizes project assets through `AssetHandle`, `AssetRegistry`, and `AssetManager`.

## Identity

- **AssetHandle** — stable 64-bit id (`StableFromRelativePath` for deterministic handles, persisted in the registry).
- **AssetType** — currently `Texture2D` only.
- **AssetMetadata** — handle, type, display name, relative path under `Resources/Assets/`, availability flag.

## Layout

```text
Resources/
  Assets/
    Textures/
    Registry/
      AssetRegistry.yaml
  Scenes/
```

Relative paths are normalized (forward slashes, no `..`).

## Workflows

### Import texture

```cpp
AssetManager manager(registry);
AssetResult result = manager.ImportTextureFile(
    ResolveResourcePath("Textures/Xora2D_Logo.png"), "Xora2D_Logo");
```

Re-importing the same file returns the **existing handle** without overwriting files silently.

### Load / cache

```cpp
manager.LoadTexture(handle);          // lazy GPU upload
auto texture = manager.GetLoadedTexture(handle);
manager.UnloadAsset(handle);
manager.PrepareShutdown();            // before GL context teardown
```

### Registry

```cpp
AssetRegistrySerializer::Load(registry, DefaultRegistryFilePath());
AssetRegistrySerializer::Save(registry, DefaultRegistryFilePath()); // atomic .tmp + rename
```

## Scenes

Scene format **v2** stores `SpriteRendererComponent.TextureAssetHandle`.  
**v1** `TexturePath` is still loaded; when an `AssetManager` is provided, legacy paths are registered automatically.

`Scene::SetAssetManager` + `ResolveSpriteTextures()` (called from `OnRender`) binds handles to GPU textures.

## Adding a future asset type

1. Extend `AssetType`.
2. Register/load paths in `AssetManager`.
3. Extend `AssetRegistrySerializer` and scene component fields as needed.

## Limitations (M8)

- Synchronous loading only.
- Textures only.
- No editor UI or async pipeline yet.