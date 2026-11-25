#include "Xora2D/Particles/ParticleTypes.h"

#include <algorithm>
#include <glm/common.hpp>

namespace Xora2D {

// ---- FloatCurve ----

float FloatCurve::Evaluate(float t, float defaultValue) const {
    if (Keys.empty()) return defaultValue;
    if (Keys.size() == 1) return Keys[0].Value;

    t = glm::clamp(t, 0.0f, 1.0f);

    if (t <= Keys.front().Time) return Keys.front().Value;
    if (t >= Keys.back().Time)  return Keys.back().Value;

    for (std::size_t i = 1; i < Keys.size(); ++i) {
        if (t <= Keys[i].Time) {
            const float span = Keys[i].Time - Keys[i - 1].Time;
            if (span <= 0.0f) return Keys[i - 1].Value;
            const float local = (t - Keys[i - 1].Time) / span;
            return glm::mix(Keys[i - 1].Value, Keys[i].Value, local);
        }
    }
    return Keys.back().Value;
}

FloatCurve FloatCurve::Constant(float value) {
    FloatCurve curve;
    curve.Keys.push_back({0.0f, value});
    curve.Keys.push_back({1.0f, value});
    return curve;
}

FloatCurve FloatCurve::Linear(float a, float b) {
    FloatCurve curve;
    curve.Keys.push_back({0.0f, a});
    curve.Keys.push_back({1.0f, b});
    return curve;
}

// ---- ColorGradient ----

glm::vec4 ColorGradient::Evaluate(float t) const {
    if (Keys.empty()) return glm::vec4(1.0f);
    if (Keys.size() == 1) return Keys[0].Color;

    t = glm::clamp(t, 0.0f, 1.0f);

    if (t <= Keys.front().Time) return Keys.front().Color;
    if (t >= Keys.back().Time)  return Keys.back().Color;

    for (std::size_t i = 1; i < Keys.size(); ++i) {
        if (t <= Keys[i].Time) {
            const float span = Keys[i].Time - Keys[i - 1].Time;
            if (span <= 0.0f) return Keys[i - 1].Color;
            const float local = (t - Keys[i - 1].Time) / span;
            return glm::mix(Keys[i - 1].Color, Keys[i].Color, local);
        }
    }
    return Keys.back().Color;
}

ColorGradient ColorGradient::Constant(const glm::vec4& color) {
    ColorGradient grad;
    grad.Keys.push_back({0.0f, color});
    grad.Keys.push_back({1.0f, color});
    return grad;
}

ColorGradient ColorGradient::Linear(const glm::vec4& a, const glm::vec4& b) {
    ColorGradient grad;
    grad.Keys.push_back({0.0f, a});
    grad.Keys.push_back({1.0f, b});
    return grad;
}

} // namespace Xora2D
