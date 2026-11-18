# Xora2D Editor (Milestones 13–17)

The **Xora2D Editor** is a separate executable that reuses the engine runtime (`Application`, `Window`, OpenGL, `Renderer2D`, `Scene`, assets) and layers **Dear ImGui** (docking branch commit `9dbcb2f1c`) for the UI.

## Build and launch

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/Editor/Xora2D-Editor                                   # opens the Project Manager
./build/Editor/Xora2D-Editor --project ~/Games/MyGame/MyGame.xora  # opens a project directly
```

The editor starts in the **Project Manager** (recent projects, open, create from a template). See
[Projects.md](Projects.md) for the project file format, templates, recent projects and project settings.

Disable the editor target:

```bash
cmake -S . -B build -G Ninja -DXORA2D_BUILD_EDITOR=OFF
```

Sandbox is unchanged and does **not** link ImGui:

```bash
./build/Sandbox/Xora2D-Sandbox
```

## Architecture

| Layer | Responsibility |
|--------|----------------|
| `EditorApplication` | Engine lifecycle, custom frame loop, input routing |
| `EditorLayer` | Workspace + UI per frame |
| `EditorWorkspace` | Panel registry and viewport scene render hook |
| `EditorUI` | Menu bar, toolbar, dock space, status bar |
| `EditorPanel` | Dockable panel base type |
| `EditorContext` | Editable scene, active scene (runtime copy while playing), assets, UUID-based selection, config |
| `EditorPlaySession` | Play / Pause / Step / Stop and ownership of the runtime scene copy |
| `EditorSelection` | Stable entity selection by UUID |
| `EditorSceneCommands` | Create/delete/rename entities, add/remove components |
| `ComponentInspectorUI` | Inspector property editors for ECS components |
| `ImGuiIntegration` | SDL3 + OpenGL3 backends, theme, layout ini |

Engine additions used by the editor (no ImGui in engine headers):

- `ColorRenderTarget` — off-screen FBO for the viewport
- `Window::SetSdlEventHook` / `GetNativeWindowHandle`
- `Logger::SetSink` — console panel subscription
- `Scene::Copy` — deep, UUID-preserving scene clone for Play mode
- `Scene::PauseAudio` / `ResumeAudio` — pause only the voices a scene started

## Viewport rendering

1. `ViewportPanel` measures its ImGui content region.
2. `ColorRenderTarget::Resize` runs only when dimensions change.
3. `Scene::SetViewportSize` matches the render target.
4. Bind FBO → `RendererAPI::Clear` → `Scene::OnRender(Renderer2D)` → unbind.
5. `ImGui::Image` displays the color texture (flipped UVs for OpenGL).

The viewport always renders `EditorContext::GetScene()`: the editable scene in Edit Mode, the runtime copy while playing or paused. The render target is reused across mode changes (it is only recreated when the panel size changes), and the scene's own primary camera is used in both modes.

## Configuration

Global editor settings: `~/Library/Application Support/Xora2D/editor.yaml` (macOS).

ImGui layout: `~/Library/Application Support/Xora2D/imgui.ini` (dock layout; no absolute paths).

Recent projects: `~/Library/Application Support/Xora2D/recent_projects.yaml` (application-level, never inside a project).

## Registering a new panel (example)

```cpp
class MyToolsPanel final : public Xora2DEditor::EditorPanel {
public:
    const char* GetTitle() const override { return "My Tools"; }
    void OnImGui(Xora2DEditor::EditorContext& ctx) override {
        if (ImGui::Begin(GetTitle())) {
            ImGui::Text("Hello from a custom panel.");
        }
        ImGui::End();
    }
};

