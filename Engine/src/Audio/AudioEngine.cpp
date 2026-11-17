#include "Xora2D/Audio/AudioEngine.h"

#include "AudioBackend.h"

#include "Xora2D/Audio/AudioClip.h"
#include "Xora2D/Core/Logger.h"

#include <algorithm>
#include <utility>

namespace Xora2D {

struct AudioEngine::Impl {
    std::unique_ptr<AudioBackend> Backend;
    float MasterVolume = 1.0f;
    float MusicVolume = 1.0f;
    float SoundEffectsVolume = 1.0f;
};

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    PrepareShutdown();
}

AudioInitResult AudioEngine::Initialize(const AudioEngineSettings& settings) {
    std::lock_guard lock(m_mutex);
    if (m_initialized) {
        return AudioInitResult{true, m_deviceAvailable, "Audio engine already initialized"};
    }

    m_impl = std::make_unique<Impl>();
    if (settings.Backend == AudioBackendKind::Null) {
        m_impl->Backend = std::make_unique<NullAudioBackend>();
    } else {
        m_impl->Backend = CreateMiniaudioBackend();
    }

    AudioInitResult result = m_impl->Backend->Initialize();
    if (!result.Success && settings.Backend == AudioBackendKind::Auto) {
        XORA_LOG_WARN("Audio device init failed ({}); using null backend", result.Message);
        m_impl->Backend = std::make_unique<NullAudioBackend>();
        result = m_impl->Backend->Initialize();
    }

    m_initialized = result.Success;
    m_deviceAvailable = result.DeviceAvailable;
    if (m_initialized) {
        m_impl->Backend->ApplyGroupVolumes(m_impl->MasterVolume, m_impl->MusicVolume, m_impl->SoundEffectsVolume);
    }
    return result;
}

void AudioEngine::Shutdown() {
    PrepareShutdown();
}

void AudioEngine::PrepareShutdown() {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->StopAll();
        m_impl->Backend->Shutdown();
        m_impl->Backend.reset();
    }
    m_initialized = false;
    m_deviceAvailable = false;
}

bool AudioEngine::IsInitialized() const {
    std::lock_guard lock(m_mutex);
    return m_initialized;
}

bool AudioEngine::IsDeviceAvailable() const {
    std::lock_guard lock(m_mutex);
    return m_deviceAvailable;
}

AudioSource AudioEngine::CreateSource() {
    AudioSource source;
    source.m_engine = this;
    return source;
}

void AudioEngine::ReleaseSource(AudioSource& source) {
    if (source.m_playbackId != kInvalidAudioPlaybackId) {
        StopPlayback(source.m_playbackId);
    }
    source.m_playbackId = kInvalidAudioPlaybackId;
    source.m_engine = nullptr;
}

AudioPlaybackId AudioEngine::PlayClip(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume,
                                        float pitch, bool loop) {
    std::lock_guard lock(m_mutex);
    if (!m_initialized || m_impl == nullptr || m_impl->Backend == nullptr || clip == nullptr) {
        return kInvalidAudioPlaybackId;
    }
    return m_impl->Backend->Play(clip, group, volume, pitch, loop, m_impl->MasterVolume, m_impl->MusicVolume,
                                 m_impl->SoundEffectsVolume);
}

void AudioEngine::PausePlayback(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->Pause(id);
    }
}

void AudioEngine::ResumePlayback(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->Resume(id);
    }
}

void AudioEngine::StopPlayback(AudioPlaybackId id) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->Stop(id);
    }
}

bool AudioEngine::IsPlaybackActive(AudioPlaybackId id) const {
    std::lock_guard lock(m_mutex);
    if (m_impl == nullptr || m_impl->Backend == nullptr) {
        return false;
    }
    return m_impl->Backend->IsPlaying(id);
}

bool AudioEngine::IsPlaybackPaused(AudioPlaybackId id) const {
    std::lock_guard lock(m_mutex);
    if (m_impl == nullptr || m_impl->Backend == nullptr) {
        return false;
    }
    return m_impl->Backend->IsPaused(id);
}

void AudioEngine::SetPlaybackVolume(AudioPlaybackId id, float volume) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->SetVolume(id, volume);
    }
}

