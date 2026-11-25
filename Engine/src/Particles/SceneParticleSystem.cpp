#include "SceneParticleSystem.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleRendering.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include "../Scene/SceneRegistry.h"

#include <entt/entt.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace Xora2D {
namespace {

float RandRange(std::mt19937& rng, float lo, float hi) {
    if (lo >= hi) return lo;
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

glm::vec2 RandomDirection(std::mt19937& rng, const glm::vec2& base, float halfAngleDeg) {
    const float angle = glm::radians(RandRange(rng, -halfAngleDeg, halfAngleDeg));
    const float cos   = std::cos(angle);
    const float sin   = std::sin(angle);
    const glm::vec2 dir = glm::length(base) > 0.0001f ? glm::normalize(base) : glm::vec2(0.0f, 1.0f);
    // rotate dir by angle
    return glm::vec2(dir.x * cos - dir.y * sin, dir.x * sin + dir.y * cos);
}

const ParticleEffectAsset* GetAsset(AssetManager* manager, AssetHandle handle) {
    if (manager == nullptr || !handle.IsValid()) return nullptr;
    return manager->GetLoadedParticleEffect(handle).get();
}

void EnsurePoolForComponent(ParticleSystemComponent& comp, const ParticleEffectConfig& cfg) {
    const std::size_t needed = static_cast<std::size_t>(cfg.MaxParticles);
    if (comp.Pool.size() != needed) {
        comp.Pool.assign(needed, Particle{});
        comp.PoolNext    = 0;
        comp.ActiveCount = 0;
    }
}

void SpawnParticle(ParticleSystemComponent& comp, const ParticleEffectConfig& cfg, const glm::vec2& worldPos) {
    if (comp.Pool.empty()) return;

    Particle& p = comp.NextSlot();

    // Position
    p.Position = worldPos;

    // Velocity
    const glm::vec2 dir  = RandomDirection(comp.Rng, cfg.Direction, cfg.SpreadAngle);
    const float speed    = RandRange(comp.Rng, cfg.SpeedMin, cfg.SpeedMax);
    p.Velocity           = dir * speed;

    // Gravity
    p.Acceleration = cfg.Gravity;

    // Lifetime
    p.LifetimeTotal     = std::max(0.001f, RandRange(comp.Rng, cfg.LifetimeMin, cfg.LifetimeMax));
    p.LifetimeRemaining = p.LifetimeTotal;

    // Size
    p.SizeBegin = cfg.SizeBegin;
    p.SizeEnd   = cfg.SizeEnd;

    // Color
    p.ColorBegin = cfg.ColorBegin;
    p.ColorEnd   = cfg.ColorEnd;

    // Rotation
    if (std::abs(cfg.AngularVelocityMax) > 0.0001f || std::abs(cfg.AngularVelocityMin) > 0.0001f) {
        const float av = RandRange(comp.Rng, cfg.AngularVelocityMin, cfg.AngularVelocityMax);
        p.RotationBegin = cfg.RotationBegin;
        p.RotationEnd   = cfg.RotationBegin + av * p.LifetimeTotal;
    } else {
        p.RotationBegin = cfg.RotationBegin;
        p.RotationEnd   = cfg.RotationEnd;
    }

    if (!p.IsActive()) {
        ++comp.ActiveCount; // was inactive, now active
    }
}

void StepParticles(ParticleSystemComponent& comp, const ParticleEffectConfig& cfg, float dt) {
    comp.ActiveCount = 0;
    for (Particle& p : comp.Pool) {
        if (!p.IsActive()) continue;
        p.LifetimeRemaining -= dt;
        if (p.LifetimeRemaining <= 0.0f) {
            p.LifetimeRemaining = 0.0f;
            continue;
        }
        p.Velocity += p.Acceleration * dt;
        p.Position += p.Velocity * dt;
        ++comp.ActiveCount;
    }
    (void)cfg;
}

} // namespace

// ---- SceneParticleSystem ----

void SceneParticleSystem::OnSceneUpdate(Scene& scene, float deltaSeconds) {
    auto& registry = scene.m_registry->Registry;

    for (auto [entity, transform, comp] :
         registry.view<TransformComponent, ParticleSystemComponent>().each()) {
        if (!comp.EffectAsset.IsValid()) continue;

        // Ensure asset is loaded
        if (m_assetManager != nullptr && !m_assetManager->IsAssetLoaded(comp.EffectAsset)) {
            m_assetManager->LoadParticleEffect(comp.EffectAsset);
        }

        const ParticleEffectAsset* asset = GetAsset(m_assetManager, comp.EffectAsset);
        if (asset == nullptr || !asset->IsValid()) continue;

        const ParticleEffectConfig& cfg = asset->GetConfig();

        // Ensure pool
        EnsurePoolForComponent(comp, cfg);

        // Resolve texture
        if (cfg.TextureAsset.IsValid() && comp.LoadedTexture == nullptr && m_assetManager != nullptr) {
            m_assetManager->LoadTexture(cfg.TextureAsset);
            comp.LoadedTexture = m_assetManager->GetLoadedTexture(cfg.TextureAsset);
        }

        if (comp.PlayState != ParticlePlayState::Playing) {
            StepParticles(comp, cfg, deltaSeconds * comp.PlaybackSpeed);
            comp.Stats.ActiveParticles  = comp.ActiveCount;
            comp.Stats.PoolCapacity     = static_cast<std::uint32_t>(comp.Pool.size());
            comp.Stats.EmittedThisFrame = 0;
            continue;
        }

        const float dt = deltaSeconds * comp.PlaybackSpeed;
        const float prevElapsed = comp.ElapsedTime;
        comp.ElapsedTime += dt;

        // Start delay
        if (comp.ElapsedTime < cfg.StartDelay) {
            StepParticles(comp, cfg, dt);
            comp.Stats.ActiveParticles  = comp.ActiveCount;
            comp.Stats.PoolCapacity     = static_cast<std::uint32_t>(comp.Pool.size());
            comp.Stats.EmittedThisFrame = 0;
            continue;
        }

        // Looping / duration
        if (!cfg.Looping && !comp.Loop) {
            if (prevElapsed >= cfg.StartDelay + cfg.Duration) {
                // emission done; still simulate alive particles
                StepParticles(comp, cfg, dt);
                if (comp.ActiveCount == 0) comp.PlayState = ParticlePlayState::Stopped;
                comp.Stats.ActiveParticles  = comp.ActiveCount;
                comp.Stats.PoolCapacity     = static_cast<std::uint32_t>(comp.Pool.size());
                comp.Stats.EmittedThisFrame = 0;
                continue;
            }
        }

        const glm::vec2 emitterPos = cfg.WorldSpace
            ? transform.Translation
            : glm::vec2(0.0f); // local origin; particle positions are local too

        // Advance particles first
        StepParticles(comp, cfg, dt);

        std::uint32_t emittedThisFrame = 0;

        // Continuous emission
        if (comp.EmissionEnabled && cfg.EmissionRate > 0.0f) {
            comp.EmitAccum += cfg.EmissionRate * dt;
            const int toEmit = static_cast<int>(comp.EmitAccum);
            comp.EmitAccum  -= static_cast<float>(toEmit);
            const int maxNew = static_cast<int>(comp.Pool.size()) - static_cast<int>(comp.ActiveCount);
            const int actual = std::min(toEmit, std::max(0, maxNew));
            for (int i = 0; i < actual; ++i) {
                SpawnParticle(comp, cfg, emitterPos);
                ++emittedThisFrame;
            }
        }

        // Burst emission
        if (comp.EmissionEnabled) {
            const float activeTime = comp.ElapsedTime - cfg.StartDelay;
            for (std::size_t bi = static_cast<std::size_t>(comp.BurstIndex); bi < cfg.Bursts.size(); ++bi) {
                const ParticleBurst& burst = cfg.Bursts[bi];
                if (activeTime >= burst.Time) {
                    for (int i = 0; i < burst.Count; ++i) {
                        SpawnParticle(comp, cfg, emitterPos);
                        ++emittedThisFrame;
                    }
                    comp.BurstIndex = static_cast<int>(bi) + 1;
                } else {
                    break;
                }
            }
        }

        comp.Stats.ActiveParticles  = comp.ActiveCount;
        comp.Stats.EmittedThisFrame = emittedThisFrame;
        comp.Stats.PoolCapacity     = static_cast<std::uint32_t>(comp.Pool.size());
    }
}

void SceneParticleSystem::OnSceneRender(Scene& scene, Renderer2D& renderer, const OrthographicCamera& /*camera*/) {
    auto& registry = scene.m_registry->Registry;

    for (auto [entity, transform, comp] :
         registry.view<TransformComponent, ParticleSystemComponent>().each()) {
        if (comp.Pool.empty() || comp.ActiveCount == 0) continue;
        const ParticleEffectAsset* asset = GetAsset(m_assetManager, comp.EffectAsset);
        RenderParticleSystem(renderer, comp, transform, asset);
    }
}

void SceneParticleSystem::OnEntityDestroyed(std::uint32_t /*entityHandle*/) {
    // The ECS component is destroyed automatically; nothing extra to do.
}

void SceneParticleSystem::Clear() {}

void SceneParticleSystem::PrepareForShutdown() {}

} // namespace Xora2D
