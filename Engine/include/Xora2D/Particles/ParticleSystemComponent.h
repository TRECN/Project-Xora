#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Particles/ParticleTypes.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace Xora2D {

class Texture2D;

// Playback state of one emitter instance.
enum class ParticlePlayState : std::uint8_t {
    Stopped = 0,
    Playing,
    Paused,
};

// ECS component – one per entity.  All runtime particle data lives here; the asset is read-only.
// Pool is heap-allocated once at first Play / when the asset changes (bounded by MaxParticles).
struct ParticleSystemComponent {
    // ---- Asset reference ----
    AssetHandle EffectAsset{};

    // ---- Per-instance overrides ----
    bool  EmissionEnabled = true;
    float PlaybackSpeed   = 1.0f;
    bool  Loop            = true;

    // ---- Runtime state (not serialized) ----
    ParticlePlayState PlayState   = ParticlePlayState::Stopped;
    float             ElapsedTime = 0.0f;
    float             EmitAccum   = 0.0f;
    int               BurstIndex  = 0;

    // Particle pool: pre-allocated to MaxParticles; never grows during simulation.
    std::vector<Particle> Pool;
    std::uint32_t         PoolNext    = 0;
    std::uint32_t         ActiveCount = 0;

    // Loaded texture (resolved by the scene particle system).
    std::shared_ptr<Texture2D> LoadedTexture;

    // Per-instance RNG.
    std::mt19937 Rng{std::random_device{}()};

    // Statistics updated each simulation step.
    ParticleStats Stats{};

    // ---- Methods ----
    void Play();
    void Stop();
    void Pause();
    void Resume();
    void Restart();
    void Clear();

    // Returns a slot from the ring buffer (overwrites the oldest active particle when full).
    [[nodiscard]] Particle& NextSlot();
};

} // namespace Xora2D
