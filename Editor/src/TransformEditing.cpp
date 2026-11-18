#include "Xora2DEditor/TransformEditing.h"

#include <glm/gtc/constants.hpp>

namespace Xora2DEditor {

float RadiansToDegrees(float radians) {
    return radians * (180.0f / glm::pi<float>());
}

float DegreesToRadians(float degrees) {
    return degrees * (glm::pi<float>() / 180.0f);
}

TransformEditValues ReadTransformForEditing(const Xora2D::TransformComponent& transform) {
    TransformEditValues values;
    values.Position = transform.Translation;
    values.RotationDegrees = RadiansToDegrees(transform.RotationRadians);
    values.Scale = transform.Scale;
    return values;
}

bool ApplyTransformEdit(Xora2D::TransformComponent& transform,
                        const TransformEditValues& before,
                        const TransformEditValues& after) {
    bool changed = false;
    for (int axis = 0; axis < 2; ++axis) {
        if (after.Position[axis] != before.Position[axis]) {
            transform.Translation[axis] = after.Position[axis];
            changed = true;
        }
        if (after.Scale[axis] != before.Scale[axis]) {
            transform.Scale[axis] = after.Scale[axis];
            changed = true;
        }
    }
    if (after.RotationDegrees != before.RotationDegrees) {
        transform.RotationRadians = DegreesToRadians(after.RotationDegrees);
        changed = true;
    }
    if (changed) {
        transform.RecalculateLocalMatrix();
    }
    return changed;
}

} // namespace Xora2DEditor
