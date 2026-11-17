#pragma once

#include <glm/vec2.hpp>

namespace Xora2D {
namespace PhysicsUtils {

float PixelsToMeters(float pixels, float pixelsPerMeter);
float MetersToPixels(float meters, float pixelsPerMeter);
glm::vec2 PixelsToMeters(const glm::vec2& pixels, float pixelsPerMeter);
glm::vec2 MetersToPixels(const glm::vec2& meters, float pixelsPerMeter);

float MetersFromPixels(float pixels, float pixelsPerMeter);

} // namespace PhysicsUtils
} // namespace Xora2D