#pragma once

#include "Xora2D/Scene/Components.h"

#include <glm/glm.hpp>

namespace Xora2DEditor {

// Inspector-facing view of a TransformComponent. The engine stores rotation in radians; the inspector shows degrees.
// The engine is 2D, so there is no Z axis (TransformComponent stores vec2 translation/scale).
struct TransformEditValues {
    glm::vec2 Position{0.0f};
    float RotationDegrees = 0.0f;
    glm::vec2 Scale{1.0f};
};

[[nodiscard]] float RadiansToDegrees(float radians);
[[nodiscard]] float DegreesToRadians(float degrees);

[[nodiscard]] TransformEditValues ReadTransformForEditing(const Xora2D::TransformComponent& transform);

// Writes only the fields that differ between `before` (what the inspector read this frame) and `after` (what the
// user edited), so editing one axis never touches the others and rotation does not drift through a
// radians -> degrees -> radians round-trip. Returns true if the component changed (its matrix is recalculated).
bool ApplyTransformEdit(Xora2D::TransformComponent& transform,
                        const TransformEditValues& before,
                        const TransformEditValues& after);

} // namespace Xora2DEditor
