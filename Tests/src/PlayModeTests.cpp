// Milestone 16: editor Play / Pause / Step / Stop with an isolated runtime scene.
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Audio/AudioEngine.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorPlaySession.h"
#include "Xora2DEditor/EditorSceneCommands.h"

#include <glad/gl.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using Xora2DEditor::EditorContext;
using Xora2DEditor::EditorPlayState;

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

constexpr float kFrame = 1.0f / 60.0f;

std::string ReadText(const fs::path& path) {
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Serialized YAML is a complete, order-independent description of a scene's persistent data (entities are sorted
// by UUID), so equal strings mean "restored exactly".
std::string Snapshot(const Xora2D::Scene& scene) {
    static int counter = 0;
    const fs::path path = fs::temp_directory_path() / ("xora2d_playmode_snapshot_" + std::to_string(++counter) + ".yaml");
    const auto result = Xora2D::SceneSerializer::Serialize(scene, path);
    std::string text = result.Success ? ReadText(path) : std::string("<serialize failed: ") + result.Message + ">";
    std::error_code ec;
    fs::remove(path, ec);
    return text;
}

struct DemoIds {
    Xora2D::UUID Camera;
    Xora2D::UUID Box;
    Xora2D::UUID Ground;
    Xora2D::UUID Marker;
};

DemoIds BuildDemoScene(Xora2D::Scene& scene) {
    scene.SetSceneName("PlayModeDemo");
    Xora2D::Entity camera = scene.CreateEntity("Camera");
    camera.AddComponent<Xora2D::CameraComponent>().Primary = true;

    Xora2D::Entity box = scene.CreateEntity("Box");
    auto& boxTransform = box.GetComponent<Xora2D::TransformComponent>();
    boxTransform.Translation = {320.0f, 400.0f};
    boxTransform.RecalculateLocalMatrix();
    box.AddComponent<Xora2D::SpriteRendererComponent>().Color = {0.9f, 0.3f, 0.2f, 1.0f};
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    box.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};

    Xora2D::Entity ground = scene.CreateEntity("Ground");
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {320.0f, 20.0f};
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {640.0f, 40.0f};

    Xora2D::Entity marker = scene.CreateEntity("Marker");
    marker.GetComponent<Xora2D::TransformComponent>().Translation = {5.0f, 6.0f};
    return {camera.GetUUID(), box.GetUUID(), ground.GetUUID(), marker.GetUUID()};
}

