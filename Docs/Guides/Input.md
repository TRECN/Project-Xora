# Input System (Milestone 9)

Xora2D centralizes keyboard and mouse state in `InputManager`, updated once per application frame from the existing engine event stream (SDL is translated inside the platform layer only).

## Architecture

1. `Application::Run` calls `InputManager::BeginFrame()` to clear per-frame transition flags and mouse/scroll deltas.
2. `Window::PollEvents` dispatches SDL events as engine `Event` instances.
3. `Application::HandleEvent` forwards each event to `InputManager::OnEvent`, then to `OnEvent` overrides.
4. Game code reads state in `OnUpdate` via `Input::` or `GetInputManager()`.

Mouse coordinates are **window logical pixels** (matching SDL motion/button events). Use `Window::GetFramebufferWidth/Height` for rendering/DPI scale.

## Keyboard and mouse semantics

| Query | Meaning |
| --- | --- |
| `Pressed` | Transition from up to down this frame |
| `Released` | Transition from down to up this frame |
| `Down` | Held this frame |
| `Up` | Not held |

SDL key-repeat does not generate additional `Pressed` transitions while a key stays down.

## Static API

```cpp
if (Input::IsKeyDown(Key::W)) { /* move */ }
if (Input::IsKeyPressed(Key::Escape)) { Close(); }
auto pos = Input::GetMousePosition();
auto scroll = Input::GetScrollDelta();
```

## Action mapping

```cpp
InputMapping mapping;
mapping.BindKey("Jump", Key::Space);
mapping.BindKey("Jump", Key::Up);
mapping.BindMouseButton("Fire", MouseButton::Left);

if (mapping.IsActionPressed(GetInputManager(), "Jump")) { /* jump once */ }
```

An action is active when **any** binding matches. Unknown action names return false for press/down/released queries and true for `IsActionUp`.

## YAML format (version 1)

```yaml
Version: 1
Actions:
  Jump:
    - Type: Key
      Code: Space
    - Type: Key
      Code: Up
  Fire:
    - Type: MouseButton
      Code: Left
```

Load with `InputMappingSerializer::Load`. Failed loads leave the previous mapping unchanged.

## Focus loss and shutdown

`WindowFocusLost` clears held keys and mouse buttons and emits release transitions for anything that was down. `Application::OnShutdownRequested` and `InputManager::PrepareShutdown` clear stuck state.

## Gamepad (future)

Add `InputBinding::Type::GamepadButton` and feed events from a platform gamepad translator without changing gameplay APIs.