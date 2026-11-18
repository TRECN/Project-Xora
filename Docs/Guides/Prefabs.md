# Prefabs (Milestone 18)

A **prefab** is a reusable entity, or group of entities, stored as an asset. You create it from entities in a
scene, place any number of independent **instances**, change instances individually (**overrides**), and push
changes back to the prefab (**apply**) or pull prefab changes into instances (**refresh**). Nothing is ever
synchronized automatically.

## Architecture

The prefab system lives in the engine (`Engine/include/Xora2D/Prefab/`) and has no UI dependency. The editor only
calls it.

| Piece | Role |
|-------|------|
| `Prefab` | The asset in memory: name, revision, and its entities in a private content `Scene` (never rendered or simulated) under prefab-local UUIDs. |
| `PrefabSerializer` | Reads and writes `.prefab.yaml`. Entity entries use the **same code** as `SceneSerializer`, through the shared `SerializationDetail` functions, so scenes and prefabs cannot drift apart. |
| `PrefabInstanceComponent` | An ordinary ECS component on each instance entity: prefab handle, instance id, source entity, revision and override list. |
| `PrefabUtility` | Operations: create, instantiate, override tracking, revert, apply, refresh, unpack, save. |
| `AssetManager` / `AssetRegistry` | The `AssetType::Prefab` type: `RegisterPrefabAtRelativePath`, `LoadPrefab` / `GetLoadedPrefab` (cached and read-only), and `ReloadAsset`. Handles are stable (path-derived on creation, kept across rename and move). |

Runtime-only state is never stored: no Box2D bodies, audio voices, Python objects or animation playback position.

## Asset format

`Assets/<folder>/<Name>.prefab.yaml`:

```yaml
Prefab:
  FormatVersion: 1        # required; newer versions are rejected
  Name: Crate             # required
  Revision: 3             # +1 on every save (apply / prefab editor)
Entities:                 # required, non-empty; same entity format as scenes
  - Entity: 5b1c...-9f2e...   # prefab-local id (what instances' SourceEntity refers to)
    TagComponent: {Tag: Crate}
    TransformComponent: {Translation: [0, 0], RotationRadians: 0, Scale: [1, 1]}
    SpriteRendererComponent: {Color: [...], Size: [...], TextureAssetHandle: 7c3e...}
    RigidBody2DComponent: {...}
    ...
```

The supported components are Tag, Transform, SpriteRenderer, Camera, RigidBody2D, BoxCollider2D, CircleCollider2D,
AudioSource, AudioListener, Animation and PythonScript. PythonScript is written in every build; scripts only run in
builds with scripting enabled.

Asset references (textures, audio, scripts, animation clips) are stored by handle, so they resolve through the
project registry.

**Validation.** Loading rejects, with a message:
- malformed YAML;
- a missing header, `FormatVersion` or `Name`;
- an unsupported version;
- an empty or duplicate entity list;
- an entity without a Tag or Transform;
- nested prefab instances.

Missing referenced assets are reported (`PrefabLoadResult::MissingAssets`) but don't block loading. The handle is
kept, so instances work again once the asset returns.

**Safe writes.** `PrefabSerializer::Save`:
1. emits the YAML;
2. re-parses it to validate;
3. writes a temporary file and renames it into place.

A failed save (invalid data, read-only folder, disk error) leaves the existing prefab untouched. Creating a prefab
never overwrites an existing file.

## Creating prefabs

In the editor:
- **Hierarchy → Create Prefab**, or right-click an entity → **Create Prefab...**
- The dialog asks for:
  - **Name**: becomes the file name; no path separators or reserved characters.
  - **Folder**: inside `Assets/`, default `Prefabs`, created if needed.
- The selected entity is pre-checked. Tick more entities to make a multi-entity prefab; the editor has no
  multi-selection or parenting yet, so this list replaces them.
- **Replace the entities with a linked instance** (on by default) turns the source entities into the prefab's
  first instance. They keep their UUIDs.

Creating a prefab:
1. writes the file;
2. registers it with a stable handle;
3. loads it into the cache;
4. shows it in the Content panel.

If any step fails, nothing is left behind: no file, no registry entry, and the entities are unchanged.

API: `Xora2D::CreatePrefabFromEntities(entities, {Name, AssetFolder, LinkSourceEntities}, assets)`.

## Instantiating