float BoxY(const Xora2D::Scene& scene, const Xora2D::UUID& id) {
    const Xora2D::Entity entity = scene.FindEntityByUUID(id);
    return entity.IsValid() ? entity.GetComponent<Xora2D::TransformComponent>().Translation.y : -99999.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// Scene isolation
// ---------------------------------------------------------------------------------------------------------------

void TestSceneCopyIsolation() {
    auto edit = Xora2D::Scene::Create();
    const DemoIds ids = BuildDemoScene(*edit);
    Xora2D::PhysicsWorld2D::Settings settings = edit->GetPhysicsWorld().GetSettings();
    settings.Gravity = {0.0f, -20.0f};
    edit->GetPhysicsWorld().Configure(settings);
    edit->SetViewportSize(800.0f, 600.0f);
    const std::string before = Snapshot(*edit);

    std::shared_ptr<Xora2D::Scene> runtime = Xora2D::Scene::Copy(*edit);
    Check(runtime != nullptr && runtime.get() != edit.get(), "runtime copy is a distinct scene instance");
    Check(runtime->GetEntityCount() == edit->GetEntityCount(), "runtime copy has the same entity count");
    Check(Snapshot(*runtime) == before, "runtime copy serializes identically (all components and UUIDs copied)");
    Check(runtime->GetSceneName() == "PlayModeDemo", "runtime copy keeps the scene name");
    Check(Near(runtime->GetViewportWidth(), 800.0f) && Near(runtime->GetViewportHeight(), 600.0f),
          "runtime copy keeps the viewport size");
    Check(Near(runtime->GetPhysicsWorld().GetSettings().Gravity.y, -20.0f), "runtime copy keeps physics settings");

    // Same UUIDs, separate ownership.
    const Xora2D::Entity editBox = edit->FindEntityByUUID(ids.Box);
    const Xora2D::Entity runtimeBox = runtime->FindEntityByUUID(ids.Box);
    Check(runtimeBox.IsValid() && runtimeBox.GetUUID() == editBox.GetUUID(), "runtime entity keeps its UUID");
    Check(runtimeBox.GetName() == "Box", "runtime entity keeps its name");
    Check(runtime->IsValid(runtimeBox) && !edit->IsValid(runtimeBox), "runtime entity is owned by the runtime scene");
    Check(edit->IsValid(editBox) && !runtime->IsValid(editBox), "editable entity is not owned by the runtime scene");

    // Mutating the runtime copy never touches the editable scene.
    auto& runtimeTransform = runtime->FindEntityByUUID(ids.Box).GetComponent<Xora2D::TransformComponent>();
    runtimeTransform.Translation = {1.0f, 2.0f};
    runtimeTransform.RecalculateLocalMatrix();
    runtime->FindEntityByUUID(ids.Box).GetComponent<Xora2D::SpriteRendererComponent>().Color = {0, 0, 1, 1};
    Xora2D::Entity runtimeMarker = runtime->FindEntityByUUID(ids.Marker);
    runtimeMarker.AddComponent<Xora2D::CameraComponent>();
    (void)Xora2DEditor::RenameEntity(runtimeMarker, "RenamedAtRuntime");
    runtime->DestroyEntity(runtime->FindEntityByUUID(ids.Ground));
    const Xora2D::Entity spawned = runtime->CreateEntity("SpawnedAtRuntime");
    runtime->OnUpdate(0.25f);

    Check(Snapshot(*edit) == before, "runtime transform/component/name/destroy/create changes leave the edit scene intact");
    Check(edit->FindEntityByUUID(ids.Ground).IsValid(), "entity destroyed at runtime still exists in the edit scene");
    Check(!edit->FindEntityByUUID(spawned.GetUUID()).IsValid(), "entity created at runtime is absent from the edit scene");
    Check(!edit->FindEntityByName("SpawnedAtRuntime").IsValid(), "runtime-created name not indexed in the edit scene");
    Check(Near(BoxY(*edit, ids.Box), 400.0f), "edit transform unchanged after runtime simulation");

    // A copy made later still works after the source changes again.
    edit->CreateEntity("AddedLater");
    auto second = Xora2D::Scene::Copy(*edit);
    Check(second->FindEntityByName("AddedLater").IsValid(), "a new copy picks up later edit-scene changes");
}

// Copies keep creation order, so overlapping sprites draw identically in play mode.
void TestCopyPreservesDrawOrder() {
    Xora2D::WindowProperties props{.title = "PlayModeDrawOrder", .width = 64, .height = 64, .enableOpenGL = true};
    Xora2D::Window window(props);
    std::unique_ptr<Xora2D::GraphicsContext> gl = window.CreateGraphicsContext();
    gl->MakeCurrent();
    auto target = Xora2D::ColorRenderTarget::Create();
    if (!target->Resize(64, 64)) {
        Check(false, "draw-order render target");
        return;
    }
    Xora2D::Renderer2D renderer;
    renderer.Init();

    auto edit = Xora2D::Scene::Create();
    edit->CreateEntity("Camera").AddComponent<Xora2D::CameraComponent>().Primary = true;
    const glm::vec4 colors[3] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    const char* names[3] = {"First", "Second", "Third"};
    for (int i = 0; i < 3; ++i) {
        Xora2D::Entity sprite = edit->CreateEntity(names[i]);
        sprite.GetComponent<Xora2D::TransformComponent>().Translation = {32.0f, 32.0f};
        auto& renderer2d = sprite.AddComponent<Xora2D::SpriteRendererComponent>();
        renderer2d.Color = colors[i];
        renderer2d.Size = {20.0f, 20.0f};
    }
    auto centerPixel = [&](Xora2D::Scene& scene) {
        scene.SetViewportSize(64.0f, 64.0f);
        target->Bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        scene.OnRender(renderer);
        std::uint8_t pixel[4] = {};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        target->Unbind();
        return (static_cast<std::uint32_t>(pixel[0]) << 16) | (static_cast<std::uint32_t>(pixel[1]) << 8) | pixel[2];
    };
    const std::uint32_t editPixel = centerPixel(*edit);
    auto runtime = Xora2D::Scene::Copy(*edit);
    const std::uint32_t runtimePixel = centerPixel(*runtime);
    XORA_LOG_INFO("  draw order: edit={:06x} runtime={:06x}", editPixel, runtimePixel);
    Check(editPixel != 0 && editPixel == runtimePixel, "runtime copy renders overlapping sprites in the same order");
    renderer.Shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------------------------------------------

void TestStateTransitions() {
    EditorContext context;
    int inputClears = 0;
    context.GetPlaySession().SetHooks({.WireRuntimeScene = {}, .ClearGameplayInput = [&]() { ++inputClears; }});
    BuildDemoScene(*context.GetEditScene());
    const std::string before = Snapshot(*context.GetEditScene());
    auto& session = context.GetPlaySession();

    Check(context.GetPlayState() == EditorPlayState::Edit && !context.IsPlayModeActive(), "starts in edit mode");
    Check(context.GetScene() == context.GetEditScene(), "edit mode shows the editable scene");
    Check(!context.PausePlayMode() && !context.ResumePlayMode() && !context.StepPlayMode() && !context.StopPlayMode(),
          "pause/resume/step/stop are rejected in edit mode");

    Check(context.StartPlayMode() && context.GetPlayState() == EditorPlayState::Playing, "Edit -> Play");
    Check(context.GetScene() == session.GetRuntimeScene() && context.GetScene() != context.GetEditScene(),
          "play mode shows the runtime scene");
    Check(!context.StartPlayMode() && !context.ResumePlayMode() && !context.StepPlayMode(),
          "play/resume/step are rejected while playing");

    Check(context.PausePlayMode() && context.GetPlayState() == EditorPlayState::Paused, "Play -> Pause");
    Check(!context.PausePlayMode() && !context.StartPlayMode(), "pause/play are rejected while paused");
    Check(context.ResumePlayMode() && context.GetPlayState() == EditorPlayState::Playing, "Pause -> Resume");

    (void)context.PausePlayMode();
    const std::uint64_t framesBeforeStep = session.GetSimulatedFrameCount();
    Check(context.StepPlayMode(), "Pause -> Step accepted");
    Check(!context.StepPlayMode(), "a second step request before the frame runs is rejected (no double step)");
    context.UpdatePlayMode(kFrame);
    Check(context.GetPlayState() == EditorPlayState::Paused, "Step keeps the editor paused");
    Check(session.GetSimulatedFrameCount() == framesBeforeStep + 1, "Step advances exactly one simulation frame");
    context.UpdatePlayMode(kFrame);
    Check(session.GetSimulatedFrameCount() == framesBeforeStep + 1, "no further frames run while paused");

    Check(context.StopPlayMode() && context.GetPlayState() == EditorPlayState::Edit, "Pause -> Stop");
    Check(context.GetScene() == context.GetEditScene(), "stop returns to the editable scene");
    Check(context.StartPlayMode() && context.StopPlayMode(), "Play -> Stop");

    std::weak_ptr<Xora2D::Scene> previousRuntime;
    bool distinctEachTime = true;
    for (int cycle = 0; cycle < 5; ++cycle) {
        (void)context.StartPlayMode();
        if (!previousRuntime.expired()) {
            distinctEachTime = false;
        }
        for (int frame = 0; frame < 10; ++frame) {
            context.UpdatePlayMode(kFrame);
        }
        previousRuntime = session.GetRuntimeScene();
        (void)context.StopPlayMode();
    }
    Check(distinctEachTime && previousRuntime.expired(), "multiple play/stop cycles each create and destroy a runtime scene");
    Check(Snapshot(*context.GetEditScene()) == before, "edit scene identical after repeated play sessions");
    Check(inputClears >= 2 * 7, "gameplay input is cleared on play, pause and stop");

    // Replacing the edited scene while playing ends the session first.
    (void)context.StartPlayMode();
    std::weak_ptr<Xora2D::Scene> runtime = session.GetRuntimeScene();
    context.SetScene(Xora2D::Scene::Create());
    Check(!context.IsPlayModeActive() && runtime.expired(), "scene replacement while playing stops play mode");
}

// ---------------------------------------------------------------------------------------------------------------
// Simulation: physics, animation
// ---------------------------------------------------------------------------------------------------------------

void TestPhysicsSimulation() {
    EditorContext context;
    const DemoIds ids = BuildDemoScene(*context.GetEditScene());
    auto& session = context.GetPlaySession();

    for (int frame = 0; frame < 30; ++frame) {
        context.UpdatePlayMode(kFrame);
    }
    Check(Near(BoxY(*context.GetEditScene(), ids.Box), 400.0f), "edit mode does not simulate physics");

    (void)context.StartPlayMode();
    const auto& runtime = session.GetRuntimeScene();
    Check(runtime->GetPhysicsWorld().HasPhysicsBody(runtime->FindEntityByUUID(ids.Box)),
          "runtime physics bodies exist after Play");
    for (int frame = 0; frame < 20; ++frame) {
        context.UpdatePlayMode(kFrame);
    }
    const float playedY = BoxY(*runtime, ids.Box);
    Check(playedY < 400.0f, "physics advances while playing");
    Check(Near(BoxY(*context.GetEditScene(), ids.Box), 400.0f), "physics never moves the edit-scene body");
    Check(!context.GetEditScene()->GetPhysicsWorld().HasPhysicsBody(context.GetEditScene()->FindEntityByUUID(ids.Box)),
          "the edit scene has no simulated body");

    (void)context.PausePlayMode();
    for (int frame = 0; frame < 30; ++frame) {
        context.UpdatePlayMode(kFrame);
    }
    Check(BoxY(*runtime, ids.Box) == playedY, "physics does not advance while paused");

    // Step: exactly one fixed physics step even if the editor frame delta is huge.
    (void)context.StepPlayMode();
    session.Update(5.0f);
    Check(runtime->GetPhysicsWorld().GetLastSubStepCount() == 1, "step runs exactly one physics fixed step");
    const float steppedY = BoxY(*runtime, ids.Box);
    Check(steppedY < playedY, "step moves the simulation forward");
    for (int frame = 0; frame < 10; ++frame) {
        context.UpdatePlayMode(kFrame);
    }
    Check(BoxY(*runtime, ids.Box) == steppedY, "simulation stays frozen after the step");

    (void)context.ResumePlayMode();
    context.UpdatePlayMode(kFrame * 2.0f);
    Check(BoxY(*runtime, ids.Box) < steppedY, "physics continues after resume");

    std::weak_ptr<Xora2D::Scene> weakRuntime = runtime;
    (void)context.StopPlayMode();
    Check(weakRuntime.expired(), "runtime scene (and its physics world/bodies) is destroyed on Stop");
    Check(Near(BoxY(*context.GetEditScene(), ids.Box), 400.0f), "edit transform restored exactly after Stop");
}

void TestAnimationSimulation() {
    EditorContext context;
    auto& assets = context.GetAssetManager();
    assets.SetGraphicsResourcesEnabled(false);
    (void)assets.RegisterTextureAtRelativePath("Textures/DemoSpriteSheet.png", "DemoSpriteSheet");
    const Xora2D::AssetResult walk = assets.RegisterAnimationClipAtRelativePath("Animations/DemoWalk.anim.yaml", "Walk");
    Check(walk.Success, "register demo walk clip");
    if (!walk.Success) {
        return;
    }
    Xora2D::Entity animated = context.GetEditScene()->CreateEntity("Animated");
    animated.AddComponent<Xora2D::SpriteRendererComponent>();
    auto& editAnimation = animated.AddComponent<Xora2D::AnimationComponent>();
    editAnimation.ClipAssets = {walk.Handle};
    editAnimation.DefaultClip = walk.Handle;
    editAnimation.Looping = true;
    editAnimation.Autoplay = true;

    constexpr float kClipFrame = 0.13f; // DemoWalk uses 0.12 s per frame
    for (int frame = 0; frame < 5; ++frame) {
        context.UpdatePlayMode(kClipFrame);
    }
    Check(editAnimation.State == Xora2D::AnimationPlaybackState::Stopped && editAnimation.CurrentFrameIndex == 0,
          "edit mode does not advance animations");

    (void)context.StartPlayMode();
    auto& runtimeAnimation =
        context.GetScene()->FindEntityByUUID(animated.GetUUID()).GetComponent<Xora2D::AnimationComponent>();
    context.UpdatePlayMode(kClipFrame);
    const int first = runtimeAnimation.CurrentFrameIndex;
    context.UpdatePlayMode(kClipFrame);
    Check(runtimeAnimation.State == Xora2D::AnimationPlaybackState::Playing && runtimeAnimation.CurrentFrameIndex != first,
          "animations advance while playing");

    (void)context.PausePlayMode();
    const int pausedFrame = runtimeAnimation.CurrentFrameIndex;
    for (int frame = 0; frame < 10; ++frame) {
        context.UpdatePlayMode(kClipFrame);
    }
    Check(runtimeAnimation.CurrentFrameIndex == pausedFrame, "animations do not advance while paused");

    (void)context.ResumePlayMode();
    context.UpdatePlayMode(kClipFrame);
    Check(runtimeAnimation.CurrentFrameIndex != pausedFrame, "animations continue after resume");

    (void)context.StopPlayMode();
    Check(editAnimation.State == Xora2D::AnimationPlaybackState::Stopped && editAnimation.CurrentFrameIndex == 0,
          "edit-scene animation state untouched by play mode");
}

// ---------------------------------------------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------------------------------------------

void TestRuntimeAudio() {
    Xora2D::AudioEngine audio; // fresh engine: the null backend hands out playback ids 1, 2, ...
    Xora2D::AudioEngineSettings settings;
    settings.Backend = Xora2D::AudioBackendKind::Null;
    Check(audio.Initialize(settings).Success, "null audio engine initializes");

    EditorContext context;
    context.GetPlaySession().SetHooks({.WireRuntimeScene = [&](Xora2D::Scene& scene) { scene.SetAudioEngine(&audio); },
                                       .ClearGameplayInput = {}});
    const Xora2D::AssetResult beep = context.GetAssetManager().RegisterAudioAtRelativePath("Audio/sfx_beep.wav", "Beep");
    Check(beep.Success, "register beep audio");
    Xora2D::Entity speaker = context.GetEditScene()->CreateEntity("Speaker");
    auto& source = speaker.AddComponent<Xora2D::AudioSourceComponent>();
    source.ClipAsset = beep.Handle;
    source.PlayOnStart = true;
    source.Loop = true;

    constexpr Xora2D::AudioPlaybackId kFirstVoice = 1;
    for (int frame = 0; frame < 5; ++frame) {
        context.UpdatePlayMode(kFrame);
    }
    Check(!audio.IsPlaybackActive(kFirstVoice), "edit mode never plays scene audio");

    (void)context.StartPlayMode();
    context.UpdatePlayMode(kFrame);
    const Xora2D::Entity runtimeSpeaker = context.GetScene()->FindEntityByUUID(speaker.GetUUID());
    Check(audio.IsPlaybackActive(kFirstVoice) && context.GetScene()->IsEntityAudioPlaying(runtimeSpeaker),
          "runtime audio source plays on start");

    (void)context.PausePlayMode();
    Check(audio.IsPlaybackPaused(kFirstVoice) && !context.GetScene()->IsEntityAudioPlaying(runtimeSpeaker),
          "pausing play mode pauses runtime audio");
    (void)context.StepPlayMode();
    context.UpdatePlayMode(kFrame);
    Check(audio.IsPlaybackPaused(kFirstVoice), "runtime audio stays paused across a step");

    (void)context.ResumePlayMode();
    Check(audio.IsPlaybackActive(kFirstVoice) && !audio.IsPlaybackPaused(kFirstVoice), "resuming play mode resumes audio");

    (void)context.StopPlayMode();
    Check(!audio.IsPlaybackActive(kFirstVoice), "stopping play mode stops runtime audio");

    // PlayOnStart fires again in the next session (state lives in the runtime scene, not the edit scene).
    (void)context.StartPlayMode();
    context.UpdatePlayMode(kFrame);
    Check(audio.IsPlaybackActive(kFirstVoice + 1), "a new session starts its own audio");
    (void)context.StopPlayMode();
    Check(!audio.IsPlaybackActive(kFirstVoice + 1), "second session audio stopped");
    audio.Shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// Selection, input gate
// ---------------------------------------------------------------------------------------------------------------

void TestSelectionSafety() {
    EditorContext context;
    const DemoIds ids = BuildDemoScene(*context.GetEditScene());
    const Xora2D::Entity editBox = context.GetEditScene()->FindEntityByUUID(ids.Box);
    context.SelectEntity(editBox);

    (void)context.StartPlayMode();
    const Xora2D::Entity selectedAtRuntime = context.GetSelectedEntity();
    Check(selectedAtRuntime.IsValid() && context.GetScene()->IsValid(selectedAtRuntime) &&
              !context.GetEditScene()->IsValid(selectedAtRuntime),
          "selection resolves to the runtime copy while playing");

    Xora2D::Entity spawned = Xora2DEditor::CreateEmptyEntity(*context.GetScene(), "RuntimeOnly");
    context.SelectEntity(spawned);
    (void)context.StopPlayMode();
    Check(!context.GetSelection().HasSelection() && !context.GetSelectedEntity().IsValid(),
          "runtime-only selection is cleared on Stop");

    context.SelectEntity(editBox);
    (void)context.StartPlayMode();
    (void)context.StopPlayMode();
    const Xora2D::Entity afterStop = context.GetSelectedEntity();
    Check(afterStop.IsValid() && context.GetEditScene()->IsValid(afterStop) && afterStop.GetUUID() == ids.Box,
          "selection of an edit entity survives play and resolves to the edit entity");

    (void)context.StartPlayMode();
    context.SelectEntity(context.GetScene()->FindEntityByUUID(ids.Marker));
    (void)Xora2DEditor::DestroyEntity(*context.GetScene(), context.GetSelection(), ids.Marker);
    Check(!context.GetSelectedEntity().IsValid(), "destroying the selected runtime entity clears the selection");
    (void)context.StopPlayMode();
    Check(context.GetEditScene()->FindEntityByUUID(ids.Marker).IsValid(), "runtime-destroyed entity restored on Stop");
}

void TestGameplayInputGate() {
    using Xora2DEditor::ComputeGameplayInputGate;
    const auto playingFocused = ComputeGameplayInputGate(EditorPlayState::Playing, true, true, false);
    Check(playingFocused.Keyboard && playingFocused.Mouse, "input reaches the game while playing with the viewport focused");
    const auto unfocused = ComputeGameplayInputGate(EditorPlayState::Playing, false, true, false);
    Check(!unfocused.Keyboard && !unfocused.Mouse, "no game input when the viewport is not focused");
    const auto notHovered = ComputeGameplayInputGate(EditorPlayState::Playing, true, false, false);
    Check(notHovered.Keyboard && !notHovered.Mouse, "mouse input only while hovering the game view");
    const auto typing = ComputeGameplayInputGate(EditorPlayState::Playing, true, true, true);
    Check(!typing.Keyboard, "keyboard input withheld while an editor text field is active");
    const auto paused = ComputeGameplayInputGate(EditorPlayState::Paused, true, true, false);
    const auto edit = ComputeGameplayInputGate(EditorPlayState::Edit, true, true, false);
    Check(!paused.Keyboard && !paused.Mouse && !edit.Keyboard && !edit.Mouse, "no game input in edit or paused mode");
}

// ---------------------------------------------------------------------------------------------------------------
// Python scripts (scripting builds only), in a temporary project so no files are written into Resources/
// ---------------------------------------------------------------------------------------------------------------

#if XORA2D_SCRIPTING_ENABLED
class ScopedTempProject {
public:
    ScopedTempProject() {
        std::mt19937_64 generator{std::random_device{}()};
        m_root = fs::temp_directory_path() / ("xora2d_playmode_" + std::to_string(generator() % 1000000));
        fs::create_directories(m_root / "Assets/Scripts");
        if (const char* previous = std::getenv("XORA2D_RESOURCES")) {
            m_previous = previous;
        }
        ::setenv("XORA2D_RESOURCES", m_root.string().c_str(), 1);
    }
    ~ScopedTempProject() {
        if (m_previous.has_value()) {
            ::setenv("XORA2D_RESOURCES", m_previous->c_str(), 1);
        } else {
            ::unsetenv("XORA2D_RESOURCES");
        }
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    [[nodiscard]] const fs::path& Root() const { return m_root; }

private:
    fs::path m_root;
    std::optional<std::string> m_previous;
};

void TestPythonScripts() {
    ScopedTempProject project;
    const fs::path statePath = project.Root() / "probe_state.txt";
    {
        std::ofstream script(project.Root() / "Assets/Scripts/PlayProbe.py");
        script << R"(
class PlayProbe:
    def _append(self, token):
        with open(self.state_path, "a") as handle:
            handle.write(token)
    def on_create(self):
        self._append("c")
    def on_update(self, dt):
        self._append("u")
        position = self.entity.transform.position
        self.entity.transform.set_position(position.x + 10.0, position.y)
    def on_destroy(self):
        self._append("d")
)";
    }
    std::ofstream(statePath).close();

    Xora2D::PythonScriptEngine engine;
    const Xora2D::ScriptLoadResult init = engine.Initialize();
    Check(init.Success, "python engine initializes for play-mode tests");
    if (!init.Success) {
        return;
    }

    std::weak_ptr<Xora2D::Scene> weakRuntime;
    Xora2D::UUID probeId;
    {
        EditorContext context;
        context.GetPlaySession().SetHooks({.WireRuntimeScene =
                                               [&](Xora2D::Scene& scene) {
                                                   scene.SetPythonScriptEngine(&engine);
                                                   Xora2D::ScriptServices services{};
                                                   services.Assets = &context.GetAssetManager();
                                                   engine.SetServices(services);
                                               },
                                           .ClearGameplayInput = {}});
        const Xora2D::AssetResult script =
            context.GetAssetManager().RegisterPythonScriptAtRelativePath("Scripts/PlayProbe.py", "PlayProbe");
        Check(script.Success, "register play probe script");
        Xora2D::Entity probe = context.GetEditScene()->CreateEntity("Probe");
        probeId = probe.GetUUID();
        auto& component = probe.AddComponent<Xora2D::PythonScriptComponent>();
        component.ScriptAsset = script.Handle;
        component.ClassName = "PlayProbe";
        component.Properties = {Xora2D::ScriptProperty{
            .Name = "state_path", .Type = Xora2D::ScriptPropertyType::String, .StringValue = statePath.string()}};
        const std::string editBefore = Snapshot(*context.GetEditScene());

        for (int frame = 0; frame < 5; ++frame) {
            context.UpdatePlayMode(kFrame);
        }
        Check(ReadText(statePath).empty(), "scripts never run in edit mode");

        (void)context.StartPlayMode();
        for (int frame = 0; frame < 3; ++frame) {
            context.UpdatePlayMode(kFrame);
        }
        Check(ReadText(statePath) == "cuuu", "scripts are created and updated while playing");
        const auto& runtimeTransform =
            context.GetScene()->FindEntityByUUID(probeId).GetComponent<Xora2D::TransformComponent>();
        Check(Near(runtimeTransform.Translation.x, 30.0f), "script moves the runtime entity");

        (void)context.PausePlayMode();
        for (int frame = 0; frame < 5; ++frame) {
            context.UpdatePlayMode(kFrame);
        }
        Check(ReadText(statePath) == "cuuu", "scripts do not update while paused");
        (void)context.StepPlayMode();
        context.UpdatePlayMode(kFrame);
        Check(ReadText(statePath) == "cuuuu", "step runs exactly one script update");
        (void)context.ResumePlayMode();
        context.UpdatePlayMode(kFrame);
        Check(ReadText(statePath) == "cuuuuu", "scripts update again after resume");

        (void)context.StopPlayMode();
        Check(ReadText(statePath) == "cuuuuud", "on_destroy runs exactly once on Stop");
        Check(Snapshot(*context.GetEditScene()) == editBefore, "script changes do not persist into the edit scene");

        // Second session: a fresh instance (on_create again), then close the editor while playing.
        std::ofstream(statePath).close();
        (void)context.StartPlayMode();
        context.UpdatePlayMode(kFrame);
        weakRuntime = context.GetPlaySession().GetRuntimeScene();
        Check(ReadText(statePath) == "cu", "a new play session creates new script instances");
    } // EditorContext destroyed while playing (editor closed during Play)
    Check(ReadText(statePath) == "cud", "closing the editor during play runs on_destroy once");
    Check(weakRuntime.expired(), "closing the editor during play releases the runtime scene");
    engine.Shutdown();
}
#endif

} // namespace

int RunPlayModeTests() {
    XORA_LOG_INFO("TEST play mode started");
    const int before = g_failures;
    TestSceneCopyIsolation();
    TestCopyPreservesDrawOrder();
    TestStateTransitions();
    TestPhysicsSimulation();
    TestAnimationSimulation();
    TestRuntimeAudio();
    TestSelectionSafety();
    TestGameplayInputGate();
#if XORA2D_SCRIPTING_ENABLED
    TestPythonScripts();
#endif
    XORA_LOG_INFO("TEST play mode finished with {} failure(s)", g_failures - before);
    return g_failures - before;
}
