// Regression tests for editing TransformComponent values from the editor Inspector.
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorSceneCommands.h"
#include "Xora2DEditor/TransformEditing.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& description) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {}", description);
    ++g_failures;
}

bool Near(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

using Xora2DEditor::ApplyTransformEdit;
using Xora2DEditor::ReadTransformForEditing;
using Xora2DEditor::TransformEditValues;

// Mimics one Inspector frame: read the component, let `edit` change the copy, write back.
template <typename EditFn>
bool InspectorEdit(Xora2D::Entity entity, EditFn edit) {
    auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
    const TransformEditValues before = ReadTransformForEditing(transform);
    TransformEditValues after = before;
    edit(after);
    return ApplyTransformEdit(transform, before, after);
}

void TestFieldEdits() {
    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity entity = scene->CreateEntity("Target");
    auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
    transform.Translation = {10.0f, 20.0f};
    transform.RotationRadians = 0.25f;
    transform.Scale = {2.0f, 3.0f};
    transform.RecalculateLocalMatrix();

    const TransformEditValues read = ReadTransformForEditing(transform);
    Check(Near(read.RotationDegrees, 0.25f * 180.0f / 3.14159265f, 1e-3f), "inspector shows rotation in degrees");

    // Position X only.
    Check(InspectorEdit(entity, [](TransformEditValues& v) { v.Position.x = 150.0f; }), "position X edit reports change");
    Check(Near(transform.Translation.x, 150.0f), "editing position X updates the component");
    Check(Near(transform.Translation.y, 20.0f), "editing position X preserves Y");
    Check(Near(transform.RotationRadians, 0.25f, 0.0f), "editing position preserves rotation exactly");
    Check(Near(transform.Scale.x, 2.0f) && Near(transform.Scale.y, 3.0f), "editing position preserves scale");

    // Position Y only.
    (void)InspectorEdit(entity, [](TransformEditValues& v) { v.Position.y = -75.5f; });
    Check(Near(transform.Translation.x, 150.0f) && Near(transform.Translation.y, -75.5f),
          "editing position Y updates Y and preserves X");

    // Rotation in degrees -> radians.
    (void)InspectorEdit(entity, [](TransformEditValues& v) { v.RotationDegrees = 90.0f; });
    Check(Near(transform.RotationRadians, 1.5707963f, 1e-5f), "90 degrees is stored as pi/2 radians");
    (void)InspectorEdit(entity, [](TransformEditValues& v) { v.RotationDegrees = -180.0f; });
    Check(Near(transform.RotationRadians, -3.1415927f, 1e-5f), "-180 degrees is stored as -pi radians");
    Check(Near(transform.Translation.x, 150.0f) && Near(transform.Translation.y, -75.5f) &&
              Near(transform.Scale.x, 2.0f) && Near(transform.Scale.y, 3.0f),
          "editing rotation preserves position and scale");

    // Scale X then Y.
    (void)InspectorEdit(entity, [](TransformEditValues& v) { v.Scale.x = 0.5f; });
    Check(Near(transform.Scale.x, 0.5f) && Near(transform.Scale.y, 3.0f), "editing scale X updates X and preserves Y");
    (void)InspectorEdit(entity, [](TransformEditValues& v) { v.Scale.y = 4.0f; });
    Check(Near(transform.Scale.x, 0.5f) && Near(transform.Scale.y, 4.0f), "editing scale Y updates Y and preserves X");

    // No-op frame must not modify anything (e.g. no rotation drift through deg/rad round-trip).
    transform.RotationRadians = 0.1234567f;
    Check(!InspectorEdit(entity, [](TransformEditValues&) {}), "unchanged inspector frame reports no change");
    Check(transform.RotationRadians == 0.1234567f, "unchanged inspector frame leaves rotation bit-exact");

    // Matrix is recalculated so anything reading GetMatrix() sees the edit.
    (void)InspectorEdit(entity, [](TransformEditValues& v) {
        v.Position = {5.0f, 6.0f};
        v.RotationDegrees = 0.0f;
        v.Scale = {1.0f, 1.0f};
    });
    Check(Near(transform.GetMatrix()[3][0], 5.0f) && Near(transform.GetMatrix()[3][1], 6.0f),
          "transform matrix recalculated after edit");
}

void TestEditModeDoesNotOverwriteDynamicBodies() {
    Xora2DEditor::EditorContext context;
    auto scene = context.GetEditScene();
    Xora2D::Entity body = scene->CreateEntity("Falling");
    body.GetComponent<Xora2D::TransformComponent>().Translation = {320.0f, 240.0f};
    body.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    body.AddComponent<Xora2D::BoxCollider2DComponent>();
    Xora2DEditor::SyncPhysicsWithScene(*scene);

    // Let the editor tick a while in edit mode, then edit, then tick again.
    for (int i = 0; i < 30; ++i) {
        context.UpdatePlayMode(1.0f / 60.0f);
    }
    auto& transform = body.GetComponent<Xora2D::TransformComponent>();
    Check(Near(transform.Translation.y, 240.0f), "edit mode does not let gravity move a dynamic body");

    (void)InspectorEdit(body, [](TransformEditValues& v) {
        v.Position = {100.0f, 400.0f};
        v.RotationDegrees = 45.0f;
        v.Scale = {2.0f, 2.0f};
    });
    for (int i = 0; i < 30; ++i) {
        context.UpdatePlayMode(1.0f / 60.0f);
    }
    Check(Near(transform.Translation.x, 100.0f) && Near(transform.Translation.y, 400.0f),
          "inspector position edit survives editor updates on a dynamic body");
    Check(Near(transform.RotationRadians, 0.7853982f, 1e-5f), "inspector rotation edit survives editor updates");
    Check(Near(transform.Scale.x, 2.0f) && Near(transform.Scale.y, 2.0f), "inspector scale edit survives editor updates");

    // Play mode simulates the runtime copy only; the edited values come back after Stop.
    Check(context.StartPlayMode(), "play mode starts");
    for (int i = 0; i < 30; ++i) {
        context.UpdatePlayMode(1.0f / 60.0f);
    }
    const Xora2D::Entity runtimeBody = context.GetScene()->FindEntityByUUID(body.GetUUID());
    Check(runtimeBody.IsValid() && runtimeBody.GetComponent<Xora2D::TransformComponent>().Translation.y < 400.0f,
          "play mode simulates dynamic bodies");
    Check(Near(transform.Translation.y, 400.0f), "play mode leaves the edited transform untouched");
    Check(context.StopPlayMode(), "play mode stops");
}

void TestSelectionRoundTripAndSerialization() {
    Xora2DEditor::EditorContext context;
    auto scene = context.GetScene();
    Xora2D::Entity a = Xora2DEditor::CreateEmptyEntity(*scene, "Alpha");
    Xora2D::Entity b = Xora2DEditor::CreateEmptyEntity(*scene, "Bravo");

    context.SelectEntity(a);
    (void)InspectorEdit(context.GetSelectedEntity(), [](TransformEditValues& v) {
        v.Position = {12.0f, 34.0f};
        v.RotationDegrees = 30.0f;
        v.Scale = {1.5f, 0.5f};
    });
    context.SelectEntity(b);
    (void)InspectorEdit(context.GetSelectedEntity(), [](TransformEditValues& v) { v.Position.x = -1.0f; });
    context.UpdatePlayMode(1.0f / 60.0f);
    context.SelectEntity(a);

    const auto& ta = context.GetSelectedEntity().GetComponent<Xora2D::TransformComponent>();
    Check(context.GetSelectedEntity().GetUUID() == a.GetUUID(), "reselecting resolves the same entity");
    Check(Near(ta.Translation.x, 12.0f) && Near(ta.Translation.y, 34.0f), "position persists after reselect");
    Check(Near(ReadTransformForEditing(ta).RotationDegrees, 30.0f, 1e-3f), "rotation persists after reselect");
    Check(Near(ta.Scale.x, 1.5f) && Near(ta.Scale.y, 0.5f), "scale persists after reselect");
    const auto& tb = b.GetComponent<Xora2D::TransformComponent>();
    Check(Near(tb.Translation.x, -1.0f) && Near(tb.Translation.y, 0.0f), "editing another entity does not leak edits");

    // Serialization stays radians-based and round-trips the edited values.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "xora2d_transform_edit_test.scene.yaml";
    const auto saved = Xora2D::SceneSerializer::Serialize(*scene, path);
    Check(saved.Success, "scene with inspector edits serializes");
    auto loaded = Xora2D::Scene::Create();
    const auto result = Xora2D::SceneSerializer::Deserialize(*loaded, path);
    Check(result.Success, "scene with inspector edits deserializes");
    Xora2D::Entity reloaded = loaded->FindEntityByUUID(a.GetUUID());
    Check(reloaded.IsValid(), "edited entity reloaded");
    if (reloaded.IsValid()) {
        const auto& tr = reloaded.GetComponent<Xora2D::TransformComponent>();
        Check(Near(tr.Translation.x, 12.0f) && Near(tr.Translation.y, 34.0f) &&
                  Near(tr.RotationRadians, 0.5235988f, 1e-5f) && Near(tr.Scale.x, 1.5f) && Near(tr.Scale.y, 0.5f),
              "serialized transform matches inspector edits (rotation stored as radians)");
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Renders the scene the way the editor viewport does (off-screen target + Scene::OnRender) and samples pixels.
void TestViewportReflectsEdits() {
    Xora2D::WindowProperties props{.title = "TransformViewportTest", .width = 64, .height = 64, .enableOpenGL = true};
    Xora2D::Window window(props);
    std::unique_ptr<Xora2D::GraphicsContext> gl = window.CreateGraphicsContext();
    gl->MakeCurrent();

    constexpr int kSize = 128;
    auto target = Xora2D::ColorRenderTarget::Create();
    if (!target->Resize(kSize, kSize)) {
        Check(false, "viewport test render target created");
        return;
    }
    Xora2D::Renderer2D renderer;
    renderer.Init();

    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity camera = scene->CreateEntity("Camera");
    camera.AddComponent<Xora2D::CameraComponent>().Primary = true; // camera at origin -> bottom-left of the view
    Xora2D::Entity sprite = scene->CreateEntity("Sprite");
    auto& renderer2d = sprite.AddComponent<Xora2D::SpriteRendererComponent>();
    renderer2d.Color = {1.0f, 0.0f, 0.0f, 1.0f};
    renderer2d.Size = {60.0f, 10.0f};
    sprite.GetComponent<Xora2D::TransformComponent>().Translation = {64.0f, 64.0f};

    auto isSpriteAt = [&](int x, int y) {
        scene->SetViewportSize(static_cast<float>(kSize), static_cast<float>(kSize));
        target->Bind();
        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        scene->OnRender(renderer);
        std::uint8_t pixel[4] = {};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        target->Unbind();
        return pixel[0] > 200 && pixel[2] < 50;
    };

    Check(isSpriteAt(64, 64), "viewport draws sprite at its initial position");
    Check(!isSpriteAt(64, 84), "viewport background above a flat sprite");

    (void)InspectorEdit(sprite, [](TransformEditValues& v) { v.RotationDegrees = 90.0f; });
    Check(isSpriteAt(64, 84), "viewport reflects inspector rotation (sprite now vertical)");

    (void)InspectorEdit(sprite, [](TransformEditValues& v) { v.RotationDegrees = 0.0f; });
    Check(!isSpriteAt(64, 74), "viewport background just above an unscaled sprite");
    (void)InspectorEdit(sprite, [](TransformEditValues& v) { v.Scale.y = 3.0f; });
    Check(isSpriteAt(64, 74), "viewport reflects inspector scale");

    (void)InspectorEdit(sprite, [](TransformEditValues& v) { v.Position.x = 20.0f; });
    Check(!isSpriteAt(100, 64), "viewport no longer draws sprite at the old position");
    Check(isSpriteAt(20, 64), "viewport reflects inspector position");

    renderer.Shutdown();
}

// The editor's ImGui backend receives SDL events through Window's raw event hook. Typing into any Inspector field
// (names, or double-click / Ctrl+click on a number) depends on SDL_EVENT_TEXT_INPUT reaching that hook; it used to be
// discarded by the engine's window-event filter before the hook ran.
void TestWindowForwardsUiEventsToHook() {
    Xora2D::WindowProperties props{.title = "EventHookTest", .width = 64, .height = 64, .enableOpenGL = false};
    Xora2D::Window window(props);
    auto* sdlWindow = static_cast<SDL_Window*>(window.GetNativeWindowHandle());
    std::vector<std::uint32_t> seen;
    std::string text;
    window.SetSdlEventHook([&](const void* raw) {
        const auto* event = static_cast<const SDL_Event*>(raw);
        seen.push_back(event->type);
        if (event->type == SDL_EVENT_TEXT_INPUT && event->text.text != nullptr) {
            text += event->text.text;
        }
    });
    window.PollEvents(); // drain startup events
    seen.clear();

    SDL_StartTextInput(sdlWindow); // what the ImGui backend does while a text field is active
    SDL_Event textEvent{};
    textEvent.type = SDL_EVENT_TEXT_INPUT;
    textEvent.text.windowID = SDL_GetWindowID(sdlWindow);
    textEvent.text.text = "42.5";
    Check(SDL_PushEvent(&textEvent), "text input event queued");

    SDL_Event leaveEvent{};
    leaveEvent.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
    leaveEvent.window.windowID = SDL_GetWindowID(sdlWindow);
    (void)SDL_PushEvent(&leaveEvent);

    window.PollEvents();
    SDL_StopTextInput(sdlWindow);
    window.SetSdlEventHook(nullptr);

    auto saw = [&](std::uint32_t type) {
        for (const std::uint32_t t : seen) {
            if (t == type) {
                return true;
            }
        }
        return false;
    };
    Check(saw(SDL_EVENT_TEXT_INPUT) && text == "42.5", "window forwards text input to the UI event hook");
    Check(saw(SDL_EVENT_WINDOW_MOUSE_LEAVE), "window forwards mouse-leave to the UI event hook");
}

} // namespace

int RunTransformInspectorTests() {
    XORA_LOG_INFO("TEST transform inspector started");
    const int before = g_failures;
    TestFieldEdits();
    TestEditModeDoesNotOverwriteDynamicBodies();
    TestSelectionRoundTripAndSerialization();
    TestViewportReflectsEdits();
    TestWindowForwardsUiEventsToHook();
    XORA_LOG_INFO("TEST transform inspector finished with {} failure(s)", g_failures - before);
    return g_failures - before;
}
