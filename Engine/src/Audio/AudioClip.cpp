#include "Xora2D/Audio/AudioClip.h"

#include "AudioDecode.h"

namespace Xora2D {

struct AudioClip::Data {
    std::uint32_t Channels = 0;
    std::uint32_t SampleRate = 0;
    std::uint64_t FrameCount = 0;
    std::shared_ptr<std::vector<float>> Pcm;
};

bool AudioClip::IsValid() const {
    return m_data != nullptr && m_data->Pcm != nullptr && !m_data->Pcm->empty();
}

std::uint32_t AudioClip::GetChannelCount() const {
    return m_data ? m_data->Channels : 0;
}

std::uint32_t AudioClip::GetSampleRate() const {
    return m_data ? m_data->SampleRate : 0;
}

std::uint64_t AudioClip::GetFrameCount() const {
    return m_data ? m_data->FrameCount : 0;
}

const float* AudioClip::GetPcmData() const {
    if (m_data == nullptr || m_data->Pcm == nullptr || m_data->Pcm->empty()) {
        return nullptr;
    }
    return m_data->Pcm->data();
}

std::size_t AudioClip::GetPcmFloatCount() const {
    if (m_data == nullptr || m_data->Pcm == nullptr) {
        return 0;
    }
    return m_data->Pcm->size();
}

double AudioClip::GetDurationSeconds() const {
    if (!IsValid() || m_data->SampleRate == 0) {
        return 0.0;
    }
    return static_cast<double>(m_data->FrameCount) / static_cast<double>(m_data->SampleRate);
}

AudioLoadResult AudioClip::LoadFromFile(const std::filesystem::path& filepath, std::shared_ptr<AudioClip>& outClip) {
    AudioLoadResult result{};
    if (filepath.empty() || !std::filesystem::exists(filepath)) {
        result.Message = "Audio file does not exist";
        return result;
    }

    auto clip = std::shared_ptr<AudioClip>(new AudioClip());
    clip->m_data = std::make_shared<Data>();

    std::string error;
    if (!DecodeAudioFileToPcm(filepath, clip->m_data->Channels, clip->m_data->SampleRate, clip->m_data->FrameCount,
                              clip->m_data->Pcm, error)) {
        result.Message = error.empty() ? "Failed to decode audio file" : error;
        return result;
    }

    outClip = std::move(clip);
    result.Success = true;
    return result;
}

} // namespace Xora2D