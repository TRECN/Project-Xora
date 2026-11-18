#include "Xora2DEditor/Content/AssetPreview.h"

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Audio/AudioClip.h"
#include "Xora2D/Audio/AudioEngine.h"

#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <set>
#include <cctype>
#include <chrono>
#include <fstream>

namespace Xora2DEditor {
namespace {

constexpr std::size_t kScriptPreviewBytes = 16 * 1024;
constexpr std::size_t kScriptPreviewLines = 200;

std::string FormatLabel(const std::string& extension) {
    std::string label;
    for (const char character : extension) {
        if (character != '.') {
            label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return label;
}

void GatherTexture(const std::filesystem::path& file, AssetDetails& details) {
    TextureDetails texture{};
    if (stbi_info(file.string().c_str(), &texture.Width, &texture.Height, &texture.Channels) == 0) {
        details.Error = std::string("Unreadable image: ") + stbi_failure_reason();
        return;
    }
    static constexpr const char* kChannelNames[] = {"?", "Grey8", "GreyAlpha8", "RGB8", "RGBA8"};
    texture.Format = FormatLabel(file.extension().string()) + " (" +
                     kChannelNames[std::clamp(texture.Channels, 0, 4)] + ")";
    details.Texture = texture;
}

void GatherAudio(const std::filesystem::path& file, AssetDetails& details) {
    std::shared_ptr<Xora2D::AudioClip> clip;
    const Xora2D::AudioLoadResult load = Xora2D::AudioClip::LoadFromFile(file, clip);
    if (!load.Success || clip == nullptr) {
        details.Error = "Audio could not be decoded: " + load.Message;
        return;
    }
    AudioDetails audio{};
    audio.DurationSeconds = clip->GetDurationSeconds();
    audio.SampleRate = clip->GetSampleRate();
    audio.Channels = clip->GetChannelCount();
    audio.FrameCount = clip->GetFrameCount();
    audio.Format = FormatLabel(file.extension().string());
    details.Audio = audio;
}

void GatherScript(const std::filesystem::path& file, AssetDetails& details) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        details.Error = "Script cannot be opened";
        return;
    }
    ScriptDetails script{};
    std::string line;
    while (std::getline(input, line)) {
        ++script.LineCount;
        if (script.LineCount <= kScriptPreviewLines && script.Preview.size() < kScriptPreviewBytes) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            script.Preview += line;
            script.Preview += '\n';
        } else {
            script.PreviewTruncated = true;
        }
    }
    details.Script = std::move(script);
}

void GatherScene(const std::filesystem::path& file, AssetDetails& details) {
    try {
        const YAML::Node root = YAML::LoadFile(file.string());
        SceneDetails scene{};
        scene.SceneName = root["Name"] ? root["Name"].as<std::string>("") : std::string{};
        if (const YAML::Node entities = root["Entities"]; entities && entities.IsSequence()) {
            scene.EntityCount = entities.size();
        }
        details.Scene = scene;
    } catch (const YAML::Exception& exception) {
        details.Error = std::string("Scene YAML is invalid: ") + exception.what();
    }
}

// Worker-thread safe: reads the YAML only (no Scene / AssetManager access). Full validation happens on load.
void GatherPrefab(const std::filesystem::path& file, const std::vector<Xora2D::AssetMetadata>& registry,
                  AssetDetails& details) {
    try {
        const YAML::Node root = YAML::LoadFile(file.string());
        const YAML::Node header = root["Prefab"];
        if (!header || !header.IsMap()) {
            details.Error = "Not a valid prefab (missing 'Prefab' header)";
            return;
        }
        PrefabDetails prefab{};
        prefab.PrefabName = header["Name"] ? header["Name"].as<std::string>("") : std::string{};
        prefab.Revision = header["Revision"] ? header["Revision"].as<std::uint32_t>(0) : 0;
        prefab.FormatVersion = header["FormatVersion"] ? header["FormatVersion"].as<int>(0) : 0;
        std::set<std::string> components;
        std::set<std::string> missing;
        const auto checkHandle = [&](const YAML::Node& node, const std::string& owner, const char* what) {
            if (!node) {
                return;
            }
            const std::string text = node.as<std::string>("");
            const bool known = std::any_of(registry.begin(), registry.end(), [&](const Xora2D::AssetMetadata& meta) {
                return meta.Handle.ToString() == text;
            });
            if (!known) {
                missing.insert(owner + ": " + what + " " + text);
            }
        };
        if (const YAML::Node entities = root["Entities"]; entities && entities.IsSequence()) {
            prefab.EntityCount = entities.size();
            for (const YAML::Node& entity : entities) {
                const std::string name =
                    entity["TagComponent"] ? entity["TagComponent"]["Tag"].as<std::string>("") : std::string{};
                prefab.EntityNames.push_back(name);
                for (const auto& item : entity) {
                    const std::string key = item.first.Scalar();
                    if (key != "Entity") {
                        components.insert(key);
                    }
                }
                if (const YAML::Node sprite = entity["SpriteRendererComponent"]) {
                    checkHandle(sprite["TextureAssetHandle"], name, "texture");
                }
                if (const YAML::Node audio = entity["AudioSourceComponent"]) {
                    checkHandle(audio["ClipAssetHandle"], name, "audio clip");
                }
                if (const YAML::Node script = entity["PythonScriptComponent"]) {
                    checkHandle(script["ScriptAssetHandle"], name, "script");
                }
                if (const YAML::Node animation = entity["AnimationComponent"]) {
                    checkHandle(animation["DefaultClip"], name, "animation clip");
                }
            }
        }
        if (prefab.FormatVersion < 1 || prefab.EntityCount == 0) {
            details.Error = prefab.FormatVersion < 1 ? "Prefab has no valid FormatVersion" : "Prefab has no entities";
        }
        prefab.Components.assign(components.begin(), components.end());
        prefab.MissingReferences.assign(missing.begin(), missing.end());
        details.Prefab = std::move(prefab);
    } catch (const YAML::Exception& exception) {
        details.Error = std::string("Prefab YAML is invalid: ") + exception.what();
    }
}

void GatherAnimation(const std::filesystem::path& file, const std::string& assetRelative,
                     const std::vector<Xora2D::AssetMetadata>& registry, AssetDetails& details) {
    std::filesystem::path texturePath;
    Xora2D::AssetHandle textureHandle{};
    std::string textureRelative;
    try {
        const YAML::Node root = YAML::LoadFile(file.string());
        if (root["TextureAssetHandle"]) {
            textureHandle = Xora2D::AssetHandle::FromString(root["TextureAssetHandle"].as<std::string>(""))
                                .value_or(Xora2D::AssetHandle{});
        }
        if (root["TextureRelativePath"]) {
            textureRelative = root["TextureRelativePath"].as<std::string>("");
        }
    } catch (const YAML::Exception& exception) {
        details.Error = std::string("Animation YAML is invalid: ") + exception.what();
        return;
    }
    for (const Xora2D::AssetMetadata& meta : registry) {
        if ((textureHandle.IsValid() && meta.Handle == textureHandle) ||
            (!textureHandle.IsValid() && !textureRelative.empty() && meta.RelativePath == textureRelative)) {
            textureHandle = meta.Handle;
            textureRelative = meta.RelativePath;
            texturePath = Xora2D::ResolveAssetPath(meta.RelativePath);
            break;
        }
    }

    Xora2D::AnimationClipAsset asset;
    const Xora2D::AssetResult load = Xora2D::AnimationClipAsset::LoadFromFile(file, assetRelative, asset, texturePath);
    if (!load.Success || !asset.IsValid()) {
        details.Error = "Animation clip could not be loaded: " + load.Message;
        return;
    }
    const Xora2D::AnimationClip& clip = asset.GetClip();
    AnimationDetails animation{};
    animation.ClipName = clip.Name;
    animation.FrameCount = clip.Frames.size();
    animation.Loop = clip.Loop;
    animation.PlaybackSpeed = clip.DefaultPlaybackSpeed;
    animation.TextureHandle = textureHandle.IsValid() ? textureHandle : clip.TextureAsset;
    animation.TexturePath = textureRelative;
    for (const Xora2D::AnimationFrame& frame : clip.Frames) {
        animation.TotalDurationSeconds += frame.DurationSeconds;
        animation.FrameDurations.push_back(frame.DurationSeconds);
        animation.FrameUVs.push_back({frame.UVRect.x, frame.UVRect.y, frame.UVRect.z, frame.UVRect.w});
    }
    details.Animation = std::move(animation);
}

} // namespace

