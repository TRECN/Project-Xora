#pragma once

#include "Xora2D/Audio/AudioTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Xora2D {

class AudioClip;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual AudioInitResult Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual AudioPlaybackId Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch,
                                 bool loop, float masterVolume, float musicVolume, float sfxVolume) = 0;
    virtual void Pause(AudioPlaybackId id) = 0;
    virtual void Resume(AudioPlaybackId id) = 0;
    virtual void Stop(AudioPlaybackId id) = 0;

    [[nodiscard]] virtual bool IsPlaying(AudioPlaybackId id) const = 0;
    [[nodiscard]] virtual bool IsPaused(AudioPlaybackId id) const = 0;

    virtual void SetVolume(AudioPlaybackId id, float volume) = 0;
    virtual void SetPitch(AudioPlaybackId id, float pitch) = 0;
    virtual void SetLooping(AudioPlaybackId id, bool loop) = 0;

    virtual void ApplyGroupVolumes(float masterVolume, float musicVolume, float sfxVolume) = 0;
    virtual void PauseAll() = 0;
    virtual void ResumeAll() = 0;
    virtual void StopAll() = 0;
    virtual void StopPlaybacksInGroup(AudioGroup group) = 0;
    virtual void StopLoopingPlaybacks() = 0;

    virtual void Update(float /*deltaSeconds*/) = 0;
};

class NullAudioBackend final : public AudioBackend {
public:
    AudioInitResult Initialize() override;
    void Shutdown() override;

    AudioPlaybackId Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch, bool loop,
                         float masterVolume, float musicVolume, float sfxVolume) override;
    void Pause(AudioPlaybackId id) override;
    void Resume(AudioPlaybackId id) override;
    void Stop(AudioPlaybackId id) override;

    bool IsPlaying(AudioPlaybackId id) const override;
    bool IsPaused(AudioPlaybackId id) const override;

    void SetVolume(AudioPlaybackId id, float volume) override;
    void SetPitch(AudioPlaybackId id, float pitch) override;
    void SetLooping(AudioPlaybackId id, bool loop) override;

    void ApplyGroupVolumes(float masterVolume, float musicVolume, float sfxVolume) override;
    void PauseAll() override;
    void ResumeAll() override;
    void StopAll() override;
    void StopPlaybacksInGroup(AudioGroup group) override;
    void StopLoopingPlaybacks() override;

    void Update(float deltaSeconds) override;

private:
    struct Voice {
        std::shared_ptr<const AudioClip> Clip;
        AudioGroup Group = AudioGroup::SoundEffects;
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool Playing = false;
        bool Paused = false;
        float ElapsedSeconds = 0.0f;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<AudioPlaybackId, Voice> m_voices;
    AudioPlaybackId m_nextId = 1;
    float m_master = 1.0f;
    float m_music = 1.0f;
    float m_sfx = 1.0f;
};

std::unique_ptr<AudioBackend> CreateMiniaudioBackend();

} // namespace Xora2D