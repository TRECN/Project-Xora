#include "AudioDecode.h"
#include "AudioBackend.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "Xora2D/Audio/AudioClip.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Xora2D {
namespace {

float ClampVolume(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

bool DecodeAudioFileToPcm(const std::filesystem::path& filepath, std::uint32_t& channels, std::uint32_t& sampleRate,
                          std::uint64_t& frameCount, std::shared_ptr<std::vector<float>>& pcm, std::string& error) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder{};
    const ma_result openResult = ma_decoder_init_file(filepath.string().c_str(), &config, &decoder);
    if (openResult != MA_SUCCESS) {
        error = "ma_decoder_init_file failed";
        return false;
    }

    channels = decoder.outputChannels;
    sampleRate = decoder.outputSampleRate;

    pcm = std::make_shared<std::vector<float>>();
    pcm->reserve(static_cast<std::size_t>(sampleRate) * channels * 2);

    constexpr std::size_t kChunkFrames = 4096;
    std::vector<float> chunk(kChunkFrames * std::max(channels, 1u));

    ma_uint64 totalFrames = 0;
    while (true) {
        ma_uint64 framesRead = 0;
        const ma_result readResult =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);
        if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
            ma_decoder_uninit(&decoder);
            error = "Failed while decoding PCM";
            return false;
        }
        if (framesRead == 0) {
            break;
        }
        pcm->insert(pcm->end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(framesRead * channels));
        totalFrames += framesRead;
    }

    ma_decoder_uninit(&decoder);
    frameCount = totalFrames;
    if (frameCount == 0 || pcm->empty()) {
        error = "Audio file contains no PCM frames";
        return false;
    }
    return true;
}

class MiniaudioBackend final : public AudioBackend {
public:
    AudioInitResult Initialize() override {
        AudioInitResult result{};
        ma_engine_config config = ma_engine_config_init();
        config.noAutoStart = MA_FALSE;
        const ma_result initResult = ma_engine_init(&config, &m_engine);
        if (initResult != MA_SUCCESS) {
            result.Message = "miniaudio engine initialization failed";
            return result;
        }
        m_initialized = true;
        result.Success = true;
        result.DeviceAvailable = true;
        return result;
    }

    void Shutdown() override {
        std::lock_guard lock(m_mutex);
        for (auto& [id, voice] : m_voices) {
            if (voice.Sound != nullptr) {
                ma_sound_uninit(voice.Sound.get());
            }
        }
        m_voices.clear();
        if (m_initialized) {
            ma_engine_uninit(&m_engine);
            m_initialized = false;
        }
    }