AssetDetails GatherAssetDetails(const std::string& resourcePath,
                                const std::vector<Xora2D::AssetMetadata>& registrySnapshot) {
    AssetDetails details{};
    details.ResourcePath = resourcePath;
    details.FileName = FileNameOf(resourcePath);
    details.Kind = ClassifyContentFile(details.FileName);
    const std::optional<std::string> assetRelative = AssetRelativePathFromResourcePath(resourcePath);
    if (assetRelative.has_value()) {
        for (const Xora2D::AssetMetadata& meta : registrySnapshot) {
            if (meta.RelativePath == *assetRelative) {
                details.Metadata = meta;
                break;
            }
        }
    }

    const std::filesystem::path file = ResolveResourceRelativePath(resourcePath);
    std::error_code errorCode;
    details.ExistsOnDisk = !file.empty() && std::filesystem::is_regular_file(file, errorCode);
    if (!details.ExistsOnDisk) {
        details.Error = "File is missing on disk";
        return details;
    }
    details.FileSizeBytes = std::filesystem::file_size(file, errorCode);
    details.LastModified = std::filesystem::last_write_time(file, errorCode);

    switch (details.Kind) {
    case ContentItemKind::Texture:
        GatherTexture(file, details);
        break;
    case ContentItemKind::Audio:
        GatherAudio(file, details);
        break;
    case ContentItemKind::Script:
        GatherScript(file, details);
        break;
    case ContentItemKind::Scene:
        GatherScene(file, details);
        break;
    case ContentItemKind::Animation:
        GatherAnimation(file, assetRelative.value_or(details.FileName), registrySnapshot, details);
        break;
    case ContentItemKind::Prefab:
        GatherPrefab(file, registrySnapshot, details);
        break;
    default:
        break;
    }
    return details;
}

