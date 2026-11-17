#pragma once

#include "Xora2D/Audio/AudioTypes.h"

#include <memory>

namespace Xora2D {

class AudioClip;
class AudioEngine;

// Controls one active playback instance.
class AudioSource {
public:
    AudioSource() = default;

    [[nodiscard]] bool IsValid() const { return m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId; }

    void Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch, bool loop);
    void Pause();
    void Resume();
    void Stop();

    void SetVolume(float volume);
    void SetPitch(float pitch);
    void SetLooping(bool loop);

    [[nodiscard]] bool IsPlaying() const;
    [[nodiscard]] bool IsPaused() const;

private:
    friend class AudioEngine;

    AudioEngine* m_engine = nullptr;
    AudioPlaybackId m_playbackId = kInvalidAudioPlaybackId;
};

} // namespace Xora2D