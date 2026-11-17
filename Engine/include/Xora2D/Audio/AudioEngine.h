#pragma once

#include "Xora2D/Audio/AudioListener.h"
#include "Xora2D/Audio/AudioSource.h"
#include "Xora2D/Audio/AudioTypes.h"

#include <memory>
#include <mutex>

namespace Xora2D {

class AudioClip;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    AudioInitResult Initialize(const AudioEngineSettings& settings = {});
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] bool IsDeviceAvailable() const;

    AudioSource CreateSource();
    void ReleaseSource(AudioSource& source);

    AudioPlaybackId PlayClip(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch,
                             bool loop);
    void PausePlayback(AudioPlaybackId id);
    void ResumePlayback(AudioPlaybackId id);
    void StopPlayback(AudioPlaybackId id);

    [[nodiscard]] bool IsPlaybackActive(AudioPlaybackId id) const;
    [[nodiscard]] bool IsPlaybackPaused(AudioPlaybackId id) const;

    void SetPlaybackVolume(AudioPlaybackId id, float volume);
    void SetPlaybackPitch(AudioPlaybackId id, float pitch);
    void SetPlaybackLooping(AudioPlaybackId id, bool loop);

    void SetMasterVolume(float volume);
    void SetMusicVolume(float volume);
    void SetSoundEffectsVolume(float volume);
    [[nodiscard]] float GetMasterVolume() const;
    [[nodiscard]] float GetMusicVolume() const;
    [[nodiscard]] float GetSoundEffectsVolume() const;

    AudioListener& GetListener();
    const AudioListener& GetListener() const;

    void PauseAll();
    void ResumeAll();
    void StopAll();
    void StopPlaybacksInGroup(AudioGroup group);
    void StopLoopingPlaybacks();

    void Update(float deltaSeconds);

    void PrepareShutdown();

private:
    friend class AudioSource;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    mutable std::mutex m_mutex;
    AudioListener m_listener;
    bool m_initialized = false;
    bool m_deviceAvailable = false;
};

} // namespace Xora2D