# Tilemaps and Tilesets (Milestone 19)

Tile-based levels: a **tileset** cuts a spritesheet texture into tiles with stable ids. A **TilemapComponent**
places those tiles on an integer grid, in layers. The editor paints them in the viewport. Rendering goes through
`Renderer2D` and collision through Box2D. Python scripts can read and change tiles at runtime.

## Architecture

The data model and tools live in the engine (`Engine/include/Xora2D/Tilemap/`) and have no editor dependency.

| Piece | Role |
|-------|------|
| `TilemapTypes.h` | `TileId` (0 = empty), `TileCell` (id + flip/rotation flags, 8 bytes), `GridCoord`, `TileCollision`, chunk math (32x32-cell chunks, floor division for negative coordinates). |
| `Tileset` / `TilesetSerializer` | A texture cut into tiles: grid (tile size, margin, spacing) or manual regions; cached UVs; per-tile collision, name and tint; `.tileset.yaml`. |
| `TilemapComponent` | Tileset, tile size (rectangular allowed), layers, active layer, collision settings, optional source asset. Each layer stores its cells in chunks. |
| `TilemapEditing` | UI-free operations: brush/eraser strokes, rectangle fill, bucket fill, eyedropper, copy/paste/move, layer add/remove/rename/reorder/duplicate. Undo/redo stores cell diffs. |
| `TilemapRendering` | Cached chunk geometry, flips/rotation, culling, bulk submission to `Renderer2D`; greedy collision-rectangle merging. |
| `TilemapSerializer` | Component YAML shared by scenes, prefabs and `.tilemap.yaml` assets (run-length-encoded chunks). |

The engine is integrated as follows:
- `Scene::OnRender` draws tilemap layers before sprites, then sprites, then layers marked *AboveSprites*.
- `Scene::Copy` copies tilemaps into Play-mode scenes.
- `PhysicsWorld2D` builds and syncs tile collision.
- `SceneSerializer` and prefabs persist `TilemapComponent`.
- `AssetManager` has `Tileset` and `Tilemap` asset types (`LoadTileset` is cached).

## Grid coordinates

Cells use integer coordinates. Cell `(x, y)` covers `[x*w, (x+1)*w) x [y*h, (y+1)*h)` in the tilemap's local space,
where `w`/`h` is `TileSize`. **+Y is up**, like the rest of the engine, and negative coordinates are valid. The
entity's transform (position, rotation, scale) places the grid in the world. Floating-point positions are never
used as tile identifiers. Use `LocalToCell` / `CellToLocal`, or `world_to_cell` / `cell_to_world` from Python.

Storage is chunked: 32x32 cells per chunk, each chunk 8 KiB stored inline, with no per-tile allocations. Empty
chunks are freed. Every change bumps the chunk's revision, a monotonic counter that is never reused. Render and
collision caches compare revisions, so only changed chunks are rebuilt.

## Tileset format (`.tileset.yaml`)

```yaml
Tileset:
  FormatVersion: 1
  Name: Terrain
  Texture: 7c3e5d0a91b2f4e6        # texture asset handle
  TextureSize: [144, 72]           # recorded at import; checked against the image on load
  TileSize: [16, 16]
  Margin: 1                        # pixels around the sheet
  Spacing: 2                       # pixels between tiles
  Mode: Grid                       # or Manual
Tiles:                             # only tiles with non-default properties (every tile for Manual)
  - {Id: 3, Collision: Solid, Name: grass}
  - {Id: 25, Collision: OneWay}
  - {Id: 40, Region: [0, 64, 32, 16]}   # Manual mode: explicit pixel region (top-left origin)
```

- **Ids are stable.** Grid tiles are numbered row-major from 1 (`row * columns + column + 1`). Manual tiles keep
  their explicit ids.
- **Validation** rejects:
  - zero or negative tile sizes;
  - negative margin or spacing;
  - sheets where no whole tile fits;
  - manual regions outside the texture;
  - duplicate ids;
  - properties for tiles that don't exist.
- **Changed or missing textures.** If the image was resized since import, loading fails with "re-import the
  tileset" instead of producing wrong regions. A missing texture is a warning: the tileset loads, but its tiles
  are not drawn.
- **UVs.** `UV` holds the exact region in the engine's bottom-up texture orientation. `RenderUV` is inset by half a
  texel, so filtering never samples margins, spacing or neighbouring tiles (no seams).

