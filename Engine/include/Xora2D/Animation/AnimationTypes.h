#pragma once

#include "Xora2D/Assets/AssetHandle.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Xora2D {

enum class AnimationPlaybackState : std::uint8_t {
    Stopped = 0,
    Playing,
    Paused,
    Completed,
};

// UV rectangle in texture space: (u0, v0, u1, v1) with v0 = bottom, v1 = top (OpenGL / Renderer2D convention).
struct AnimationFrame {
    glm::vec4 UVRect{0.0f, 0.0f, 1.0f, 1.0f};
    float DurationSeconds = 0.1f;
    std::string EventMarker;
};

enum class SpriteSheetFrameOrder : std::uint8_t {
    RowMajor = 0,
    ColumnMajor,
    CustomSequence,
};

struct SpriteSheetLayout {
    int Columns = 1;
    int Rows = 1;
    float FrameWidthPixels = 0.0f;
    float FrameHeightPixels = 0.0f;
    int StartFrame = 0;
    int FrameCount = 1;
    SpriteSheetFrameOrder Order = SpriteSheetFrameOrder::RowMajor;
    std::vector<int> CustomSequence;
};

struct AnimationClip {
    std::string Name;
    AssetHandle TextureAsset{};
    bool Loop = true;
    float DefaultPlaybackSpeed = 1.0f;
    std::vector<AnimationFrame> Frames;
    SpriteSheetLayout SheetLayout{};
    bool UsesExplicitFrames = false;
};

} // namespace Xora2D