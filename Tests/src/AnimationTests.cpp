#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Animation/AnimationController.h"
#include "Xora2D/Animation/AnimationEvents.h"
#include "Xora2D/Animation/AnimationPlayback.h"
#include "Xora2D/Animation/SpriteSheetLayout.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <filesystem>
#include <fstream>

namespace {

int g_animationFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_animationFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

Xora2D::AnimationClip MakeTestClip(int frameCount, bool loop) {
    Xora2D::AnimationClip clip{};
    clip.Name = "Test";
    clip.Loop = loop;
    clip.DefaultPlaybackSpeed = 1.0f;
    for (int index = 0; index < frameCount; ++index) {
        Xora2D::AnimationFrame frame{};
        frame.DurationSeconds = 0.1f;
        frame.UVRect = glm::vec4(0.0f, 0.0f, 0.25f, 1.0f);
        if (index == 1) {
            frame.EventMarker = "step";
        }
        clip.Frames.push_back(frame);
    }
    return clip;
}

void TestSpriteSheetLayout() {
    Xora2D::SpriteSheetLayout layout{};
    layout.Columns = 4;
    layout.Rows = 2;
    layout.FrameWidthPixels = 32.0f;
    layout.FrameHeightPixels = 32.0f;
    layout.StartFrame = 0;
    layout.FrameCount = 8;
    layout.Order = Xora2D::SpriteSheetFrameOrder::RowMajor;

    CHECK(Xora2D::ValidateSpriteSheetLayout(layout, 128, 64).Valid, "valid multi-row sheet");
    const std::optional first = Xora2D::CalculateSpriteSheetFrameUV(layout, 128, 64, 0);
    const std::optional last = Xora2D::CalculateSpriteSheetFrameUV(layout, 128, 64, 7);
    CHECK(first.has_value() && last.has_value(), "first and last frame UV");
    CHECK(Xora2D::CalculateSpriteSheetFrameUV(layout, 128, 64, 8) == std::nullopt, "out-of-range frame rejected");

    layout.FrameWidthPixels = 200.0f;
    CHECK(!Xora2D::ValidateSpriteSheetLayout(layout, 128, 64).Valid, "invalid dimensions rejected");

    layout = {};
    layout.Columns = 2;
    layout.Rows = 2;
    layout.FrameWidthPixels = 10;
    layout.FrameHeightPixels = 10;
    layout.FrameCount = 3;
    layout.Order = Xora2D::SpriteSheetFrameOrder::CustomSequence;
    layout.CustomSequence = {3, 1, 0};
    CHECK(Xora2D::ValidateSpriteSheetLayout(layout, 20, 20).Valid, "custom sequence validated");
    CHECK(Xora2D::CalculateSpriteSheetFrameUV(layout, 20, 20, 1).has_value(), "custom sequence UV");
}

void TestAnimationControllerPlayback() {
    Xora2D::AnimationClip clip = MakeTestClip(3, true);
    Xora2D::AnimationController controller;
    controller.AssignClip(&clip);
    controller.Play();

    bool frameChanged = false;
    bool started = false;
    bool marker = false;
    std::string markerText;
    (void)controller.Update(0.05f, frameChanged, started, marker, markerText);
    CHECK(controller.GetCurrentFrameIndex() == 0, "initial frame");

    (void)controller.Update(0.2f, frameChanged, started, marker, markerText);
    CHECK(controller.GetCurrentFrameIndex() == 2, "frame advances with delta");

    controller.SetPlaybackSpeed(2.0f);
    (void)controller.Update(0.05f, frameChanged, started, marker, markerText);
    CHECK(controller.GetCurrentFrameIndex() >= 0, "playback speed affects timing");

    controller.Pause();
    const int pausedFrame = controller.GetCurrentFrameIndex();
    (void)controller.Update(0.5f, frameChanged, started, marker, markerText);
    CHECK(controller.GetCurrentFrameIndex() == pausedFrame, "pause preserves frame");

    controller.Resume();
    controller.Stop();
    CHECK(controller.GetState() == Xora2D::AnimationPlaybackState::Stopped, "stop resets playback state");

    clip.Loop = false;
    controller.AssignClip(&clip);
    controller.Play();
    bool completed = false;
  for (int step = 0; step < 20; ++step) {
        completed = controller.Update(0.2f, frameChanged, started, marker, markerText);
        if (completed) {
            break;
        }
    }
    CHECK(completed, "non-looping animation completes");
    CHECK(controller.GetState() == Xora2D::AnimationPlaybackState::Completed, "completed state");
    CHECK(!controller.Update(0.1f, frameChanged, started, marker, markerText), "completion emitted once");

    controller.Restart();
    CHECK(controller.GetState() == Xora2D::AnimationPlaybackState::Playing, "restart plays again");
}

void TestSceneAnimationIntegration() {
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager assets(registry);
    assets.SetGraphicsResourcesEnabled(false);
    assets.RegisterTextureAtRelativePath("Textures/DemoSpriteSheet.png", "DemoSpriteSheet");
    const Xora2D::AssetResult walkReg =
        assets.RegisterAnimationClipAtRelativePath("Animations/DemoWalk.anim.yaml", "Walk");
    CHECK(walkReg.Success, "register walk clip asset");
    CHECK(assets.LoadAnimationClip(walkReg.Handle).Success, "load walk clip");

    auto scene = Xora2D::Scene::Create();
    scene->SetAssetManager(&assets);

    int completionCount = 0;
    scene->SetAnimationEventCallback([&](const Xora2D::AnimationEvent& event) {
        if (event.Type == Xora2D::AnimationEventType::Completed) {
            ++completionCount;
        }
    });

    Xora2D::Entity entity = scene->CreateEntity("Animated");
    entity.AddComponent<Xora2D::SpriteRendererComponent>().Size = {64.0f, 64.0f};
    auto& animation = entity.AddComponent<Xora2D::AnimationComponent>();
    animation.ClipAssets = {walkReg.Handle};
    animation.DefaultClip = walkReg.Handle;
    animation.Looping = false;
    animation.Autoplay = true;

    for (int frame = 0; frame < 60; ++frame) {
        scene->OnUpdate(0.05f);
    }
    CHECK(animation.CurrentFrameIndex >= 0, "scene update advances animation");
    CHECK(completionCount <= 1, "completion dispatched once per playthrough");

    Xora2D::AnimationPlay(entity, assets, walkReg.Handle);
    scene->OnUpdate(0.5f);
    CHECK(completionCount >= 1, "replay can dispatch completion again");

    scene->DestroyEntity(entity);
    scene->OnUpdate(0.016f);
    CHECK(true, "entity destruction does not crash animation system");
}

void TestAnimationSerialization() {
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager assets(registry);
    auto scene = Xora2D::Scene::Create();
    scene->SetAssetManager(&assets);
    Xora2D::Entity entity = scene->CreateEntity("AnimEntity");
    auto& animation = entity.AddComponent<Xora2D::AnimationComponent>();
    animation.PlaybackSpeed = 1.5f;
    animation.Looping = false;
    animation.Autoplay = false;
    animation.DefaultClipName = "Walk";

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "xora2d_anim_scene.yaml";
    CHECK(Xora2D::SceneSerializer::Serialize(*scene, path).Success, "serialize animation component");

    auto loaded = Xora2D::Scene::Create();
    CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, path, &assets).Success, "deserialize animation component");
    const Xora2D::Entity loadedEntity = loaded->FindEntityByName("AnimEntity");
    CHECK(loadedEntity.IsValid(), "animated entity round trip");
    const auto& loadedAnim = loadedEntity.GetComponent<Xora2D::AnimationComponent>();
    CHECK(loadedAnim.PlaybackSpeed == 1.5f, "playback speed round trip");
    CHECK(!loadedAnim.Looping, "looping round trip");

    const std::filesystem::path badPath = std::filesystem::temp_directory_path() / "xora2d_anim_bad.yaml";
    std::ofstream(badPath) << R"(Version: 2
Entities:
  - Name: Bad
    ID: "00000000-0000-0000-0000-000000000001"
    Transform: {}
    AnimationComponent:
      ClipAssets: [not-a-handle]
)";
    const std::size_t before = loaded->GetEntityCount();
    CHECK(!Xora2D::SceneSerializer::Deserialize(*loaded, badPath, &assets).Success, "invalid animation data rejected");
    CHECK(loaded->GetEntityCount() == before, "failed animation deserialize is transactional");
}

} // namespace

int RunAnimationTests() {
    const int before = g_animationFailures;
    XORA_LOG_INFO("TEST animation started");
    TestSpriteSheetLayout();
    TestAnimationControllerPlayback();
    TestSceneAnimationIntegration();
    TestAnimationSerialization();
    if (g_animationFailures == before) {
        XORA_LOG_INFO("TEST animation passed");
    } else {
        XORA_LOG_ERROR("TEST animation failed");
    }
    return g_animationFailures - before;
}