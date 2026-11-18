#include "Xora2D/Animation/AnimationController.h"

#include <algorithm>

namespace Xora2D {

void AnimationController::Reset() {
    m_state = AnimationPlaybackState::Stopped;
    m_frameIndex = 0;
    m_frameTimer = 0.0f;
    m_completionSignaled = false;
}

void AnimationController::AssignClip(const AnimationClip* clip) {
    m_clip = clip;
    Reset();
    if (m_clip != nullptr) {
        m_looping = m_clip->Loop;
        m_playbackSpeed = m_clip->DefaultPlaybackSpeed;
    }
}

void AnimationController::Play() {
    if (m_clip == nullptr || m_clip->Frames.empty()) {
        m_state = AnimationPlaybackState::Stopped;
        return;
    }
    m_state = AnimationPlaybackState::Playing;
    m_completionSignaled = false;
}

void AnimationController::Pause() {
    if (m_state == AnimationPlaybackState::Playing) {
        m_state = AnimationPlaybackState::Paused;
    }
}

void AnimationController::Resume() {
    if (m_state == AnimationPlaybackState::Paused) {
        m_state = AnimationPlaybackState::Playing;
    }
}

void AnimationController::Stop() {
    m_state = AnimationPlaybackState::Stopped;
    m_frameIndex = 0;
    m_frameTimer = 0.0f;
    m_completionSignaled = false;
}

void AnimationController::Restart() {
    if (m_clip == nullptr || m_clip->Frames.empty()) {
        Stop();
        return;
    }
    m_frameIndex = 0;
    m_frameTimer = 0.0f;
    m_completionSignaled = false;
    m_state = AnimationPlaybackState::Playing;
}

void AnimationController::SetLooping(const bool loop) {
    m_looping = loop;
}

void AnimationController::SetPlaybackSpeed(const float speed) {
    m_playbackSpeed = std::max(speed, 0.0f);
}

bool AnimationController::GetLooping() const {
    return m_looping;
}

const AnimationFrame* AnimationController::GetCurrentFrame() const {
    if (m_clip == nullptr || m_frameIndex < 0 ||
        m_frameIndex >= static_cast<int>(m_clip->Frames.size())) {
        return nullptr;
    }
    return &m_clip->Frames[static_cast<std::size_t>(m_frameIndex)];
}

bool AnimationController::Update(float deltaSeconds, bool& frameChanged, bool& started, bool& markerFired,
                                 std::string& marker) {
    frameChanged = false;
    started = false;
    markerFired = false;
    marker.clear();

    if (m_clip == nullptr || m_clip->Frames.empty()) {
        return false;
    }

    if (m_state == AnimationPlaybackState::Stopped) {
        return false;
    }

    if (m_state == AnimationPlaybackState::Completed) {
        return false;
    }

    if (m_state == AnimationPlaybackState::Paused) {
        return false;
    }

    float remaining = std::max(0.0f, deltaSeconds) * m_playbackSpeed;
    (void)started;
    bool completed = false;

    while (remaining > 0.0f && m_state == AnimationPlaybackState::Playing) {
        const AnimationFrame& frame = m_clip->Frames[static_cast<std::size_t>(m_frameIndex)];
        const float duration = std::max(frame.DurationSeconds, 0.0001f);
        const float needed = duration - m_frameTimer;

        if (remaining < needed) {
            m_frameTimer += remaining;
            remaining = 0.0f;
            break;
        }

        remaining -= needed;
        m_frameTimer = 0.0f;
        if (!frame.EventMarker.empty()) {
            marker = frame.EventMarker;
            markerFired = true;
        }
        const int nextIndex = m_frameIndex + 1;

        if (nextIndex >= static_cast<int>(m_clip->Frames.size())) {
            if (m_looping) {
                m_frameIndex = 0;
                frameChanged = true;
                continue;
            }
            m_frameIndex = static_cast<int>(m_clip->Frames.size()) - 1;
            m_state = AnimationPlaybackState::Completed;
            completed = !m_completionSignaled;
            m_completionSignaled = true;
            frameChanged = true;
            break;
        }

        m_frameIndex = nextIndex;
        frameChanged = true;
    }

    return completed;
}

} // namespace Xora2D