AssetDetailsLoader::~AssetDetailsLoader() {
    // Futures from std::async join in their destructors; all work is bounded file parsing.
    m_abandoned.clear();
}

const AssetDetails* AssetDetailsLoader::Request(const std::string& resourcePath,
                                                std::filesystem::file_time_type lastModified,
                                                const std::vector<Xora2D::AssetMetadata>& registrySnapshot) {
    std::erase_if(m_abandoned, [](std::future<AssetDetails>& future) {
        return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });

    if (resourcePath != m_key || lastModified != m_lastModified) {
        if (m_pending.valid()) {
            m_abandoned.push_back(std::move(m_pending));
        }
        m_key = resourcePath;
        m_lastModified = lastModified;
        m_ready.reset();
        m_pending = std::async(std::launch::async, [resourcePath, registrySnapshot]() {
            return GatherAssetDetails(resourcePath, registrySnapshot);
        });
    }
    if (m_pending.valid() && m_pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_ready = m_pending.get();
    }
    return m_ready ? &*m_ready : nullptr;
}

void AssetDetailsLoader::Invalidate() {
    if (m_pending.valid()) {
        m_abandoned.push_back(std::move(m_pending));
    }
    m_key.clear();
    m_ready.reset();
}

AudioPreviewPlayer::~AudioPreviewPlayer() {
    Stop();
}

void AudioPreviewPlayer::SetAudioEngine(Xora2D::AudioEngine* engine) {
    if (engine != m_engine) {
        Stop();
    }
    m_engine = engine;
}

bool AudioPreviewPlayer::Play(Xora2D::AssetHandle handle, Xora2D::AssetManager& assetManager, std::string& error) {
    Stop();
    if (m_engine == nullptr || !m_engine->IsInitialized()) {
        error = "Audio engine is not available";
        return false;
    }
    const Xora2D::AssetResult load = assetManager.LoadAudio(handle);
    if (!load.Success) {
        error = load.Message;
        return false;
    }
    const std::shared_ptr<const Xora2D::AudioClip> clip = assetManager.GetLoadedAudio(handle);
    if (clip == nullptr || !clip->IsValid()) {
        error = "Audio clip is not valid";
        return false;
    }
    m_playback = m_engine->PlayClip(clip, Xora2D::AudioGroup::SoundEffects, 0.8f, 1.0f, false);
    if (m_playback == Xora2D::kInvalidAudioPlaybackId) {
        error = "Playback could not be started (no audio device?)";
        return false;
    }
    m_handle = handle;
    m_state = State::Playing;
    return true;
}

void AudioPreviewPlayer::Pause() {
    if (m_engine != nullptr && m_state == State::Playing) {
        m_engine->PausePlayback(m_playback);
        m_state = State::Paused;
    }
}

void AudioPreviewPlayer::Resume() {
    if (m_engine != nullptr && m_state == State::Paused) {
        m_engine->ResumePlayback(m_playback);
        m_state = State::Playing;
    }
}

void AudioPreviewPlayer::Stop() {
    if (m_engine != nullptr && m_playback != Xora2D::kInvalidAudioPlaybackId) {
        m_engine->StopPlayback(m_playback);
    }
    m_playback = Xora2D::kInvalidAudioPlaybackId;
    m_handle = {};
    m_state = State::Stopped;
}

void AudioPreviewPlayer::Update() {
    if (m_engine != nullptr && m_state == State::Playing && !m_engine->IsPlaybackActive(m_playback)) {
        m_playback = Xora2D::kInvalidAudioPlaybackId;
        m_handle = {};
        m_state = State::Stopped;
    }
}

} // namespace Xora2DEditor