## Tilemap data format

The same body appears in scenes (`TilemapComponent:`), prefabs and `.tilemap.yaml` assets (under a
`Tilemap: {Name}` header):

```yaml
TilemapComponent:
  FormatVersion: 1
  Tileset: 9a1f...
  TileSize: [16, 16]
  ActiveLayer: 1
  Source: 4b2c...                     # optional tilemap asset
  Collision: {Enabled: true, Friction: 0.6, Restitution: 0, Category: 1, Mask: 65535}
  Layers:
    - Name: Ground
      Visible: true
      Locked: false
      Opacity: 1
      AboveSprites: false
      Collision: true
      Chunks:
        - At: [0, 0]                    # chunk coordinate
          Tiles: "10x40 3x40 0x944"     # 1024 cells, row-major from bottom-left: <id>[:<flags>][x<count>]
```

- Flags: bit 0 flips X, bit 1 flips Y, bits 2-3 are quarter turns counter-clockwise.
- Only ids and flags are stored, never texture data. A 12,208-tile level scene is under 6 KB.
- Loading is transactional. Malformed tile data (bad tokens, a chunk that isn't 1024 cells) fails the whole load and
  leaves the open scene untouched.
- Unknown tile ids, missing tilesets and missing textures are reported in `SceneSerializeResult::Warnings`. The data
  is kept, and undefined tiles are simply not drawn.
- Scenes without tilemaps load unchanged.

## Rendering and batching

- Each layer chunk keeps cached geometry: 4 pre-built vertices per tile, in tilemap-local pixels, with
  flip/rotation-adjusted UVs. It is rebuilt only when the chunk's revision, the tileset (reloaded asset) or the tile
  size changes. Frames without edits rebuild nothing (`Scene::GetTilemapRenderStats().ChunksRebuilt == 0`).
- Chunks outside the camera view are culled. Culling is skipped when the camera is rotated.
- Cached vertices go to `Renderer2D::DrawTexturedQuads` and share the normal batches: up to 10,000 quads per draw
  call per texture. In the tests, 12,000 tiles draw in 2 draw calls.
- Layers draw in list order. Visibility skips a layer, and opacity is applied as a tint. Layers with
  *AboveSprites* draw after sprites (the default *Foreground* layer); the rest draw before.
- Rotation is per cell in quarter turns. On rectangular tiles, a rotated tile is stretched to the cell.

## Editing tools (Editor)

Open the **Tilemap** panel with **View > Tilemap**, the Inspector's *Open Tilemap Editor*, or by double-clicking a
tilemap asset.

- **Target.** The selected entity's tilemap. *Create Tilemap Entity* or *Add Tilemap to Selected* if there is none.
- **Paint in viewport.** While enabled, the viewport:
  - uses an editor camera: mouse wheel zooms around the cursor; right- or middle-drag pans;
  - draws the grid, but only the visible lines;
  - shows the hovered cell, a translucent brush preview, drag rectangles and the selection;
  - shows the tool, layer, cell coordinates and zoom in its header.
- **Tools:**

  | Tool | Key | Behaviour |
  |------|-----|-----------|
  | Brush | B | Click or drag to paint. Fast drags are interpolated cell by cell, and repeated mouse events on a painted cell are ignored. One drag is one undo step. |
  | Eraser | E | Like Brush, but clears cells. |
  | Rect | R | Drag to fill a rectangle; Shift erases it. |
  | Fill | G | Bucket fill of the connected same-tile area; Shift erases. Filling empty space is limited to the painted bounds. |
  | Pick | I | Eyedropper: picks the topmost visible tile and its flips, and switches to its layer. |
  | Select | S | Drag a region. Copy (Cmd/Ctrl+C), Cut (Cmd/Ctrl+X), Paste (Cmd/Ctrl+V, then click), Delete/Backspace, arrow keys move it, Esc clears. |

- **Brush options:** Flip X (X), Flip Y (Y), Rotate (T).
- **Undo / Redo:** Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z or Cmd/Ctrl+Y.
  - History is per tilemap entity and stores cell diffs: a one-tile change on a 20,000-tile map takes under
    1 KB.
  - Layer removal stores that layer's data so it can be restored.
  - History is cleared when the scene changes.
- **Palette:** the active layer's tileset, drawn with tile previews. Click a tile to select it; tooltips show its
  id, region and collision.
- Painting is only available in Edit mode. During Play, tilemaps change through scripts.
- Edits to a prefab instance's tilemap are recorded as a `Tilemap` override.

### Layers

Default layers:

| Layer | Collision | Drawn |
|-------|-----------|-------|
| Background | no | behind sprites |
| Ground | yes | behind sprites |
| Decorations | no | behind sprites |
| Foreground | no | in front of sprites |

In the panel's layer table you can:
- **Add**, **Duplicate** and **Remove** layers, and **rename** one by double-clicking it;
- reorder layers with the arrows (the top row is drawn last);
- toggle **Show**, **Lock** and **Solid** (collision);
- set **Opacity**, and make the active layer draw in front of sprites.

Add, remove, rename, reorder and duplicate are undoable. **Locked or hidden layers refuse every paint operation**,
and the panel says why.

### Tilesets (Editor)

- Create one by right-clicking a texture in the Content panel and choosing **Create Tileset...**. Edit one by
  double-clicking a `.tileset.yaml` or using **Edit Tileset**.
- The **Tileset** panel sets the name, texture, Grid or Manual mode, tile size, margin and spacing (or manual
  regions).
- It shows a live preview of the cut with a grid overlay. Click a tile to set its collision (None, Solid, One-way
  platform), name and tint.
- **Save** validates the tileset and writes it atomically to `Assets/Tilesets/<Name>.tileset.yaml` for a new
  tileset, or to its existing file. Existing files are never overwritten by a new tileset.
- Tilemaps using the tileset pick up the change on the next frame.

## Content panel

- Tilesets show their texture as the thumbnail, with a tile grid and a TILESET badge. Tilemaps have a grid icon.
  Both have a type filter (*Tilesets & Tilemaps*).
- The Inspector shows tileset metadata: tile size, margin/spacing, tile and collision counts, and the texture's
  status (missing, or resized). It shows tilemap metadata: layers, tile count and the tileset's status.
- **Double-click:**
  - a tileset opens the Tileset editor;
  - a tilemap selects the entity using it, or creates an entity from the asset, and opens the Tilemap panel.
- Dragging a tileset onto the Inspector's Tileset field, or using *Assign to Selected Entity*, assigns it. A
  Tilemap component is added if needed.
- **Assets:**
  - rename and move keep handles;
  - deleting is refused while a scene, prefab, tilemap or tileset references the asset;
  - files copied into `Assets/` can be registered in place after validation;
  - importing tilesets or tilemaps from outside the project is refused (they hold project-specific handles).
- **Tilemap assets (`.tilemap.yaml`)** are an explicit export/import. Scenes always store their own tiles.
  - In the Inspector, **Save As Asset...** writes `Assets/Tilemaps/<Entity>.tilemap.yaml` and records it as the
    component's `Source`.
  - **Save to Asset** and **Reload from Asset** sync explicitly.

## Collision

- **One static Box2D body per tilemap entity.** Its fixtures are merged rectangles, never one body per tile.
- **Merging.** The collidable cells of all collision layers in a chunk are greedy-merged into rectangles, with
  Solid winning over One-way. A 20-tile floor is one fixture, an L shape is two, and a full chunk is one.
- **Rebuilds.** Rectangles never cross chunk borders, so each chunk rebuilds independently.
  - A chunk is rebuilt only when its collision signature changes. The signature combines that chunk's revision on
    every collision layer with the tilemap's structural revision (layer flags, tile size, tileset, settings).
  - Edits on non-collision layers never rebuild collision.
- **One-way platforms.** A `PreSolve` contact filter only lets bodies landing from above collide: the contact is
  ignored while the other body moves up, or while its bottom is below the platform top.
- **Settings.** `Collision.Enabled`, friction, restitution, and Box2D category/mask bits (collision layers) are set
  per tilemap.
- **Cleanup.** The body is destroyed when the entity is destroyed, when the component is removed, when collision is
  disabled, or with the scene.
- **Edit mode.** Edit scenes never build tile collision: editable scenes don't simulate. Play-mode scenes build
  their own bodies, so runtime collision is isolated, and runtime tile edits (e.g. from scripts) re-collide on the
  next physics step.

## Python API

`entity.tilemap`, available on any entity (`exists` is False without a Tilemap component). Layers are given by name;
`""` means the active layer.

```python
tm = self.entity.tilemap
tm.exists                         # bool
tm.get_tile(x, y, layer="")       # tile id, 0 = empty, -1 = no tilemap / layer
tm.set_tile(x, y, tile_id, layer="", flip_x=False, flip_y=False, rotation=0)  # -> bool
tm.clear_tile(x, y, layer="")     # -> bool (True if a tile was removed)
tm.world_to_cell(wx, wy)          # -> (x, y), uses the entity transform
tm.cell_to_world(x, y)            # -> (wx, wy), cell centre
tm.layer_names()                  # -> list[str]
tm.tile_count(layer="")           # -> int
```

Changes affect the runtime scene only. Render geometry and collision of the touched chunk update automatically.
No renderer or Box2D objects are exposed.

## Performance

These numbers come from a Debug build on an Apple M1, logged by `TestLargeMapPerformance`:

| Operation (32,768 tiles in 32 chunks) | Time |
|---|---|
| Fill (two 256x64 layers) | about 9 ms |
| First geometry build | about 6 ms |
| Frame with no edits (cache checks only) | about 0.01–0.02 ms |
| Collision merge (16,384 solid tiles → 16 rectangles) | about 0.5 ms |
| First physics sync | about 0.6 ms |
| Idle physics step | about 0.02 ms |
| Physics step after a one-tile edit (one chunk re-merged) | about 0.07 ms |

- **Live editor:** a 12,208-tile map averaged 17.6 ms per frame, which is the 60 Hz VSync limit, with 0 chunks
  rebuilt per frame.
- **Memory:** 8 KiB per allocated chunk per layer. Empty chunks are freed.
- **Per-frame cost:** the CPU cost per frame is copying the cached vertices of visible chunks into the batch. GPU
  buffers are not persistent across frames.
- **Overlay:** the editor overlay only draws visible grid lines, and tools never scan the whole map. Bucket fill on
  empty space computes the map bounds once per fill.

## Tests

`Tests/src/TilemapTests.cpp`, 145 checks:
- **Tilesets:** grid generation with margin and spacing, exact UVs, every validation rule, manual regions,
  round-trip, malformed files, missing and resized textures, handle kept on rename.
- **Data:** set/get/remove, negative coordinates, chunk borders and freeing, invalid layers, flags, rectangular
  tiles.
- **Editing:** brush duplicate suppression, eraser, rectangle and bucket fill (bounded), eyedropper,
  copy/paste/move, multi-step undo/redo, diff-sized undo memory, locked and hidden layer protection, undoable layer
  operations.
- **Rendering:** UV flip and rotation math, chunk invalidation; GPU pixel checks for upright UVs, mirrored flips,
  layer order, hidden and zero-opacity layers; 12,000 tiles in 2 draw calls; culling; empty tilemaps.
- **Physics:** landing on solid tiles, passing through decoration tiles, one fixture per merged floor, L shapes,
  solid vs one-way, per-chunk rebuild counts, collision following runtime edits, one-way from above and below,
  cleanup on destroy, component removal and disable.
- **Serialization:** RLE, scene round-trip of every property, a transactional failed load, unknown ids and missing
  tilesets as warnings, tilemap asset save/load and no silent overwrite.
- **Prefabs and runtime:** a prefab with a tilemap, a Tilemap override, Play-mode copy and isolated collision,
  runtime edits not leaking, unchanged after Stop.
- **Python:** read, write, and coordinate conversion; edits stay in the runtime scene.
- **Large map:** timings plus structural assertions.

## Known limitations

- No autotiling or terrain rules, animated tiles, or isometric/hex grids.
- Collision merges within a chunk only, so a long floor is one fixture per 32 cells. Collision ignores entity
  rotation for cell size (rotation of the whole body is applied).
- The editor's grid overlay assumes an unrotated tilemap transform.
- One texture per tileset. A tilemap uses one tileset per layer (the tilemap default or a per-layer override).
- Tileset changes made during Play don't rebuild existing runtime collision until a structural change.
- Painting and editor undo are Edit-mode only. Layer property toggles (visibility, lock, opacity, collision) are not
  in the undo history.
- Tilemap assets are explicit export/import; there is no live link between a scene and its `.tilemap.yaml`.
