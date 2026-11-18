#include "Xora2D/Animation/SpriteSheetLayout.h"

#include <algorithm>
#include <cmath>

namespace Xora2D {

SpriteSheetValidationResult ValidateSpriteSheetLayout(const SpriteSheetLayout& layout,
                                                      const std::uint32_t textureWidth,
                                                      const std::uint32_t textureHeight) {
    SpriteSheetValidationResult result{};
    if (textureWidth == 0 || textureHeight == 0) {
        result.Message = "Texture dimensions must be non-zero";
        return result;
    }
    if (layout.Columns <= 0 || layout.Rows <= 0) {
        result.Message = "Sprite sheet columns and rows must be positive";
        return result;
    }
    if (layout.FrameWidthPixels <= 0.0f || layout.FrameHeightPixels <= 0.0f) {
        result.Message = "Frame width and height must be positive";
        return result;
    }
    if (layout.FrameCount <= 0) {
        result.Message = "Frame count must be positive";
        return result;
    }
    const float sheetWidth = static_cast<float>(layout.Columns) * layout.FrameWidthPixels;
    const float sheetHeight = static_cast<float>(layout.Rows) * layout.FrameHeightPixels;
    if (sheetWidth > static_cast<float>(textureWidth) + 0.001f ||
        sheetHeight > static_cast<float>(textureHeight) + 0.001f) {
        result.Message = "Sprite sheet layout exceeds texture bounds";
        return result;
    }
    if (layout.StartFrame < 0) {
        result.Message = "Start frame must be non-negative";
        return result;
    }
    const int gridCapacity = layout.Columns * layout.Rows;
    if (layout.StartFrame + layout.FrameCount > gridCapacity) {
        result.Message = "Frame range extends beyond sprite sheet grid";
        return result;
    }
    if (layout.Order == SpriteSheetFrameOrder::CustomSequence &&
        static_cast<int>(layout.CustomSequence.size()) < layout.FrameCount) {
        result.Message = "Custom sequence does not contain enough frame indices";
        return result;
    }
    result.Valid = true;
    return result;
}

std::optional<glm::vec4> CalculateSpriteSheetFrameUV(const SpriteSheetLayout& layout,
                                                     const std::uint32_t textureWidth,
                                                     const std::uint32_t textureHeight,
                                                     const int frameIndex) {
    if (frameIndex < 0 || frameIndex >= layout.FrameCount) {
        return std::nullopt;
    }

    const SpriteSheetValidationResult validation = ValidateSpriteSheetLayout(layout, textureWidth, textureHeight);
    if (!validation.Valid) {
        return std::nullopt;
    }

    int gridIndex = layout.StartFrame;
    if (layout.Order == SpriteSheetFrameOrder::CustomSequence) {
        if (frameIndex >= static_cast<int>(layout.CustomSequence.size())) {
            return std::nullopt;
        }
        gridIndex = layout.CustomSequence[static_cast<std::size_t>(frameIndex)];
    } else {
        gridIndex = layout.StartFrame + frameIndex;
    }

    if (gridIndex < 0 || gridIndex >= layout.Columns * layout.Rows) {
        return std::nullopt;
    }

    int column = 0;
    int row = 0;
    if (layout.Order == SpriteSheetFrameOrder::ColumnMajor) {
        column = gridIndex / layout.Rows;
        row = gridIndex % layout.Rows;
    } else {
        column = gridIndex % layout.Columns;
        row = gridIndex / layout.Columns;
    }

    const float texW = static_cast<float>(textureWidth);
    const float texH = static_cast<float>(textureHeight);
    const float pixelX = static_cast<float>(column) * layout.FrameWidthPixels;
    const float pixelYTop = static_cast<float>(row) * layout.FrameHeightPixels;
    const float pixelYBottom = pixelYTop + layout.FrameHeightPixels;

    const float u0 = pixelX / texW;
    const float u1 = (pixelX + layout.FrameWidthPixels) / texW;
    // Image files load with stbi flip: v=0 is bottom of displayed sprite.
    const float v0 = (texH - pixelYBottom) / texH;
    const float v1 = (texH - pixelYTop) / texH;

    return glm::vec4(u0, v0, u1, v1);
}

void BuildQuadUvsFromRect(const glm::vec4& uvRect, glm::vec2 outCorners[4]) {
    outCorners[0] = {uvRect.x, uvRect.y};
    outCorners[1] = {uvRect.z, uvRect.y};
    outCorners[2] = {uvRect.z, uvRect.w};
    outCorners[3] = {uvRect.x, uvRect.w};
}

} // namespace Xora2D