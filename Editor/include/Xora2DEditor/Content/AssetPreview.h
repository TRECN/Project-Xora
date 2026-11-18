#pragma once

#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetMetadata.h"
#include "Xora2D/Audio/AudioTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace Xora2D {

class AssetManager;
class AudioEngine;

} // namespace Xora2D

namespace Xora2DEditor {

struct TextureDetails {
    int Width = 0;
    int Height = 0;
    int Channels = 0;
    std::string Format; // e.g. "PNG (RGBA8)"
};

struct AudioDetails {
    double DurationSeconds = 0.0;
    std::uint32_t SampleRate = 0;
    std::uint32_t Channels = 0;
    std::uint64_t FrameCount = 0;
    std::string Format;
};

struct ScriptDetails {
    std::string Preview;
    std::size_t LineCount = 0;
    bool PreviewTruncated = false;
};

struct SceneDetails {
    std::string SceneName;
    std::size_t EntityCount = 0;
};

struct PrefabDetails {
    std::string PrefabName;
    std::uint32_t Revision = 0;
    int FormatVersion = 0;
    std::size_t EntityCount = 0;
    std::vector<std::string> EntityNames;
    std::vector<std::string> Components; // union of component types used
    std::vector<std::string> MissingReferences;
};

struct AnimationDetails {
    std::string ClipName;
    std::size_t FrameCount = 0;
    bool Loop = false;
    float PlaybackSpeed = 1.0f;
    float TotalDurationSeconds = 0.0f;
    Xora2D::AssetHandle TextureHandle{};
    std::string TexturePath; // asset-relative
    std::vector<float> FrameDurations;
    std::vector<std::array<float, 4>> FrameUVs; // (u0, v0, u1, v1), OpenGL convention (v0 = bottom)
};

// Everything the asset inspector shows about one file. Gathered off the main thread (files are parsed/decoded).
struct AssetDetails {
    std::string ResourcePath;
    std::string FileName;
    ContentItemKind Kind = ContentItemKind::Other;
    std::optional<Xora2D::AssetMetadata> Metadata; // registry snapshot when registered
    bool ExistsOnDisk = false;
    std::uint64_t FileSizeBytes = 0;
    std::filesystem::file_time_type LastModified{};

    std::optional<TextureDetails> Texture;
    std::optional<AudioDetails> Audio;
    std::optional<ScriptDetails> Script;
    std::optional<SceneDetails> Scene;
    std::optional<AnimationDetails> Animation;
    std::optional<PrefabDetails> Prefab;
    std::string Error; // type-specific metadata could not be read
};

// Pure function; `registrySnapshot` is a copy of the registry so this can run on a worker thread.
[[nodiscard]] AssetDetails GatherAssetDetails(const std::string& resourcePath,
                                              const std::vector<Xora2D::AssetMetadata>& registrySnapshot);

// Loads AssetDetails asynchronously for the current selection; stale requests are discarded.
class AssetDetailsLoader {
public:
    ~AssetDetailsLoader();
    // Returns the details for `resourcePath` if ready; otherwise starts/continues loading and returns null.
    const AssetDetails* Request(const std::string& resourcePath, std::filesystem::file_time_type lastModified,
                                const std::vector<Xora2D::AssetMetadata>& registrySnapshot);
    void Invalidate();
    [[nodiscard]] bool IsLoading() const { return m_pending.valid(); }

private:
    std::string m_key;
    std::filesystem::file_time_type m_lastModified{};
    std::optional<AssetDetails> m_ready;
    std::future<AssetDetails> m_pending;
    std::vector<std::future<AssetDetails>> m_abandoned;
};

// Editor-only audio preview. Uses its own playback id on the shared AudioEngine and only ever stops/pauses that
// id, so it never affects audio started by the scene or play mode.
class AudioPreviewPlayer {
public:
    enum class State : std::uint8_t { Stopped, Playing, Paused };

    ~AudioPreviewPlayer();
    void SetAudioEngine(Xora2D::AudioEngine* engine);

    bool Play(Xora2D::AssetHandle handle, Xora2D::AssetManager& assetManager, std::string& error);
    void Pause();
    void Resume();
    void Stop();
    // Detects natural end of playback.
    void Update();

    [[nodiscard]] State GetState() const { return m_state; }
    [[nodiscard]] Xora2D::AssetHandle GetHandle() const { return m_handle; }
    [[nodiscard]] bool IsPreviewing(Xora2D::AssetHandle handle) const { return m_state != State::Stopped && m_handle == handle; }

private:
    Xora2D::AudioEngine* m_engine = nullptr;
    Xora2D::AudioPlaybackId m_playback = Xora2D::kInvalidAudioPlaybackId;
    Xora2D::AssetHandle m_handle{};
    State m_state = State::Stopped;
};

} // namespace Xora2DEditor