- Drag the prefab from the Content panel onto the **Hierarchy** (anywhere) or the **Viewport**. In the viewport the
  instance's first entity lands under the cursor and keeps the others' offsets.
- **Hierarchy → Instantiate...** lists the project's prefabs.
- Content panel: right-click the prefab → **Instantiate in Scene**, or use the **Instantiate** button in its
  Inspector.

Each instantiation creates new entities with **fresh UUIDs** and a new instance id. Instances are independent
copies: editing one never changes another or the prefab.

Instantiation is **transactional**. Failures create nothing, and a failure part-way destroys what was already
created. Failures include:
- an unknown or missing handle;
- a non-prefab asset;
- an invalid prefab file.

Placing an instance at a position records a `Transform.Translation` override.

API: `Xora2D::InstantiatePrefab(scene, handle, assets, {Position})`.

## Instance relationship

Every instance entity carries a `PrefabInstanceComponent`:

| Field | Meaning |
|-------|---------|
| `Prefab` | Source prefab handle (survives prefab rename and move). |
| `InstanceId` | Shared by all entities created by one instantiation. |
| `SourceEntity` | The prefab-local entity this entity mirrors. |
| `Revision` | Prefab revision the instance was created from or last synchronized with. Lower than the prefab's revision means **outdated**. |
| `Overrides` | Sorted property paths that intentionally differ from the prefab. |

The Hierarchy shows instances in blue with `[prefab, outdated, N override(s)]`, or red when the prefab asset is
missing.

## Overrides

Overrides use an **explicit list of property paths**:

- **Fine properties:** `Name`, `Transform.Translation`, `Transform.Rotation`, `Transform.Scale`, `Sprite.Color`,
  `Sprite.Visible`, `Sprite.Size`, `Sprite.Texture`.
- **Whole-component properties**, covering every serialized field and whether the component exists: `Sprite`,
  `Camera`, `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `AudioSource`, `AudioListener`, `PythonScript`,
  `Animation`, `Tilemap` (Milestone 19; tile painting on an instance marks it).

An edit to a fine property marks that property. Any other edit inside a component (for example TilingFactor or a
collider size), or adding or removing a component, marks the whole component. No instance change is ever untracked.

**Tracking is event-driven, not per frame.**
1. When an instance is selected, the Inspector takes a snapshot of its properties (`CapturePrefabProperties`).
2. Only on frames where the Inspector received input (a widget edit, a click, Enter or Tab, a drop) does it
   compare once against the snapshot.
3. `RecordPrefabEdits` marks what changed.

Renaming from the Hierarchy is recorded the same way. When the instance is up to date with the prefab, editing a
value back to the prefab value removes its mark.

`DiffAgainstPrefab(instance, prefab)` returns the actual differences, for inspection and tests.

## Apply, revert, refresh

The Inspector's **Prefab Instance** section for the selected entity shows:
- the source prefab;
- a status: up to date, outdated (instance and prefab revisions), or missing;
- the overrides, each with **Revert**;
- the actions in the table below.

| Action | Effect |
|--------|--------|
| **Apply to Prefab** | Writes the instance's current data (all its entities) into the prefab: new revision, same handle and file, validated and atomic. On success only this instance's overrides are cleared. **Other instances are not touched**; they show as outdated. On failure the prefab file, the cache and the instance's overrides are unchanged. Disabled in Play mode. |
| **Revert** (per property) | Restores that property from the prefab and clears its mark. Reverting a whole component also clears its fine marks. |
| **Revert All** | Restores every entity of the instance from the prefab's current revision and clears all overrides. |
| **Refresh** | Explicitly updates an instance to the prefab's current revision, keeping its overrides. Entities added to the prefab are created. Instance entities whose source was removed from the prefab are **unlinked, never deleted**. Content panel → **Refresh Instances in Scene** refreshes every instance in the open scene. |
| **Unpack** | Removes the link and overrides; entities and values stay. |
| **Open Prefab** | Opens the prefab for editing (below). |
| **Select Asset** | Shows the prefab in the Content panel and Inspector. |

## Editing a prefab in isolation

Open a prefab in any of these ways:
- double-click it in the Content panel;
- **Open Prefab** in its context menu or Inspector;
- **Open Prefab** on an instance.

The editor then works on a **private copy** of the prefab:

- Hierarchy, Inspector and Viewport show only the prefab's entities. The viewport frames them automatically, since
  prefabs usually have no camera.
- The open scene, its selection and the cached prefab are untouched.
- The toolbar shows **EDITING PREFAB: name** with **Save Prefab** (also Cmd/Ctrl+S) and **Cancel**.
- **Save** validates and writes the asset (new revision, same handle), then returns to the scene. Instances are
  *not* updated; use **Refresh**.
- **Cancel** discards the copy and restores the scene and its selection.
- These are unavailable while editing a prefab:
  - Play mode;
  - instantiating prefabs (no nesting);
  - creating prefabs;
  - saving the scene.
- Opening a scene, switching or closing the project, or closing the editor while a prefab is open discards the
  unsaved prefab edits. Closing the editor logs a warning.

## Scenes

Scenes store each instance's link next to the normal component data:

```yaml
  - Entity: 3a9f...-71c2...          # the instance entity's own unique UUID
    TagComponent: {Tag: Crate}
    ...
    PrefabInstance:
      Prefab: 7c3e5d...              # prefab asset handle
      Instance: 0b1e...-44aa...
      SourceEntity: 5b1c...-9f2e...
      Revision: 2
      Overrides: [Sprite.Color, Transform.Translation]
