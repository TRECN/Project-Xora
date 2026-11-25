#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetResult.h"
#include "Xora2D/Particles/ParticleTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Xora2D {

// File format version for forward-compatibility checks.
inline constexpr int kParticleEffectFormatVersion = 1;

// Serializable configuration of a particle effect.
// Runtime particles are NOT stored here: instances live in ParticleSystemComponent.
struct ParticleEffectConfig {
    // ---- Emission ----
    std::uint32_t MaxParticles   = 500;
    float EmissionRate           = 50.0f;  // particles per second (continuous)
    bool  Looping                = true;
    float Duration               = 2.0f;   // seconds (ignored when Looping)
    float StartDelay             = 0.0f;   // seconds before first emission
    std::vector<ParticleBurst> Bursts;
    std::uint64_t RandomSeed     = 12345;  // deterministic seed (0 = random each play)

    // ---- Particle lifetime & kinematics ----
    float LifetimeMin = 1.0f;
    float LifetimeMax = 2.0f;
    float SpeedMin    = 50.0f;
    float SpeedMax    = 100.0f;
    glm::vec2 Direction{0.0f, 1.0f};  // base direction (normalized)
    float SpreadAngle = 45.0f;         // half-cone angle in degrees

    glm::vec2 Gravity{0.0f, 0.0f};    // pixels per second squared

    // ---- Size ----
    float SizeBegin = 20.0f;
    float SizeEnd   = 0.0f;
    FloatCurve SizeCurve;              // multiplies SizeBegin..SizeEnd interpolation

    // ---- Rotation ----
    float RotationBegin    = 0.0f;    // radians
    float RotationEnd      = 0.0f;
    float AngularVelocityMin = 0.0f;  // radians per second (overrides Begin/End per-particle)
    float AngularVelocityMax = 0.0f;

    // ---- Color & opacity ----
    glm::vec4 ColorBegin{1.0f, 0.5f, 0.0f, 1.0f};
    glm::vec4 ColorEnd{1.0f, 0.0f, 0.0f, 0.0f};
    ColorGradient ColorOverLifetime;   // multiplied on top of Begin/End lerp
    FloatCurve    OpacityCurve;        // additional alpha multiplier

    // ---- Texture ----
    AssetHandle TextureAsset{};        // optional; particles are colored quads when invalid

    // ---- Simulation ----
    bool WorldSpace = false;           // true: particles move in world space, false: local to emitter
};

// Immutable loaded particle effect asset (cached by AssetManager).
class ParticleEffectAsset {
public:
    [[nodiscard]] bool IsValid() const { return m_valid; }
    [[nodiscard]] const ParticleEffectConfig& GetConfig() const { return m_config; }
    [[nodiscard]] const std::string& GetRelativePath() const { return m_relativePath; }

    static AssetResult LoadFromFile(const std::filesystem::path& absolutePath,
                                    const std::string& relativePath,
                                    ParticleEffectAsset& output);

    // Saves a config to a versioned YAML file. Creates parent directories.
    static AssetResult SaveToFile(const std::filesystem::path& absolutePath,
                                  const ParticleEffectConfig& config);

private:
    bool   m_valid = false;
    std::string m_relativePath;
    ParticleEffectConfig m_config;
};

} // namespace Xora2D
