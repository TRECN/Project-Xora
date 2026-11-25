#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace Xora2D {

// ---- Curves and gradients stored in ParticleEffectAsset ----

struct FloatCurveKey {
    float Time  = 0.0f; // [0, 1]
    float Value = 0.0f;
};

// Piecewise-linear float curve. Evaluated in [0, 1] without heap allocation.
struct FloatCurve {
    std::vector<FloatCurveKey> Keys;

    // Returns `defaultValue` when Keys is empty.
    [[nodiscard]] float Evaluate(float t, float defaultValue = 1.0f) const;
    // Convenience: flat curve at `value`.
    static FloatCurve Constant(float value);
    // Convenience: linear from `a` (t=0) to `b` (t=1).
    static FloatCurve Linear(float a, float b);
};

struct ColorGradientKey {
    float     Time  = 0.0f;             // [0, 1]
    glm::vec4 Color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Piecewise-linear RGBA gradient. Evaluated in [0, 1] without heap allocation.
struct ColorGradient {
    std::vector<ColorGradientKey> Keys;

    [[nodiscard]] glm::vec4 Evaluate(float t) const;
    static ColorGradient Constant(const glm::vec4& color);
    static ColorGradient Linear(const glm::vec4& a, const glm::vec4& b);
};

// ---- Runtime particle (one slot in the pool) ----

struct Particle {
    glm::vec2 Position{0.0f};
    glm::vec2 Velocity{0.0f};
    glm::vec2 Acceleration{0.0f}; // gravity contribution

    float LifetimeTotal     = 1.0f;
    float LifetimeRemaining = 0.0f; // 0 = dead/inactive

    float RotationBegin = 0.0f; // radians
    float RotationEnd   = 0.0f;

    float SizeBegin = 10.0f;
    float SizeEnd   = 0.0f;

    glm::vec4 ColorBegin{1.0f};
    glm::vec4 ColorEnd{1.0f};

    // Fraction of lifetime completed [0, 1]; drives property curves.
    [[nodiscard]] float LifetimeFraction() const {
        if (LifetimeTotal <= 0.0f) return 1.0f;
        return 1.0f - (LifetimeRemaining / LifetimeTotal);
    }

    [[nodiscard]] bool IsActive() const { return LifetimeRemaining > 0.0f; }
};

// ---- Burst descriptor ----

struct ParticleBurst {
    float Time  = 0.0f; // seconds after emitter starts
    int   Count = 10;
};

// ---- Particle simulation statistics ----

struct ParticleStats {
    std::uint32_t ActiveParticles  = 0;
    std::uint32_t EmittedThisFrame = 0;
    std::uint32_t PoolCapacity     = 0;
};

} // namespace Xora2D
