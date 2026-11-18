# Projects and the Project Manager (Milestone 17)

Xora2D works on **projects**: a folder containing a `<Name>.xora` project file, the game's assets, scenes and
configuration. The editor opens with the **Project Manager**, where you create, open and browse projects. The
editor itself opens once a project is loaded.

## Project directory structure

```text
MyGame/
├── MyGame.xora                 project file (format below)
├── Assets/                     everything the asset registry manages
│   ├── Textures/
│   ├── Audio/
│   ├── Scripts/                Python gameplay scripts
│   ├── Animations/
│   └── Registry/
│       └── AssetRegistry.yaml  stable asset handles -> Assets-relative paths
├── Scenes/
│   └── Main.scene.yaml         startup scene (templates)
├── Config/                     project configuration files (e.g. input mappings)
└── ProjectSettings/            reserved for editor/project tooling state
```

Projects never contain engine sources, shaders or other engine files. Shaders always come from the engine's
`Resources/` folder; project content comes from the project folder.

All references inside a project are **project-relative** (forward slashes):

- Scene and directory paths in the `.xora` file are relative to the project root (`Scenes/Main.scene.yaml`).
- The asset registry stores paths relative to `Assets/` (`Scripts/PlayerController.py`). Scenes refer to assets by
  their stable handle, not by path.

Paths are resolved with `Project::ResolvePath()`, which rejects absolute paths, `..`, drive letters and anything
that escapes the project root, including through symbolic links.

## Project file format (`.xora`, format version 1)

YAML, written by `ProjectSerializer`:

```yaml
XoraProject:
  FormatVersion: 1          # required; files with a newer version are refused
  EngineVersion: 0.1.0      # engine that last saved the file (informational)
  Name: MyGame              # required
  StartupScene: Scenes/Main.scene.yaml   # optional, project-relative *.scene.yaml
  Directories:              # optional; defaults shown
    Assets: Assets          # must be "Assets" in format version 1
    Scenes: Scenes
    Scripts: Assets/Scripts # must be inside Assets (scripts are registered assets)
    Config: Config
  Settings:                 # optional; defaults shown
    ProjectVersion: 0.1.0
    Window: {Width: 1280, Height: 720, VSync: true}
    Physics: {Gravity: [0, -9.81]}
    Scripting: {Enabled: true}
```

Validation happens before a project is used. Each failure has its own error (`ProjectError`) and a message that
names the problem:

| Problem | Error |
|---------|-------|
| File missing | `FileNotFound` |
| Not a `.xora` file / no `XoraProject` root | `NotAProjectFile` |
| Malformed YAML | `InvalidYaml` |
| `FormatVersion` newer than supported, or < 1 | `UnsupportedVersion` |
| `FormatVersion` or `Name` missing | `MissingField` |
| Invalid name | `InvalidName` |
| Bad window size / gravity, unsafe paths, wrong types | `InvalidValue` |
| `Assets/` folder missing | `MissingDirectory` |

A missing startup scene or missing `Scenes/`, `Scripts/` or `Config/` folder is a **warning**: the project still
opens, and missing standard folders are recreated.

Saving is atomic (temporary file + rename), and invalid settings are rejected before anything is written.

## Creating projects

**Project Manager → New Project**:

1. **Name**: 1–64 characters (letters, digits, spaces, `-`, `_`, `.`). It cannot start with `.`, `-` or a space,
   or end with a space or `.`. The name becomes the folder and file name.
2. **Location**: an existing parent folder (**Browse...** opens a native folder picker). It defaults to the folder
   of the most recent project, or `~/Documents`.
3. **Template** (see below).

The form validates as you type and shows the exact path it will create:
`<Location>/<Name>/<Name>.xora`. After creation the project opens immediately and is added to the recent list.

Safety rules:

- An existing non-empty folder with the project name is never touched, so creation is refused.
- An existing *empty* folder with that name is reused.
- Everything is written into a hidden staging folder (`.xora_create_*`) inside the location, then renamed into
  place. If any step fails, only that staging folder is removed, so no partial project is left behind and no user
  file is ever deleted.

Programmatic API: `Xora2D::CreateProject(ProjectCreateOptions)` (`Xora2D/Project/ProjectTemplates.h`).

## Templates

| Template | Contents |
|----------|----------|
| **Empty 2D Project** | `Scenes/Main.scene.yaml` with a primary camera only. |
| **Basic 2D Game** | Camera, static ground, two platforms, coins, a goal, and a dynamic player with `Assets/Scripts/PlayerController.py` (registered in the asset registry). Press Play, click the viewport: A/D or ←/→ move, Space jumps. |
| **Physics Demo** | Camera, ground, two walls, a tilted ramp, a 3×4 stack of dynamic boxes and four bouncing balls. Press Play to watch them fall. |

All templates create the full directory structure, an asset registry and a `.xora` file with default settings.
Layouts fit the default editor viewport. The engine has no sprite depth order yet, so templates avoid overlapping
sprites.

## Opening projects

