#pragma once

#include "Xora2D/Core/UUID.h"

#include <cstdint>
#include <string>

namespace Xora2D {

enum class AnimationEventType : std::uint8_t {
    Started = 0,
    Completed,
    FrameChanged,
    FrameMarker,
};

struct AnimationEvent {
    AnimationEventType Type = AnimationEventType::Started;
    UUID EntityId{};
    std::string ClipName;
    int FrameIndex = 0;
    std::string Marker;
};

} // namespace Xora2D