void AudioEngine::SetPlaybackPitch(AudioPlaybackId id, float pitch) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->SetPitch(id, pitch);
    }
}

void AudioEngine::SetPlaybackLooping(AudioPlaybackId id, bool loop) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->SetLooping(id, loop);
    }
}

void AudioEngine::SetMasterVolume(float volume) {
    std::lock_guard lock(m_mutex);
    if (m_impl == nullptr) {
        return;
    }
    m_impl->MasterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_impl->Backend != nullptr) {
        m_impl->Backend->ApplyGroupVolumes(m_impl->MasterVolume, m_impl->MusicVolume, m_impl->SoundEffectsVolume);
    }
}

void AudioEngine::SetMusicVolume(float volume) {
    std::lock_guard lock(m_mutex);
    if (m_impl == nullptr) {
        return;
    }
    m_impl->MusicVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_impl->Backend != nullptr) {
        m_impl->Backend->ApplyGroupVolumes(m_impl->MasterVolume, m_impl->MusicVolume, m_impl->SoundEffectsVolume);
    }
}

void AudioEngine::SetSoundEffectsVolume(float volume) {
    std::lock_guard lock(m_mutex);
    if (m_impl == nullptr) {
        return;
    }
    m_impl->SoundEffectsVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_impl->Backend != nullptr) {
        m_impl->Backend->ApplyGroupVolumes(m_impl->MasterVolume, m_impl->MusicVolume, m_impl->SoundEffectsVolume);
    }
}

float AudioEngine::GetMasterVolume() const {
    std::lock_guard lock(m_mutex);
    return m_impl != nullptr ? m_impl->MasterVolume : 1.0f;
}

float AudioEngine::GetMusicVolume() const {
    std::lock_guard lock(m_mutex);
    return m_impl != nullptr ? m_impl->MusicVolume : 1.0f;
}

float AudioEngine::GetSoundEffectsVolume() const {
    std::lock_guard lock(m_mutex);
    return m_impl != nullptr ? m_impl->SoundEffectsVolume : 1.0f;
}

AudioListener& AudioEngine::GetListener() {
    return m_listener;
}

const AudioListener& AudioEngine::GetListener() const {
    return m_listener;
}

void AudioEngine::PauseAll() {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->PauseAll();
    }
}

void AudioEngine::ResumeAll() {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->ResumeAll();
    }
}

void AudioEngine::StopAll() {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->StopAll();
    }
}

void AudioEngine::StopPlaybacksInGroup(AudioGroup group) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->StopPlaybacksInGroup(group);
    }
}

void AudioEngine::StopLoopingPlaybacks() {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->StopLoopingPlaybacks();
    }
}

void AudioEngine::Update(float deltaSeconds) {
    std::lock_guard lock(m_mutex);
    if (m_impl != nullptr && m_impl->Backend != nullptr) {
        m_impl->Backend->Update(deltaSeconds);
    }
}

void AudioSource::Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch, bool loop) {
    if (m_engine == nullptr) {
        return;
    }
    if (m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->StopPlayback(m_playbackId);
    }
    m_playbackId = m_engine->PlayClip(clip, group, volume, pitch, loop);
}

void AudioSource::Pause() {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->PausePlayback(m_playbackId);
    }
}

void AudioSource::Resume() {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->ResumePlayback(m_playbackId);
    }
}

void AudioSource::Stop() {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->StopPlayback(m_playbackId);
        m_playbackId = kInvalidAudioPlaybackId;
    }
}

void AudioSource::SetVolume(float volume) {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->SetPlaybackVolume(m_playbackId, volume);
    }
}

void AudioSource::SetPitch(float pitch) {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->SetPlaybackPitch(m_playbackId, pitch);
    }
}

void AudioSource::SetLooping(bool loop) {
    if (m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId) {
        m_engine->SetPlaybackLooping(m_playbackId, loop);
    }
}

bool AudioSource::IsPlaying() const {
    return m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId &&
           m_engine->IsPlaybackActive(m_playbackId);
}

bool AudioSource::IsPaused() const {
    return m_engine != nullptr && m_playbackId != kInvalidAudioPlaybackId &&
           m_engine->IsPlaybackPaused(m_playbackId);
}

} // namespace Xora2D