- **Recent Projects**: double-click a row or press **Open**.
- **Open Project...**: native file dialog filtered to `*.xora`.
- **Open by path**: paste a `.xora` path, or a project folder containing exactly one `.xora` file.
- Command line: `./build/Editor/Xora2D-Editor --project /path/MyGame/MyGame.xora` (or just the `.xora` path).
- In the editor: **File → Open Project... / Open Recent**.

Opening a project:

1. validates the project file (errors are shown in a dialog; the Project Manager stays open);
2. stops Play mode and saves the previous project's asset registry;
3. resets the content services (queued imports are committed, thumbnail/import workers restarted, caches cleared);
4. unloads the previous project's assets and registry, and resets selection and script module state;
5. points content resolution (`Xora2D::SetContentRoot`) at the project root;
6. loads `Assets/Registry/AssetRegistry.yaml`, registers new files under `Assets/`, and marks missing ones;
7. opens the startup scene;
8. sets the window title to `Xora2D Editor - <Name> (<root>)` and applies the project's VSync setting.

**Missing startup scene**: the editor opens a new, unsaved scene containing only a camera, and shows a dialog to:
- **Create** a new startup scene at a chosen path (saved and recorded in the `.xora` file);
- **Open** an existing scene from the project and make it the startup scene;
- **Continue** without one.

**File → Close Project** returns to the Project Manager. **File → New Project...** does the same and jumps to the
create form. Closing or switching never deletes project files.

**Open Engine Samples** (Project Manager) opens the engine's own `Resources/` folder without a project. This is the
pre-project workflow (`ExampleScene`, `SandboxPhysicsDemo`, and so on). Changes there are written into `Resources/`.

## Recent projects

- Stored per user, outside every project, in `recent_projects.yaml` in the editor config folder:
  - macOS: `~/Library/Application Support/Xora2D/`
  - elsewhere: `$XDG_CONFIG_HOME/Xora2D/` or `~/.config/Xora2D/`
- Each entry records the project name, the absolute `.xora` path and when it was last opened. The list is newest
  first and limited to **10** entries (the oldest are dropped).
- Duplicates are impossible: paths are normalized (absolute, symlinks resolved, `.`/`..` removed) before
  comparing.
- Moved or deleted projects stay listed, marked **Missing:** with **Open** disabled. Opening one anyway reports
  that it may have been moved, and **Remove** drops it.
- **Remove** only edits the list. The project's files are kept.

## Project settings

**File → Project Settings...** edits a draft copy. **Apply** validates the draft and saves the `.xora` file;
**Revert** discards it.

| Setting | Effect |
|---------|--------|
| Name | Display name, also shown in the title bar. The folder and `.xora` file keep their names. |
| Project version | Free-form version of your game. |
| Startup scene | Chosen from the scenes found in the project (or none). **Rescan** refreshes the list. |
| Paths | Project root and the Assets/Scenes/Scripts/Config folders (read-only in format version 1). |
| Window width / height | Runtime window size for the game (160×120 to 16384×16384). |
| VSync | Runtime VSync; also applied to the editor while the project is open. |
| Gravity | World gravity in m/s², applied to every runtime scene when Play starts. The edit scene never simulates. |
| Scripting | When off, Play mode does not run Python scripts. In builds without scripting the checkbox is disabled and the value is kept. |

Settings changed elsewhere, such as a new startup scene chosen from the missing-scene dialog, show up in the panel
unless you have unsaved edits.

## Code map

| Piece | Location |
|-------|----------|
| `ProjectConfig`, `ProjectSettings`, validation, `ProjectError` | `Engine/include/Xora2D/Project/ProjectConfig.h` |
| `.xora` parsing / writing | `Engine/include/Xora2D/Project/ProjectSerializer.h` |
| `Project` (load, paths, startup scene, settings) | `Engine/include/Xora2D/Project/Project.h` |
| Templates and `CreateProject` | `Engine/include/Xora2D/Project/ProjectTemplates.h` |
| Content root (project vs. engine resources) | `Engine/include/Xora2D/Core/Paths.h` |
| Open / close / switch, scene files | `EditorContext` (`OpenProject`, `CloseProject`, `CreateScene`, `SaveScene`, ...) |
| Recent history | `Editor/include/Xora2DEditor/Project/RecentProjects.h` |
| Project Manager window | `Editor/include/Xora2DEditor/Project/ProjectManagerUI.h` |
| Project Settings panel | `Editor/include/Xora2DEditor/Panels/ProjectSettingsPanel.h` |

The project system lives in the engine and has no UI or editor dependency. The editor decides when to open or close
projects and how to present them.

## Tests

`Tests/src/ProjectTests.cpp`:

- **Creation**: valid creation, directory structure, metadata, every template's content, invalid names and
  locations, existing folders, and cleanup after a failure at every creation step.
- **Loading**: valid files, invalid YAML, unsupported versions, missing metadata, invalid values, a missing startup
  scene, a missing asset folder, round-trips, path resolution and symlink escape.
- **Recent projects**: adding, duplicates, removal, persistence, missing projects, the size limit and an invalid
  history file.
- **Isolation**: switching between two projects while playing (assets, registry files, scenes, settings, no stale
  scene or project references), closing, engine samples, and the Basic template being playable.
