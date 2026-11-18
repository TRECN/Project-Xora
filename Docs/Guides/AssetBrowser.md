# Visual Asset Browser (Milestone 15)

The **Content** panel in `Xora2D-Editor` is a visual browser for the project `Resources/` tree. It imports, previews, renames, moves and deletes assets. All of this goes through the existing `AssetManager` / `AssetRegistry`; there is no second registry and no editor-only asset format.

## Architecture

Editor-only code lives in `Editor/include/Xora2DEditor/Content/` and `Editor/src/Content/`. The engine runtime has no knowledge of thumbnails, selection or browser state.

| Module | Responsibility |
|---|---|
| `ContentBrowserPaths` | Resource-relative path rules: normalization, traversal/symlink rejection, protected folders, file classification, single-directory listing (with "ghost" entries for missing assets) |
| `ContentBrowserQuery` | Pure filter / search / sort over a listing |
| `ContentBrowserState` | Navigation history (back/forward/up), view mode, and the selection, keyed by `AssetHandle` |
| `ContentBrowserDirectoryCache` | Per-folder listing cache; rescans only on invalidation or when a visible folder's listing is older than 2 s |
| `ContentBrowserImport` | Classification, content validation, staged copy (`StageImport`, worker-safe) and registration (`CommitImport`, main thread); `ContentImportQueue` runs staging on a worker thread |
| `ContentBrowserOperations` | Create folder, rename, move, delete, reference scanning, registry/disk sync, register-in-place |
| `AssetThumbnailCache` | Async decode and downscale on a worker thread; GPU upload of a few thumbnails per frame on the main thread |
| `AssetPreview` | `GatherAssetDetails` (type-specific metadata, gathered asynchronously), `AudioPreviewPlayer` |
| `EditorAssetAssignments` | Validated asset → component assignment and the drag-and-drop payload |
| `EditorFileDialog` | Native SDL3 multi-file open dialog (asynchronous) |
| `Panels/ContentBrowserPanel`, `AssetInspectorUI` | ImGui UI |

`EditorContext` owns these services and exposes `SelectContentItem`, `NotifyContentChanged`, `PersistAssetRegistry` and `ShutdownContentServices`. The background workers (import, thumbnails, details) are stopped in `EditorApplication` before the audio engine and GL context shut down.

## Project asset folder structure

```
Resources/
  Assets/                  registry asset root (AssetMetadata::RelativePath is relative to this)
    Textures/   Audio/   Scripts/   Animations/
    Registry/AssetRegistry.yaml    (protected)
  Scenes/                  *.scene.yaml (referenced by path, not registered)
  Shaders/  Input/         engine folders (protected)
```

Browser paths are relative to `Resources/` (for example `Assets/Textures/Hero.png`). Registry paths drop the `Assets/` prefix (`Textures/Hero.png`).

## Supported asset types

| Type | Extensions | Registered as | Validation on import |
|---|---|---|---|
| Image | `.png`, `.jpg`, `.jpeg` | `Texture2D` | full stb_image decode, 1–16384 px, RGB/RGBA |
| Audio | `.wav`, `.mp3` | `Audio` | full decode with the engine decoder (miniaudio) |
| Python script | `.py` (scripting builds only) | `PythonScript` | text file under 4 MB, no NUL bytes; **never executed** |
| Scene | `.scene.yaml` | not registered (opened by path) | YAML map with an `Entities` sequence |
| Animation clip | `.anim.yaml` | `AnimationClip` | YAML, `Version: 1`, `Frames` or `SpriteSheet`, then a full `AnimationClipAsset` load |

`.ogg` is rejected with an explanation, because the engine's audio decoder has no Vorbis support. The editor also lists `.bmp`/`.tga` (which the engine renderer can load), but it doesn't import them.

## Import workflow

