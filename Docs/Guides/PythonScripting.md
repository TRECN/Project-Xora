# Python Scripting (Milestone 11)

Xora2D embeds **CPython 3.12** via **pybind11 v2.13.6**. One interpreter is owned by `Application::GetPythonScriptEngine()`.

## Build

```bash
cmake -S . -B build -G Ninja -DXORA2D_ENABLE_SCRIPTING=ON
cmake --build build
```

Disabled build:

```bash
cmake -S . -B build-no-scripting -G Ninja -DXORA2D_ENABLE_SCRIPTING=OFF
```

On macOS Apple Silicon install CPython headers:

```bash
brew install python@3.12
```

Override interpreter: `-DXORA2D_PYTHON_EXECUTABLE=/path/to/python3.12`

## Runtime ownership

| Piece | Owner |
| --- | --- |
| Interpreter | `PythonScriptEngine` |
| ECS sync | `ScenePythonScriptSystem` (internal) |
| Python instance | Per-entity namespace in engine; not stored on components |
| `xora2d` module | Embedded via `PYBIND11_EMBEDDED_MODULE` |

All script callbacks run on the **main thread** with GIL acquired. Python is **not** called from physics or audio callbacks.

## Python API

```python
import xora2d

class MyScript:
    def on_create(self):
        xora2d.Log.info(self.entity.name)

    def on_update(self, delta_time):
        if xora2d.Input.is_key_down(xora2d.Key.W):
            p = self.entity.transform.position
            self.entity.transform.set_position(p.x, p.y + 200 * delta_time)
```

- Transform units: **pixels**, rotation in **radians**.
- Physics impulses: **meters** (Box2D), same as C++ sandbox.
- Entity handles validate UUID + scene each access; destroyed entities return `is_valid() == False`.

## Assets & components

Register scripts under `Resources/Assets/Scripts/*.py` with `AssetManager::RegisterPythonScriptAtRelativePath`.

`PythonScriptComponent` stores `ScriptAsset`, `ClassName`, `Enabled`, and typed `Properties` (Number, Boolean, String) — serialized in scene YAML v2.

## Reload

```cpp
GetPythonScriptEngine().ReloadScript(handle, assetManager);
```

Compiles new source before swap; keeps previous resource on failure and invalidates active instances.

## Security

Scripts are **trusted local project code**. This is not a sandbox for untrusted Python.

## Packaging (future)

Ship `Python.framework` (macOS) or embedded `libpython` + standard library beside the game binary. Extension point: `third_party/python/Xora2D.cmake` documents discovery; exporter copies `PYTHONHOME` layout (not implemented here).