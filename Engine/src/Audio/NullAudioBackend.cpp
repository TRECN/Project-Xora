#include "AudioBackend.h"

#include "Xora2D/Audio/AudioClip.h"

#include <algorithm>

namespace Xora2D {

namespace {

float ClampVolume(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

AudioInitResult NullAudioBackend::Initialize() {
    AudioInitResult result{};
    result.Success = true;
    result.DeviceAvailable = false;
    result.Message = "Null audio backend active";
    return result;
}

void NullAudioBackend::Shutdown() {
    std::lock_guard lock(m_mutex);
    m_voices.clear();
}

AudioPlaybackId NullAudioBackend::Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume,
                                       float pitch, bool loop, float masterVolume, float musicVolume,
                                       float sfxVolume) {
    if (clip == nullptr || !clip->IsValid()) {
        return kInvalidAudioPlaybackId;
    }
    std::lock_guard lock(m_mutex);
    m_master = masterVolume;
    m_music = musicVolume;
    m_sfx = sfxVolume;
    const AudioPlaybackId id = m_nextId++;
    Voice voice{};
    voice.Clip = std::move(clip);
    voice.Group = group;
    voice.Volume = volume;
    voice.Pitch = pitch;
    voice.Loop = loop;
    voice.Playing = true;
    voice.Paused = false;
    voice.ElapsedSeconds = 0.0f;
    m_voices.emplace(id, std::move(voice));
    return id;
}

void NullAudioBackend::Pause(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return;
    }
    found->second.Paused = true;
}

void NullAudioBackend::Resume(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return;
    }
    found->second.Paused = false;
    found->second.Playing = true;
}

void NullAudioBackend::Stop(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    m_voices.erase(id);
}

bool NullAudioBackend::IsPlaying(AudioPlaybackId id) const {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return false;
    }
    return found->second.Playing && !found->second.Paused;
}

bool NullAudioBackend::IsPaused(AudioPlaybackId id) const {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return false;
    }
    return found->second.Paused;
}

void NullAudioBackend::SetVolume(AudioPlaybackId id, float volume) {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return;
    }
    found->second.Volume = volume;
}

void NullAudioBackend::SetPitch(AudioPlaybackId id, float pitch) {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return;
    }
    found->second.Pitch = pitch;
}

void NullAudioBackend::SetLooping(AudioPlaybackId id, bool loop) {
    std::lock_guard lock(m_mutex);
    const auto found = m_voices.find(id);
    if (found == m_voices.end()) {
        return;
    }
    found->second.Loop = loop;
}

void NullAudioBackend::ApplyGroupVolumes(float masterVolume, float musicVolume, float sfxVolume) {
    std::lock_guard lock(m_mutex);
    m_master = masterVolume;
    m_music = musicVolume;
    m_sfx = sfxVolume;
}

void NullAudioBackend::PauseAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [id, voice] : m_voices) {
        (void)id;
        voice.Paused = true;
    }
}

void NullAudioBackend::ResumeAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [id, voice] : m_voices) {
        (void)id;
        voice.Paused = false;
        voice.Playing = true;
    }
}

void NullAudioBackend::StopAll() {
    std::lock_guard lock(m_mutex);
    m_voices.clear();
}

void NullAudioBackend::StopPlaybacksInGroup(AudioGroup group) {
    std::lock_guard lock(m_mutex);
    for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
        if (iterator->second.Group == group) {
            iterator = m_voices.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void NullAudioBackend::StopLoopingPlaybacks() {
    std::lock_guard lock(m_mutex);
    for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
        if (iterator->second.Loop) {
            iterator = m_voices.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void NullAudioBackend::Update(float deltaSeconds) {
    std::lock_guard lock(m_mutex);
    for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
        Voice& voice = iterator->second;
        if (!voice.Playing || voice.Paused) {
            ++iterator;
            continue;
        }
        voice.ElapsedSeconds += std::max(deltaSeconds, 0.0f);
        const double duration = voice.Clip != nullptr ? voice.Clip->GetDurationSeconds() : 0.0;
        if (!voice.Loop && duration > 0.0 && voice.ElapsedSeconds >= static_cast<float>(duration)) {
            iterator = m_voices.erase(iterator);
            continue;
        }
        ++iterator;
    }
}

} // namespace Xora2D