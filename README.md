# Xora2D

Xora2D is a 2D game engine for creating, running, and exporting desktop games. The long-term project has three parts:

- **Xora2D Engine** — C++ runtime and core systems.
- **Xora2D Editor** — visual game editor.
- **Xora2D Sandbox** — development and testing application.

## Current status

The sandbox opens a resizable 1280×720 window, creates an OpenGL 3.3 core context, and drives rendering through an ECS `Scene` with Box2D **2.4.1** physics (`PhysicsWorld2D`, rigid bodies, box/circle colliders, collision/sensor events).

Not implemented yet: audio, scripting, the editor, scene serialization, and game export.

## Technology stack

- C++20
- CMake and Ninja
- SDL3 for the window, events, and the OpenGL context
- OpenGL 3.3 Core Profile through GLAD 2.0.8
- GLM for mathematics
- EnTT 3.13.2 (vendored under `third_party/entt/`) for ECS
- Box2D 2.4.1 (vendored under `third_party/box2d/`) for 2D physics
- Apple Clang on macOS (Apple Silicon)

## Directory structure

```
Xora2D/
├── CMakeLists.txt          Root build
├── Engine/                 Static library (public headers in include/)
├── Editor/                 Visual editor (not started)
├── Sandbox/                Xora2D-Sandbox executable
├── Tests/                  Core, graphics, renderer, scene, and physics tests
├── Resources/              Runtime assets
├── Docs/
│   ├── Architecture/
│   └── Guides/
└── third_party/            GLAD, stb_image, EnTT, Box2D
```

SDL3 stays inside `Engine/src`. Public headers do not include it. `Application` owns a `Window` and never calls SDL itself. The sandbox links the engine library and uses only the public headers.

Run the tests with:

```bash
./build/Tests/Xora2D-Tests
```

## macOS build

Dependencies are expected from Homebrew: CMake, Ninja, and SDL3. GLM can stay installed for later milestones.

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

The default build type is Debug. Pass `-DCMAKE_BUILD_TYPE=Release` to configure an optimized build.

## Launch the sandbox

```bash
./build/Sandbox/Xora2D-Sandbox
```

Close the window, choose Quit from the macOS menu, or send SIGINT to exit. A failure to initialize SDL or create the window is printed to stderr and the process exits with a non-zero status.