```

- Scenes without `PrefabInstance` blocks load exactly as before.
- A malformed `PrefabInstance` block is a load error.
- A prefab asset that is missing from the registry is a **warning** (`SceneSerializeResult::Warnings`). The
  entities keep their data and their link. Revert, Refresh and Apply then report the missing prefab without
  touching the entity.

## Content panel

- `.prefab.yaml` files are recognized as prefabs: blue cube icon, `PREFAB` badge and a **Prefabs** type filter.
- The Inspector shows prefab metadata: name, revision, format version, entity names, components, and missing asset
  references.
- Rename and move go through the existing transactional operations and **keep the handle**, so scenes and
  instances keep working.
- Deleting a prefab is refused while scene files, other prefabs or the open scene reference it (the existing
  reference check now also scans `.prefab.yaml` files and `PrefabInstance` links).
- Invalid prefab files are shown with their error. A prefab file copied into `Assets/` can be registered in place
  after validation.
- Importing `.prefab.yaml` from outside the project is refused, because prefabs carry project-specific asset
  handles.

## Play mode

- `Scene::Copy` copies `PrefabInstanceComponent`, so the runtime scene keeps the prefab relationships.
- Changes to runtime instances stay in the runtime copy.
- Prefabs instantiated while playing go into the runtime scene and get physics bodies immediately. They disappear
  on Stop.
- Prefab assets can't be written from Play mode: Apply, Create Prefab and Open Prefab are disabled. After Stop the
  editable scene and its instance links and overrides are exactly as before.

## Tests

`Tests/src/PrefabTests.cpp`, 134 checks:
- **Serialization:** every component and asset handle, round-trip, 11 malformed or unsupported inputs, missing
  assets, failed and refused writes.
- **Creation and instantiation:** links, no-overwrite, invalid names and paths, multi-entity order, unique UUIDs,
  independence, exact component restore, asset references, placement, transactional failures, rename and move
  keeping the handle.
- **Overrides:** detection of every supported property, an edit back removing its override, revert one and revert
  all, apply, other instances untouched until refreshed, refresh keeping overrides, structural refresh, failed apply,
  unpack.
- **Scene integration:** save and load of links, UUIDs and overrides; old scenes; malformed blocks; a missing
  prefab asset.
- **Runtime:** metadata in the runtime copy, runtime edits and runtime instances isolated, assets and editable scene
  unchanged after Stop.
- **Editing workflow:** isolation, cancel, save (new revision, same handle), blocked actions, closing while editing.

## Known limitations

- No nested prefabs, prefab variants or entity parenting. Multi-entity prefabs are flat groups, and the
  create-prefab dialog stands in for multi-selection.
- Overrides are tracked when edits come from the Inspector or Hierarchy rename. Changes made in code (C++ or Python)
  are not recorded as overrides, although Apply still writes the full current data.
- Reverting a single property on an *outdated* instance takes the prefab's *current* value.
- Entities added to a prefab appear at the prefab's position on Refresh (the instance offset isn't applied).
- Entity names can repeat across instances; the engine logs its usual duplicate-name warning.
- The isolated editor discards unsaved prefab changes without a prompt when the scene, project or editor is closed.
- No undo/redo for prefab operations (the editor has no undo yet).
