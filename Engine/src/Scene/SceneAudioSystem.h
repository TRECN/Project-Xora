#pragma once

#include "Xora2D/Audio/AudioTypes.h"

#include <cstdint>
#include <unordered_map>

namespace Xora2D {

class AssetManager;
class AudioEngine;
class Scene;

class SceneAudioSystem {
public:
    void SetAudioEngine(AudioEngine* engine) { m_audioEngine = engine; }
    void SetAssetManager(AssetManager* assetManager) { m_assetManager = assetManager; }

    void OnSceneUpdate(Scene& scene, float deltaSeconds);
    void OnEntityDestroyed(std::uint32_t entityHandle);
    void StopEntityPlayback(std::uint32_t entityHandle);
    void StopEntityPlaybacksInGroup(Scene& scene, AudioGroup group);
    void StopLoopingEntityPlaybacks(Scene& scene);
    void PauseEntityPlayback(std::uint32_t entityHandle);
    void ResumeEntityPlayback(std::uint32_t entityHandle);
    void PauseAllEntityPlaybacks();
    void ResumeAllEntityPlaybacks();
    [[nodiscard]] bool IsEntityPlaybackActive(std::uint32_t entityHandle) const;
    void PrepareForShutdown();
    void Clear();

private:
    struct EntityVoice {
        AudioPlaybackId PlaybackId = kInvalidAudioPlaybackId;
        bool PlayOnStartConsumed = false;
    };

    AudioEngine* m_audioEngine = nullptr;
    AssetManager* m_assetManager = nullptr;
    std::unordered_map<std::uint32_t, EntityVoice> m_entityVoices;
};

} // namespace Xora2D