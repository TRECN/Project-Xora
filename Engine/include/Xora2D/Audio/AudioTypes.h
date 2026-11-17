#pragma once

#include <cstdint>
#include <string>

namespace Xora2D {

enum class AudioGroup : std::uint8_t {
    Music = 0,
    SoundEffects,
};

struct AudioInitResult {
    bool Success = false;
    bool DeviceAvailable = false;
    std::string Message;
};

struct AudioLoadResult {
    bool Success = false;
    std::string Message;
};

using AudioPlaybackId = std::uint32_t;
constexpr AudioPlaybackId kInvalidAudioPlaybackId = 0;

enum class AudioBackendKind : std::uint8_t {
    Auto = 0,
    Null,
};

struct AudioEngineSettings {
    AudioBackendKind Backend = AudioBackendKind::Auto;
};

} // namespace Xora2D