#pragma once

#include "Xora2D/Audio/AudioTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace Xora2D {

// Decoded PCM audio data. Thread-safe to read from the main thread while voices reference it.
class AudioClip {
public:
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] std::uint32_t GetChannelCount() const;
    [[nodiscard]] std::uint32_t GetSampleRate() const;
    [[nodiscard]] std::uint64_t GetFrameCount() const;
    [[nodiscard]] double GetDurationSeconds() const;

    static AudioLoadResult LoadFromFile(const std::filesystem::path& filepath,
                                        std::shared_ptr<AudioClip>& outClip);

    // Decoded interleaved PCM (backend consumes on the main thread only).
    [[nodiscard]] const float* GetPcmData() const;
    [[nodiscard]] std::size_t GetPcmFloatCount() const;

private:
    friend class AudioEngine;
    friend class AssetManager;

    AudioClip() = default;

    struct Data;
    std::shared_ptr<Data> m_data;
};

} // namespace Xora2D