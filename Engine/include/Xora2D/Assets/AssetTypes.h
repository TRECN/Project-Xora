#pragma once

#include <cstdint>

namespace Xora2D {

enum class AssetType : std::uint8_t {
    Unknown = 0,
    Texture2D,
    Audio,
    PythonScript,
    AnimationClip,
    Prefab,
    Tileset,
    Tilemap,
};

} // namespace Xora2D