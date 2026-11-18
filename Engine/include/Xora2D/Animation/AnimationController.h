#pragma once

#include "Xora2D/Animation/AnimationTypes.h"

namespace Xora2D {

// Time-based clip playback state machine (independent of rendering).
class AnimationController {
public:
    void Reset();
    void AssignClip(const AnimationClip* clip);

    void Play();
    void Pause();
    void Resume();
    void Stop();
    void Restart();

    void SetLooping(bool loop);
    void SetPlaybackSpeed(float speed);

    [[nodiscard]] AnimationPlaybackState GetState() const { return m_state; }
    [[nodiscard]] int GetCurrentFrameIndex() const { return m_frameIndex; }
    [[nodiscard]] const AnimationFrame* GetCurrentFrame() const;
    [[nodiscard]] bool IsPlaying() const { return m_state == AnimationPlaybackState::Playing; }
    [[nodiscard]] float GetPlaybackSpeed() const { return m_playbackSpeed; }
    [[nodiscard]] bool GetLooping() const;

    // Updates playback; returns true once when a non-looping clip completes (exactly one completion edge).
    bool Update(float deltaSeconds, bool& frameChanged, bool& started, bool& markerFired, std::string& marker);

private:
    const AnimationClip* m_clip = nullptr;
    AnimationPlaybackState m_state = AnimationPlaybackState::Stopped;
    int m_frameIndex = 0;
    float m_frameTimer = 0.0f;
    float m_playbackSpeed = 1.0f;
    bool m_looping = true;
    bool m_completionSignaled = false;
};

} // namespace Xora2D