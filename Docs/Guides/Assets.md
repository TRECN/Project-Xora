# Asset management (Milestone 8)

Xora2D centralizes project assets through `AssetHandle`, `AssetRegistry`, and `AssetManager`.

> **Projects (Milestone 17):** paths below written as `Resources/Assets/...` resolve against the *content root*
> (`Xora2D::GetContentRoot()`). While a project is open that is the project folder, so assets live in
> `<Project>/Assets/` and the registry in `<Project>/Assets/Registry/AssetRegistry.yaml`. Without a project (Sandbox,
> engine samples) it is the engine's `Resources/` folder. See [Projects.md](Projects.md).

## Identity

- **AssetHandle** — stable 64-bit id (`StableFromRelativePath` for deterministic handles, persisted in the registry). New assets get their handle from `AssetRegistry::AllocateHandleForPath`, which falls back to a deterministic alternate when a renamed/moved asset still owns the path-derived handle. Rename/move (`AssetRegistry::RelocateAssetPath`) preserves handles — see [AssetBrowser.md](AssetBrowser.md).
- Supported audio extensions: `.wav` and `.mp3` (decoded by miniaudio).
- **AssetType** — `Texture2D`, `Audio`, `PythonScript`, `AnimationClip`, `Prefab` (`.prefab.yaml`, see
  [Prefabs.md](Prefabs.md)), `Tileset` (`.tileset.yaml`) and `Tilemap` (`.tilemap.yaml`) (see
  [Tilemaps.md](Tilemaps.md)). `UnloadAll()` drops every cache, including animation clips and prefabs.
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