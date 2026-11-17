#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Audio/AudioTypes.h"

namespace Xora2D {

struct AudioSourceComponent {
    AssetHandle ClipAsset{};
    float Volume = 1.0f;
    float Pitch = 1.0f;
    bool Loop = false;
    bool PlayOnStart = false;
    bool Muted = false;
    AudioGroup Group = AudioGroup::SoundEffects;
    bool SpatialAudio = false;
};

struct AudioListenerComponent {
    bool Active = true;
};

} // namespace Xora2D