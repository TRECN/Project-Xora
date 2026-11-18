#pragma once

#include "Xora2D/Animation/AnimationController.h"
#include "Xora2D/Animation/AnimationTypes.h"
#include "Xora2D/Assets/AssetHandle.h"

#include <glm/vec4.hpp>

#include <string>
#include <vector>

namespace Xora2D {

struct AnimationComponent {
    // Persisted configuration
    std::vector<AssetHandle> ClipAssets;
    AssetHandle DefaultClip{};
    std::string DefaultClipName;
    float PlaybackSpeed = 1.0f;
    bool Looping = true;
    bool Autoplay = true;

    // Runtime playback
    AssetHandle ActiveClip{};
    AnimationPlaybackState State = AnimationPlaybackState::Stopped;
    int CurrentFrameIndex = 0;
    glm::vec4 FrameUV{0.0f, 0.0f, 1.0f, 1.0f};
    bool FrameUVValid = false;

    // Not serialized — owned playback logic
    AnimationController Controller;
    bool PendingStartEvent = false;
    bool PendingCompletionEvent = false;
};

} // namespace Xora2D