#pragma once

#include "Xora2D/Animation/AnimationTypes.h"

#include <glm/vec4.hpp>

#include <optional>

namespace Xora2D {

struct SpriteSheetValidationResult {
    bool Valid = false;
    std::string Message;
};

[[nodiscard]] SpriteSheetValidationResult ValidateSpriteSheetLayout(const SpriteSheetLayout& layout,
                                                                    std::uint32_t textureWidth,
                                                                    std::uint32_t textureHeight);

// Returns UV rect (u0, v0, u1, v1) for a logical frame index within the layout.
[[nodiscard]] std::optional<glm::vec4> CalculateSpriteSheetFrameUV(const SpriteSheetLayout& layout,
                                                                 std::uint32_t textureWidth,
                                                                 std::uint32_t textureHeight,
                                                                 int frameIndex);

void BuildQuadUvsFromRect(const glm::vec4& uvRect, glm::vec2 outCorners[4]);

} // namespace Xora2D