1. Click **Import...** (native multi-file dialog), or drop files from the OS onto the editor window.
2. On a worker thread, each file is classified and **validated before anything is copied**. Files are processed in dependency order: textures before animation clips.
3. Destination: the type's default folder (`Assets/Textures`, `Assets/Audio`, `Assets/Scripts`, `Assets/Animations`, `Scenes`). If the browser is currently inside that folder's subtree, such as `Assets/Textures/Characters`, the file goes there instead.
4. Copy into a hidden temp file (`.xora_import_*`), verify the size, then rename it into place. An interrupted copy never leaves a partial asset.
5. On the main thread, the file is registered via `AssetManager::Register*AtRelativePath`. If registration fails, the copied file is removed again.
6. The registry is saved, the affected folders refresh, and the last imported item is selected. A results dialog lists any failures and warnings.

Duplicates:
- **Identical bytes** already at the destination: nothing is copied and the existing handle is reused ("already in project").
- **Same name, different content:** imported as `Name_1.ext` with its own handle.
- **A file already inside the project:** registered in place, not copied.
- **Files added outside the editor:** show an orange dot or a "Not imported" status. Use **Import (register in place)** from the context menu or the Inspector.

## Stable asset handles

Handles are 64-bit values persisted in `AssetRegistry.yaml` and in scenes. A new asset gets `AssetHandle::StableFromRelativePath(path)`.
- **Rename/move:** the handle is preserved and only the registry path changes, so scenes keep working.
- **New file at an old path:** if a file is later created at a renamed asset's old path, `AssetRegistry::AllocateHandleForPath` derives a different, deterministic handle. The moved asset's handle is never taken over.
- **Selection:** the browser selection, drag-and-drop payloads and Inspector all identify registered assets by handle, not by path or pointer.

## Previews and thumbnails

- **Images:** decoded on a worker thread (`GenerateThumbnail`), box-filtered to ≤128 px with the aspect ratio preserved, then uploaded via `Texture2D::CreateFromRGBA8`. Full-resolution pixels are never kept. The cache is keyed by path and invalidated when the file's modification time changes. LRU capacity is 512. Corrupt images show an error instead of a thumbnail.
- **Audio:** duration, format, sample rate and channels. The **Play / Pause / Resume / Stop** controls use the editor's own playback id on the shared `AudioEngine` and only stop that id, so scene audio is untouched. Preview stops on selection change, delete and shutdown.
- **Scripts:** line count, scripting availability, and a read-only source preview (first 200 lines / 16 KB).
- **Scenes:** scene name and entity count. **Open Scene** asks for confirmation; a failed load leaves the current scene intact.
- **Animation clips:** frame count, duration, speed, loop and texture, plus a live preview of the frames from the clip's texture.

Details are gathered off the main thread (`AssetDetailsLoader`) and cached for the current selection.

## Inspector integration

Selecting a file shows it in the **Inspector**: name, handle (with a copy button), type, path, size, modified time, import status, and type-specific data. Selecting an entity in the Hierarchy switches the Inspector back to that entity.

Assigning assets:
- **Assign to '<entity>'** in the asset Inspector or the context menu adds or updates the matching component:
  - texture → Sprite Renderer
  - audio → Audio Source
  - script → Python Script (scripting builds only)
  - animation clip → Animation (default clip, plus the clip list)
- **Drag and drop** a file onto the matching Inspector field. The payload carries only the handle, type and path. The drop target re-checks the handle against the registry (it must be registered, available and of the right type) and rejects anything else.
- Assignments store handles only, so `SceneSerializer` output is unchanged.

## Rename / move / delete

| Operation | Behaviour |
|---|---|
| Rename (F2) | Files keep their extension: typing `Hero` renames `Player.png` to `Hero.png`. Changing an asset's extension is refused. The display name follows the file name unless it was customised. |
| Move | Drag onto a folder (grid, tree or breadcrumb), or use **Move To...**. A folder can't move into itself. Registered assets can't leave `Assets/`. |
| Folder rename/move | Every registered asset below the folder is relocated. |
| Delete (Del) | Shows the references first. Referenced assets need an explicit **Delete anyway**. |

