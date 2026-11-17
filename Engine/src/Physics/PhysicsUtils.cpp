#include "Xora2D/Physics/PhysicsUtils.h"

#include <algorithm>
#include <cmath>

namespace Xora2D {
namespace PhysicsUtils {

float PixelsToMeters(float pixels, float pixelsPerMeter) {
    const float ppm = std::max(pixelsPerMeter, 0.0001f);
    return pixels / ppm;
}

float MetersToPixels(float meters, float pixelsPerMeter) {
    return meters * pixelsPerMeter;
}

glm::vec2 PixelsToMeters(const glm::vec2& pixels, float pixelsPerMeter) {
    return {PixelsToMeters(pixels.x, pixelsPerMeter), PixelsToMeters(pixels.y, pixelsPerMeter)};
}

glm::vec2 MetersToPixels(const glm::vec2& meters, float pixelsPerMeter) {
    return {MetersToPixels(meters.x, pixelsPerMeter), MetersToPixels(meters.y, pixelsPerMeter)};
}

float MetersFromPixels(float pixels, float pixelsPerMeter) {
    return PixelsToMeters(pixels, pixelsPerMeter);
}

} // namespace PhysicsUtils
} // namespace Xora2D