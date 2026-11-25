#include "Xora2D/Particles/ParticleSystemComponent.h"

namespace Xora2D {

Particle& ParticleSystemComponent::NextSlot() {
    if (Pool.empty()) {
        Pool.emplace_back();
    }
    const std::uint32_t capacity = static_cast<std::uint32_t>(Pool.size());
    Particle& slot = Pool[PoolNext % capacity];
    PoolNext = (PoolNext + 1) % capacity;
    return slot;
}

void ParticleSystemComponent::Play() {
    if (PlayState == ParticlePlayState::Playing) return;
    PlayState   = ParticlePlayState::Playing;
    ElapsedTime = 0.0f;
    EmitAccum   = 0.0f;
    BurstIndex  = 0;
}

void ParticleSystemComponent::Stop() {
    PlayState   = ParticlePlayState::Stopped;
    ElapsedTime = 0.0f;
    EmitAccum   = 0.0f;
    BurstIndex  = 0;
    Clear();
}

void ParticleSystemComponent::Pause() {
    if (PlayState == ParticlePlayState::Playing) {
        PlayState = ParticlePlayState::Paused;
    }
}

void ParticleSystemComponent::Resume() {
    if (PlayState == ParticlePlayState::Paused) {
        PlayState = ParticlePlayState::Playing;
    }
}

void ParticleSystemComponent::Restart() {
    Stop();
    Play();
}

void ParticleSystemComponent::Clear() {
    for (Particle& p : Pool) {
        p.LifetimeRemaining = 0.0f;
    }
    ActiveCount = 0;
    PoolNext    = 0;
}

} // namespace Xora2D
