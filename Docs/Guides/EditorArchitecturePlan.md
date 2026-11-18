# Milestone 13 — Editor architecture plan

## Goals

- Add **Xora2D-Editor** as a separate executable that reuses the existing `Application` stack (window, GL context, `Renderer2D`, `Scene`, `AssetManager`).
- Integrate **Dear ImGui v1.91.8** (docking) via SDL3 + OpenGL3 backends on the **same** window/context as the engine.
- Keep **no ImGui symbols** in public engine headers; the engine gains only small, reusable runtime pieces (`ColorRenderTarget`, optional logger sink, `Window` hooks).

## Dependency direction

```
Xora2D-Editor  →  imgui (static)  →  SDL3
              →  Xora2D::Xora2D (engine)
Xora2D-Sandbox →  Xora2D only
Tests          →  Xora2D (+ editor config / render-target tests)
```

The engine never links ImGui.

## Frame loop (editor)

1. `InputManager::BeginFrame`
2. `Window::PollEvents` (SDL hook feeds ImGui, then engine events)
3. ImGui backend `NewFrame`
4. `EditorWorkspace` / panels (dock space, menus, status bar)
5. **Viewport**: resize `ColorRenderTarget`, bind FBO, `RendererAPI` clear, `Scene::OnRender`
6. Bind default framebuffer, clear editor chrome color, ImGui `Render` + backend draw
7. `SwapBuffers`

Gameplay `Input` is suppressed when `ImGui::GetIO().WantCaptureKeyboard/Mouse` is set (after `NewFrame`), except window close/quit.

## Viewport

- `ColorRenderTarget` owns FBO + `Texture2D` color attachment (`GetRendererId()` for `ImGui::Image`).
- Resize only when panel size changes (not every frame).
- Zero/minimized size: skip GL resize and scene draw.

## Configuration

- Global: `~/Library/Application Support/Xora2D/` (macOS) — `editor.yaml` + `imgui.ini` (layout, no absolute paths).
- YAML via existing yaml-cpp dependency in editor only for config IO (engine stays unchanged).

## Panels

`EditorPanel` subclasses registered on `EditorWorkspace`; `EditorUI` builds menu/dock/status and delegates draw calls.