// In EditorLayer::BuildDefaultPanels():
m_workspace.RegisterPanel(std::make_unique<MyToolsPanel>());
```

## Shortcuts

- **Cmd/Ctrl+P** — Play / Stop
- **Cmd/Ctrl+Shift+P** — Pause / Resume
- **Cmd/Ctrl+S** — Save Scene (asks for a path when the scene has no file yet; not while playing)
- **Cmd/Ctrl+Shift+S** — Save Scene As
- **Cmd/Ctrl+Z** / **Cmd/Ctrl+Shift+Z** — Undo/Redo placeholders

Play shortcuts work whenever no text field is being edited, including while the game view has focus (so a game cannot use these two key combinations in the editor). The other shortcuts are ignored while ImGui captures the keyboard.

## Play mode (Milestone 16)

### Edit Mode vs Play Mode

| | Edit Mode | Playing | Paused |
|---|---|---|---|
| Scene shown and edited by panels | editable scene | runtime copy | runtime copy |
| Physics, Python scripts, animation, scene audio | not updated | updated every frame | frozen |
| Gameplay input | blocked | delivered when the viewport is focused | blocked |
| Indicator | grey `EDIT MODE` badge | green `PLAYING` badge + green viewport border | amber `PAUSED` badge + amber border |

Edit Mode never simulates: dynamic bodies stay where you put them, scripts never run, sounds never play, and animations show their first frame. The editable scene is not connected to the audio or Python engines at all.

### Runtime scene isolation

- **Play** deep-copies the editable scene with `Scene::Copy`. The copy keeps every entity's UUID, name, creation (draw) order and all component data, plus the scene name, viewport size and physics settings. It gets its own physics world, audio voices and script instances; nothing runtime-related is shared with the editable scene. Copying is in memory; no YAML round-trip.
- Only the runtime copy is updated. Transform changes, physics, scripts, animation state, entities created or destroyed at runtime, and Inspector edits made while playing all live in the copy.
- **Stop** shuts the copy down (Python `on_destroy` for every script instance, runtime audio voices stopped, physics callbacks disabled, bodies released) and destroys it. The editable scene is shown again exactly as it was before Play.
- Entities are matched by UUID only. An `Entity` handle belongs to one scene (`Scene::IsValid` checks ownership), so a runtime entity can never be used against the editable scene or vice versa.
- The selection is stored as a UUID: a selected entity resolves to its runtime copy while playing and back to the editable entity after Stop. A selection that only exists at runtime (spawned entity) is cleared on Stop; destroying the selected entity at runtime clears it immediately.
- The Hierarchy shows a **Runtime scene** label and the Inspector a **Runtime values** notice while a session is active, because those edits are discarded on Stop.
- Opening another scene while playing stops the session first. Content-browser reference checks (delete warnings) always use the editable scene.

### Toolbar

| Button | Enabled in | Result |
|---|---|---|
| Play | Edit | Edit → Playing (the viewport receives focus) |
| Pause | Playing | Playing → Paused |
| Resume | Paused | Paused → Playing |
| Step Frame | Paused | runs exactly one simulation frame, stays Paused |
| Stop | Playing, Paused | → Edit, runtime copy discarded |

The toolbar also shows the number of simulation frames run in the current session.

### Pause and frame stepping

- While paused, `Scene::OnUpdate` is not called for the runtime copy, so physics, script `on_update`, animation and audio-source updates all stop. Runtime audio voices are paused (`Scene::PauseAudio`) and resumed on Resume. Editor previews in the Content panel are unaffected. The editor UI stays fully interactive.
- **Step Frame** queues one frame, which runs on the next editor update with a delta of exactly one physics fixed timestep (`PhysicsWorld2D::Settings::FixedTimestep`, 1/60 s by default). The accumulator always holds less than one step, so a step performs exactly one physics substep, one script update and one animation advance regardless of the editor's frame time. A second step request before that frame runs is ignored. Audio started during the step is paused again afterwards.

### Input

- Gameplay input (keyboard/mouse events into the `InputManager`, used by `Input::` and Python `xora2d.Input`) is delivered only while **Playing** and the **Viewport** panel has focus. Mouse buttons, movement and scroll additionally require the cursor to be over the game image. Keyboard input is withheld while an editor text field is active.
- Clicking another panel (or pausing/stopping) blocks gameplay input and clears the input state, so no key or button stays held. Click the viewport to give input back to the game; the viewport header says whether game input is active.
- Python scripts in the runtime scene get the same services as the Sandbox (input, audio, assets).

### Cleanup and shutdown

Closing the editor while playing, opening another scene, or replacing the scene stops the session first, so script `on_destroy` callbacks run and runtime audio stops while the audio and Python engines are still alive.

## Scene Hierarchy (Milestone 14)

The **Hierarchy** panel lists every root-level entity in the active scene (no parent-child tree yet).

- Click a row to select; the selection is stored as an entity **UUID** in `EditorSelection`, not a cached registry handle.
- **Create Empty Entity**, **Delete** (with confirmation), **Rename** (modal), and filter-by-name search.
- Right-click context menu: Select, Rename, Delete.
- Selection is cleared when the entity is destroyed, when the UUID is stale, or when `EditorContext::SetScene` replaces the scene.

## Inspector (Milestone 14)

When an entity is selected, the **Inspector** shows name (editable), read-only UUID, and component sections:

| Component | Editable fields |
|-----------|-----------------|
| Transform | Position, rotation, scale (required; not removable) |
| Sprite Renderer | Color, visibility, size, texture asset (preview when loaded) |
| Camera | Enabled, primary, zoom |
| Rigid Body 2D | Body type, damping, gravity scale, flags |
| Box / Circle Collider 2D | Geometry, density, friction, restitution, sensor |
| Audio Source | Clip, volume, pitch, loop, play-on-start, group, spatial |
| Audio Listener | Active |
| Python Script | Script asset, class, enabled, typed properties (scripting on) |
| Animation | Default clip, speed, loop, autoplay |

Use **Add Component** for types not yet on the entity (duplicates are disabled). Remove optional components with the **X** on each header; physics/audio/script removal goes through `EditorSceneCommands` so runtime state stays consistent (`SyncSceneBodies`, `StopEntityAudio`, `Scene::ReleasePythonScriptInstance`).

Asset pickers list entries registered under the open project's `Assets/` folder (`RegisterKnownResourceAssets` runs whenever a project opens). Asset fields also accept drag-and-drop from the Content panel (validated by handle and type).

## Content panel (Milestone 15)

The Content panel is a full asset browser: folder tree, breadcrumbs, grid/list views, search, type filters, sorting, import (native dialog or OS drag-and-drop), thumbnails, previews, and transactional rename/move/delete that preserves asset handles. Selecting an asset shows its details in the Inspector. See [AssetBrowser.md](AssetBrowser.md).

## Projects (Milestone 17)

- **File** menu:
  - **New Project...** and **Close Project** return to the Project Manager.
  - **Open Project...** and **Open Recent** switch projects directly.
- **New Scene...**, **Save Scene** and **Save Scene As...** work on project-relative paths. Saving always writes
  the editable scene, never the runtime copy, and is disabled while playing.
- **File → Project Settings...** opens the settings panel.
- The status bar shows the project name and the current scene file. The Content panel's root is the project
  folder.
- Switching projects stops Play mode first and clears everything from the previous project, so nothing stale
  remains. See [Projects.md](Projects.md).

## Tests

`Tests/src/ProjectTests.cpp` covers projects: creation, templates, loading/validation, recent history and project isolation. `Tests/src/PlayModeTests.cpp` covers Play mode: scene-copy isolation (UUIDs, ownership, runtime create/destroy, draw order), every state transition and repeated sessions, physics/animation/script/audio behaviour while playing, paused and stepping, input gating, selection safety, and cleanup including closing the editor while playing. `Tests/src/TransformInspectorTests.cpp` covers Inspector transform editing. `Tests/src/ContentBrowserTests.cpp` covers the asset browser (see AssetBrowser.md). `Tests/src/EditorTests.cpp` covers configuration round-trip, panel registration, `ColorRenderTarget` resize, and editor logic: UUID selection, scene replacement, entity create/delete/rename, component add/remove/duplicate prevention, and stale selection handling.

## Current limitations

- Flat entity list only (no hierarchy/parenting).
- No undo/redo for inspector edits.
- Animation clip list editing is limited to default clip fields (not multi-clip authoring).
- Play mode:
  - Mouse position reported to gameplay (`Input::GetMousePosition`) is in window coordinates, not viewport-relative.
  - There is no way to keep a change made during play (no "apply runtime changes"); copy values back by hand.
  - Python scripts cannot create or destroy entities (no binding yet), so runtime spawning is only possible from the Hierarchy panel or C++.
  - The shared Python interpreter keeps module-level state between sessions (`sys.modules`, imported modules); script instances and their namespaces are recreated each session.
  - Physics collision and animation event callbacks are not connected to anything in the editor.
  - Play mode never writes files: Save Scene is disabled while playing and always saves the editable scene.