**Transactional relocation.** Every registry change is planned and checked for conflicts before the disk is touched. The disk rename happens next, then the registry updates. If any registry update fails, all of them are reverted and the file is renamed back.

**Animation clip references.** When a texture moves, `.anim.yaml` files that point at it via `TextureRelativePath` are rewritten atomically. They also gain a `TextureAssetHandle` line, so the clip resolves the preserved handle. The engine now prefers an explicit `TextureAssetHandle` over the path-derived one.

**Reference scanning (delete).**
- **Sources scanned:** all `*.scene.yaml` files (any scalar equal to the handle), `*.anim.yaml` files (`TextureAssetHandle` / `TextureRelativePath`), and the scene currently open in the editor.
- **Exclusions:** references from files inside the folder being deleted don't count.
- **Why it matters:** a scene that references a deleted asset will **fail to load** (`SceneSerializer` validates handles), so the warning should be taken seriously.

**Atomic delete.**
1. The item is renamed into a hidden `Resources/.xora_trash/` slot.
2. Registry entries are removed.
3. The trash slot is purged. Leftovers from a crash are cleaned up at editor startup.

**Protected paths.** `Resources/` itself, `Assets`, `Scenes`, `Shaders`, `Input` and everything under `Assets/Registry` can't be renamed, moved or deleted.

**Path safety.**
- **Rejected paths:** paths containing `..`, absolute paths, drive letters, control characters, and symlinks that resolve outside `Resources/`.
- **Rejected names:** reserved characters and leading dots.
- **Outside the project:** no operation writes or deletes outside the project root.

## Missing assets and external changes

- Every 3 s (and on **Refresh** / F5) the editor checks each registered file and updates `AssetMetadata::Available`.
- A missing asset appears in its folder as a red **MISSING** ghost entry. The status bar shows "N missing asset(s)", with a list offering **Show** and **Remove from registry**.
- Restoring the file clears the missing state automatically.
- Each visible folder is rescanned at most every 2 s, so files added, removed or modified outside the editor show up without a manual refresh. Thumbnails regenerate when the modification time changes.

## Keyboard shortcuts (Content panel focused)

F5 refresh · Ctrl/Cmd+F search · Alt+Left / Alt+Right back/forward · Backspace parent folder · F2 rename · Del delete · Enter opens a folder · double-click opens a folder or scene.

## Tests

`Tests/src/ContentBrowserTests.cpp` runs every filesystem test in an isolated temporary project (via `XORA2D_RESOURCES`). It covers:
- path safety (traversal, absolute paths, symlink escape), navigation history, filter/search/sort
- enumeration, folder creation, directory cache refresh
- valid image/audio/scene/animation imports, script import with and without scripting, duplicate and name-clash imports, invalid file rejection with no leftovers, the async import queue
- registry persistence, stable handles
- rename/move (files and folders) with handle preservation and animation clip rewriting
- refused operations leaving disk and registry unchanged
- reference-aware delete, folder delete, protected paths
- missing/restored detection and register-in-place
- thumbnail generation, caching, invalidation, failure and eviction
- asset details, and assignment + scene serialization round-trip

## Known limitations

- Search covers the current folder only (it isn't recursive).
- Single selection; there is no multi-select bulk move/delete and no undo for file operations.
- Scenes aren't registry assets, so moving a scene file isn't tracked by anything that refers to it by path. Delete warnings don't cover scenes.
- Legacy `SpriteRendererComponent.TexturePath` values aren't rewritten on rename (handle-based references are unaffected).
- OGG isn't supported (no Vorbis decoder in the engine). MP3 support comes from miniaudio's built-in decoder.
- Audio details decode the whole file (off the main thread). The preview itself loads the clip through `AssetManager` on the main thread when you press Play.
- External changes are detected by polling (2–3 s), not by OS file-watch notifications.
- The registry is saved after each operation. If that save fails, the in-memory registry stays correct, but the status bar reports the error and the change won't survive a restart until a later save succeeds.
- Imports staged when the editor quits mid-import may leave copied files unregistered. They show as "Not imported" and can be registered in place.
