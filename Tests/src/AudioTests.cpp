#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Audio/AudioClip.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Audio/AudioEngine.h"
#include "Xora2D/Audio/AudioTypes.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_audioFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_audioFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

std::filesystem::path TempWavPath() {
    return std::filesystem::temp_directory_path() / "xora2d_test_tone.wav";
}

void WriteTestWav(const std::filesystem::path& path) {
    const std::filesystem::path source = Xora2D::ResolveAssetPath("Audio/sfx_beep.wav");
    std::filesystem::copy_file(source, path, std::filesystem::copy_options::overwrite_existing);
}

} // namespace

int RunAudioTests() {
    const int before = g_audioFailures;
    XORA_LOG_INFO("TEST audio system started");

    Xora2D::AudioEngine engine;
    Xora2D::AudioEngineSettings settings{};
    settings.Backend = Xora2D::AudioBackendKind::Null;
    const Xora2D::AudioInitResult init = engine.Initialize(settings);
    CHECK(init.Success, "null backend initializes");
    CHECK(engine.Initialize(settings).Success, "repeated initialize safe");

    const std::filesystem::path wav = TempWavPath();
    WriteTestWav(wav);

    std::shared_ptr<Xora2D::AudioClip> clip;
    const Xora2D::AudioLoadResult load = Xora2D::AudioClip::LoadFromFile(wav, clip);
    CHECK(load.Success && clip != nullptr && clip->IsValid(), "load valid wav");
    CHECK(clip->GetDurationSeconds() > 0.0, "duration metadata available");

    std::shared_ptr<Xora2D::AudioClip> missing;
    const Xora2D::AudioLoadResult badLoad = Xora2D::AudioClip::LoadFromFile("missing.wav", missing);
    CHECK(!badLoad.Success, "missing file rejected");

    const Xora2D::AudioPlaybackId voice = engine.PlayClip(clip, Xora2D::AudioGroup::SoundEffects, 0.8f, 1.0f, false);
    CHECK(voice != Xora2D::kInvalidAudioPlaybackId, "playback starts");
    CHECK(engine.IsPlaybackActive(voice), "playback active");
    engine.PausePlayback(voice);
    CHECK(engine.IsPlaybackPaused(voice), "playback pauses");
    engine.ResumePlayback(voice);
    CHECK(engine.IsPlaybackActive(voice), "playback resumes");
    engine.SetPlaybackVolume(voice, 0.5f);
    engine.SetPlaybackPitch(voice, 1.1f);
    engine.SetPlaybackLooping(voice, true);

    const Xora2D::AudioPlaybackId voice2 = engine.PlayClip(clip, Xora2D::AudioGroup::Music, 0.4f, 1.0f, false);
    CHECK(voice2 != voice, "multiple instances coexist");

    engine.SetMasterVolume(0.6f);
    engine.SetMusicVolume(0.3f);
    engine.SetSoundEffectsVolume(0.9f);
    CHECK(engine.GetMasterVolume() == 0.6f, "master volume stored");

    Xora2D::AudioSource source = engine.CreateSource();
    source.Play(clip, Xora2D::AudioGroup::SoundEffects, 1.0f, 1.0f, false);
    CHECK(source.IsPlaying(), "audio source wrapper plays");

    engine.StopAll();
    CHECK(!engine.IsPlaybackActive(voice), "stop all clears playback");

    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager assets(registry);
    const auto reg = assets.RegisterAudioAtRelativePath("Audio/sfx_beep.wav", "Beep");
    CHECK(reg.Success, "register audio asset");
    const auto loadAsset = assets.LoadAudio(reg.Handle);
    CHECK(loadAsset.Success, "load audio through asset manager");
    CHECK(assets.LoadAudio(reg.Handle).Success, "audio cache reuse");

    auto scene = Xora2D::Scene::Create();
    scene->SetAudioEngine(&engine);
    scene->SetAssetManager(&assets);
    Xora2D::Entity entity = scene->CreateEntity("AudioEntity");
    auto& component = entity.AddComponent<Xora2D::AudioSourceComponent>();
    component.ClipAsset = reg.Handle;
    component.PlayOnStart = true;
    component.Group = Xora2D::AudioGroup::SoundEffects;
    scene->OnUpdate(0.016f);
    scene->DestroyEntity(entity);

    const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "xora2d_audio_scene.yaml";
    entity = scene->CreateEntity("SaveMe");
    entity.AddComponent<Xora2D::AudioSourceComponent>() = component;
    entity.AddComponent<Xora2D::AudioListenerComponent>();
    CHECK(Xora2D::SceneSerializer::Serialize(*scene, scenePath).Success, "serialize audio components");

    auto loaded = Xora2D::Scene::Create();
    loaded->SetAssetManager(&assets);
    CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, scenePath, &assets).Success, "deserialize audio components");
    const Xora2D::Entity loadedEntity = loaded->FindEntityByName("SaveMe");
    CHECK(loadedEntity.IsValid() && loadedEntity.HasComponent<Xora2D::AudioSourceComponent>(), "audio source restored");

    engine.PrepareShutdown();
    CHECK(engine.Initialize(settings).Success, "init after shutdown");

    if (std::filesystem::exists(wav)) {
        std::filesystem::remove(wav);
    }
    if (std::filesystem::exists(scenePath)) {
        std::filesystem::remove(scenePath);
    }

    const int added = g_audioFailures - before;
    XORA_LOG_INFO("TEST audio system finished ({} failures)", added);
    return added;
}