    AudioPlaybackId Play(std::shared_ptr<const AudioClip> clip, AudioGroup group, float volume, float pitch, bool loop,
                         float masterVolume, float musicVolume, float sfxVolume) override {
        if (clip == nullptr || !clip->IsValid()) {
            return kInvalidAudioPlaybackId;
        }
        const float* pcm = clip->GetPcmData();
        if (pcm == nullptr) {
            return kInvalidAudioPlaybackId;
        }

        std::lock_guard lock(m_mutex);
        const AudioPlaybackId id = m_nextId++;

        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            ma_format_f32, clip->GetChannelCount(), clip->GetFrameCount(), pcm, nullptr);
        auto buffer = std::make_unique<ma_audio_buffer>();
        if (ma_audio_buffer_init(&bufferConfig, buffer.get()) != MA_SUCCESS) {
            return kInvalidAudioPlaybackId;
        }

        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_data_source(&m_engine, buffer.get(), 0, nullptr, sound.get()) != MA_SUCCESS) {
            ma_audio_buffer_uninit(buffer.get());
            return kInvalidAudioPlaybackId;
        }

        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        const float effective =
            ClampVolume(volume) * GroupGain(group, musicVolume, sfxVolume) * ClampVolume(masterVolume);
        ma_sound_set_volume(sound.get(), effective);
        ma_sound_set_pitch(sound.get(), std::max(pitch, 0.01f));
        ma_sound_start(sound.get());

        Voice voice{};
        voice.Sound = std::move(sound);
        voice.Buffer = std::move(buffer);
        voice.Clip = std::move(clip);
        voice.Group = group;
        voice.Volume = volume;
        voice.Pitch = pitch;
        voice.Loop = loop;
        m_voices.emplace(id, std::move(voice));
        return id;
    }

    void Pause(AudioPlaybackId id) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return;
        }
        ma_sound_stop(found->second.Sound.get());
        found->second.Paused = true;
    }

    void Resume(AudioPlaybackId id) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return;
        }
        ma_sound_start(found->second.Sound.get());
        found->second.Paused = false;
    }

    void Stop(AudioPlaybackId id) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end()) {
            return;
        }
        if (found->second.Sound != nullptr) {
            ma_sound_stop(found->second.Sound.get());
        }
        if (found->second.Sound != nullptr) {
            ma_sound_uninit(found->second.Sound.get());
        }
        m_voices.erase(found);
    }

    bool IsPlaying(AudioPlaybackId id) const override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return false;
        }
        return ma_sound_is_playing(found->second.Sound.get()) == MA_TRUE;
    }

    bool IsPaused(AudioPlaybackId id) const override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end()) {
            return false;
        }
        return found->second.Paused;
    }

    void SetVolume(AudioPlaybackId id, float volume) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return;
        }
        found->second.Volume = volume;
        const float effective =
            ClampVolume(volume) * GroupGain(found->second.Group, m_music, m_sfx) * ClampVolume(m_master);
        ma_sound_set_volume(found->second.Sound.get(), effective);
    }

    void SetPitch(AudioPlaybackId id, float pitch) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return;
        }
        found->second.Pitch = pitch;
        ma_sound_set_pitch(found->second.Sound.get(), std::max(pitch, 0.01f));
    }

    void SetLooping(AudioPlaybackId id, bool loop) override {
        std::lock_guard lock(m_mutex);
        const auto found = m_voices.find(id);
        if (found == m_voices.end() || found->second.Sound == nullptr) {
            return;
        }
        found->second.Loop = loop;
        ma_sound_set_looping(found->second.Sound.get(), loop ? MA_TRUE : MA_FALSE);
    }

    void ApplyGroupVolumes(float masterVolume, float musicVolume, float sfxVolume) override {
        std::lock_guard lock(m_mutex);
        m_master = masterVolume;
        m_music = musicVolume;
        m_sfx = sfxVolume;
        for (auto& [id, voice] : m_voices) {
            if (voice.Sound == nullptr) {
                continue;
            }
            const float effective =
                ClampVolume(voice.Volume) * GroupGain(voice.Group, m_music, m_sfx) * ClampVolume(m_master);
            ma_sound_set_volume(voice.Sound.get(), effective);
            (void)id;
        }
    }

    void PauseAll() override {
        std::lock_guard lock(m_mutex);
        for (auto& [id, voice] : m_voices) {
            (void)id;
            if (voice.Sound != nullptr) {
                ma_sound_stop(voice.Sound.get());
                voice.Paused = true;
            }
        }
    }

    void ResumeAll() override {
        std::lock_guard lock(m_mutex);
        for (auto& [id, voice] : m_voices) {
            (void)id;
            if (voice.Sound != nullptr && voice.Paused) {
                ma_sound_start(voice.Sound.get());
                voice.Paused = false;
            }
        }
    }

    void StopAll() override {
        std::lock_guard lock(m_mutex);
        for (auto& [id, voice] : m_voices) {
            (void)id;
            if (voice.Sound != nullptr) {
                ma_sound_uninit(voice.Sound.get());
            }
        }
        m_voices.clear();
    }

    void StopPlaybacksInGroup(AudioGroup group) override {
        std::lock_guard lock(m_mutex);
        for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
            if (iterator->second.Group == group) {
                if (iterator->second.Sound != nullptr) {
                    ma_sound_stop(iterator->second.Sound.get());
                    ma_sound_uninit(iterator->second.Sound.get());
                }
                iterator = m_voices.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void StopLoopingPlaybacks() override {
        std::lock_guard lock(m_mutex);
        for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
            if (!iterator->second.Loop) {
                ++iterator;
                continue;
            }
            if (iterator->second.Sound != nullptr) {
                ma_sound_stop(iterator->second.Sound.get());
                ma_sound_uninit(iterator->second.Sound.get());
            }
            iterator = m_voices.erase(iterator);
        }
    }

    void Update(float /*deltaSeconds*/) override {
        std::lock_guard lock(m_mutex);
        for (auto iterator = m_voices.begin(); iterator != m_voices.end();) {
            Voice& voice = iterator->second;
            if (voice.Sound == nullptr) {
                iterator = m_voices.erase(iterator);
                continue;
            }
            if (!voice.Loop && ma_sound_at_end(voice.Sound.get()) == MA_TRUE) {
                ma_sound_uninit(voice.Sound.get());
                iterator = m_voices.erase(iterator);
                continue;
            }
            ++iterator;
        }
    }

private:
    static float GroupGain(AudioGroup group, float musicVolume, float sfxVolume) {
        return group == AudioGroup::Music ? ClampVolume(musicVolume) : ClampVolume(sfxVolume);
    }

    struct Voice {
        std::unique_ptr<ma_sound> Sound;
        std::unique_ptr<ma_audio_buffer> Buffer;
        std::shared_ptr<const AudioClip> Clip;
        AudioGroup Group = AudioGroup::SoundEffects;
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool Paused = false;
    };

    mutable std::mutex m_mutex;
    ma_engine m_engine{};
    bool m_initialized = false;
    std::unordered_map<AudioPlaybackId, Voice> m_voices;
    AudioPlaybackId m_nextId = 1;
    float m_master = 1.0f;
    float m_music = 1.0f;
    float m_sfx = 1.0f;
};

std::unique_ptr<AudioBackend> CreateMiniaudioBackend() {
    return std::make_unique<MiniaudioBackend>();
}

} // namespace Xora2D