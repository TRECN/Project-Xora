// Milestone 20: particle types, simulation, assets, ECS and scene serialization.
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetResult.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Particles/ParticleTypes.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace Xora2D;

int g_particleFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_particleFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

// ---------------------------------------------------------------------------------------------------------------
// FloatCurve / ColorGradient evaluation
// ---------------------------------------------------------------------------------------------------------------

void TestFloatCurve() {
    // Constant curve.
    FloatCurve constant = FloatCurve::Constant(2.5f);
    CHECK(constant.Evaluate(0.0f) == 2.5f, "constant curve at t=0");
    CHECK(constant.Evaluate(0.5f) == 2.5f, "constant curve at t=0.5");
    CHECK(constant.Evaluate(1.0f) == 2.5f, "constant curve at t=1");

    // Linear 0..1 curve.
    FloatCurve linear = FloatCurve::Linear(0.0f, 1.0f);
    CHECK(linear.Evaluate(0.0f) == 0.0f, "linear curve at t=0");
    CHECK(std::abs(linear.Evaluate(0.5f) - 0.5f) < 1e-5f, "linear curve at t=0.5");
    CHECK(linear.Evaluate(1.0f) == 1.0f, "linear curve at t=1");

    // Out-of-range clamp.
    CHECK(linear.Evaluate(-1.0f) == 0.0f, "linear curve clamps below 0");
    CHECK(linear.Evaluate(2.0f) == 1.0f, "linear curve clamps above 1");

    // Empty curve returns 1.
    FloatCurve empty{};
    CHECK(empty.Evaluate(0.5f) == 1.0f, "empty float curve returns 1");
}

void TestColorGradient() {
    const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);
    ColorGradient grad = ColorGradient::Linear(red, blue);

    const glm::vec4 at0 = grad.Evaluate(0.0f);
    const glm::vec4 at1 = grad.Evaluate(1.0f);
    CHECK(std::abs(at0.r - 1.0f) < 1e-5f, "gradient at t=0 is red");
    CHECK(std::abs(at1.b - 1.0f) < 1e-5f, "gradient at t=1 is blue");

    const glm::vec4 mid = grad.Evaluate(0.5f);
    CHECK(std::abs(mid.r - 0.5f) < 1e-5f && std::abs(mid.b - 0.5f) < 1e-5f, "gradient midpoint");
}

// ---------------------------------------------------------------------------------------------------------------
// Particle struct
// ---------------------------------------------------------------------------------------------------------------

void TestParticleLifetime() {
    Particle p{};
    p.LifetimeTotal     = 2.0f;
    p.LifetimeRemaining = 2.0f;
    CHECK(p.IsActive(), "fresh particle is active");
    CHECK(std::abs(p.LifetimeFraction() - 0.0f) < 1e-5f, "fraction=0 at start");

    p.LifetimeRemaining = 1.0f;
    CHECK(std::abs(p.LifetimeFraction() - 0.5f) < 1e-5f, "fraction=0.5 at half-life");

    p.LifetimeRemaining = 0.0f;
    CHECK(!p.IsActive(), "expired particle is not active");
    CHECK(std::abs(p.LifetimeFraction() - 1.0f) < 1e-5f, "fraction=1 at death");
}

// ---------------------------------------------------------------------------------------------------------------
// ParticleEffectAsset serialization round-trip
// ---------------------------------------------------------------------------------------------------------------

void TestParticleAssetRoundTrip() {
    const fs::path tmp = fs::temp_directory_path() / "xora_particle_test.particle.yaml";

    ParticleEffectConfig cfg{};
    cfg.MaxParticles  = 200;
    cfg.EmissionRate  = 50.0f;
    cfg.Looping       = true;
    cfg.Duration      = 3.0f;
    cfg.LifetimeMin   = 0.5f;
    cfg.LifetimeMax   = 1.5f;
    cfg.SpeedMin      = 2.0f;
    cfg.SpeedMax      = 5.0f;
    cfg.SizeBegin     = 0.3f;
    cfg.SizeEnd       = 0.0f;
    cfg.ColorBegin    = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    cfg.ColorEnd      = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    cfg.SizeCurve     = FloatCurve::Linear(1.0f, 0.0f);
    cfg.OpacityCurve  = FloatCurve::Linear(1.0f, 0.0f);
    cfg.WorldSpace    = false;

    const AssetResult saveResult = ParticleEffectAsset::SaveToFile(tmp, cfg);
    CHECK(saveResult.Success, "particle asset saved to file");

    ParticleEffectAsset loaded{};
    std::string loadError;
    const AssetResult loadResult = ParticleEffectAsset::LoadFromFile(tmp, "Particles/test.particle.yaml", loaded);
    CHECK(loadResult.Success && loaded.IsValid(), "particle asset loaded from file");
    if (loaded.IsValid()) {
        const auto& c = loaded.GetConfig();
        CHECK(c.MaxParticles == 200,              "MaxParticles round-trips");
        CHECK(std::abs(c.EmissionRate - 50.0f) < 1e-4f, "EmissionRate round-trips");
        CHECK(c.Looping == true,                  "Looping round-trips");
        CHECK(std::abs(c.Duration - 3.0f) < 1e-4f, "Duration round-trips");
        CHECK(std::abs(c.LifetimeMin - 0.5f) < 1e-4f, "LifetimeMin round-trips");
        CHECK(std::abs(c.SizeBegin - 0.3f) < 1e-4f,   "SizeBegin round-trips");
        CHECK(c.SizeCurve.Keys.size() == 2,       "SizeCurve key count round-trips");
        CHECK(c.WorldSpace == false,               "WorldSpace round-trips");
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------------------------------------------
// IsSupportedParticleEffectExtension
// ---------------------------------------------------------------------------------------------------------------

void TestExtensionClassification() {
    CHECK(IsSupportedParticleEffectExtension(fs::path("fire.particle.yaml")),
          ".particle.yaml recognised");
    CHECK(!IsSupportedParticleEffectExtension(fs::path("fire.yaml")),
          "plain .yaml not recognised");
    CHECK(!IsSupportedParticleEffectExtension(fs::path(".particle.yaml")),
          "bare compound extension rejected");
}

// ---------------------------------------------------------------------------------------------------------------
// ParticleSystemComponent play-state machine
// ---------------------------------------------------------------------------------------------------------------

void TestParticlePlayState() {
    ParticleSystemComponent ps{};
    CHECK(ps.PlayState == ParticlePlayState::Stopped, "default state is Stopped");

    ps.Play();
    CHECK(ps.PlayState == ParticlePlayState::Playing, "Play() transitions to Playing");

    ps.Pause();
    CHECK(ps.PlayState == ParticlePlayState::Paused, "Pause() transitions to Paused");

    ps.Resume();
    CHECK(ps.PlayState == ParticlePlayState::Playing, "Resume() returns to Playing");

    ps.Stop();
    CHECK(ps.PlayState == ParticlePlayState::Stopped, "Stop() transitions to Stopped");

    ps.Play();
    ps.Restart();
    CHECK(ps.PlayState == ParticlePlayState::Playing, "Restart() keeps Playing");
    CHECK(ps.ElapsedTime == 0.0f, "Restart() resets ElapsedTime");
}

// ---------------------------------------------------------------------------------------------------------------
// Ring-buffer particle pool
// ---------------------------------------------------------------------------------------------------------------

void TestParticlePool() {
    ParticleSystemComponent ps{};
    ps.Pool.resize(4);
    ps.PoolNext = 0;

    // Fill pool round-trip.
    Particle& a = ps.NextSlot();
    a.LifetimeRemaining = 1.0f;
    Particle& b = ps.NextSlot();
    b.LifetimeRemaining = 1.0f;
    Particle& c = ps.NextSlot();
    c.LifetimeRemaining = 1.0f;
    Particle& d = ps.NextSlot();
    d.LifetimeRemaining = 1.0f;

    // 5th slot wraps to index 0 (oldest).
    Particle& e = ps.NextSlot();
    CHECK(&e == &ps.Pool[0], "ring buffer wraps to slot 0 on overflow");
}

// ---------------------------------------------------------------------------------------------------------------
// ECS integration: add component, scene update ticks, scene serialization
// ---------------------------------------------------------------------------------------------------------------

void TestParticleECSIntegration() {
    auto scene = Scene::Create();
    Entity entity = scene->CreateEntity("Emitter");
    CHECK(!entity.HasComponent<ParticleSystemComponent>(), "no component before adding");

    auto& ps = entity.AddComponent<ParticleSystemComponent>();
    ps.EmissionEnabled = true;
    ps.Loop            = false;
    CHECK(entity.HasComponent<ParticleSystemComponent>(), "component present after add");

    // Simulate a few ticks without an asset — should not crash.
    for (int i = 0; i < 10; ++i) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(true, "simulation with no asset does not crash");
}

void TestParticleSceneSerialization() {
    // Build scene with a particle component.
    auto scene = Scene::Create();
    Entity entity = scene->CreateEntity("FX");
    auto& ps = entity.AddComponent<ParticleSystemComponent>();
    ps.EmissionEnabled = true;
    ps.PlaybackSpeed   = 2.0f;
    ps.Loop            = false;

    // Serialize to a temp file.
    const fs::path tmp = fs::temp_directory_path() / "xora_particle_scene_test.scene.yaml";
    const auto saveResult = SceneSerializer::Serialize(*scene, tmp);
    CHECK(saveResult.Success, "particle scene serialized");

    // Deserialize into a new scene.
    auto loaded = Scene::Create();
    const auto loadResult = SceneSerializer::Deserialize(*loaded, tmp, nullptr);
    CHECK(loadResult.Success, "particle scene deserialized");

    Entity found = loaded->FindEntityByName("FX");
    CHECK(found.IsValid(), "FX entity found after deserialization");
    if (found.IsValid()) {
        CHECK(found.HasComponent<ParticleSystemComponent>(), "ParticleSystemComponent present after deserialization");
        if (found.HasComponent<ParticleSystemComponent>()) {
            const auto& rps = found.GetComponent<ParticleSystemComponent>();
            CHECK(rps.EmissionEnabled == true, "EmissionEnabled round-trips");
            CHECK(std::abs(rps.PlaybackSpeed - 2.0f) < 1e-4f, "PlaybackSpeed round-trips");
            CHECK(rps.Loop == false, "Loop round-trips");
        }
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------------------------------------------
// Performance: allocate 10 000 particles, simulate 60 frames.
// ---------------------------------------------------------------------------------------------------------------

void TestParticlePerformance() {
    ParticleSystemComponent ps{};
    const std::uint32_t capacity = 10000u;
    ps.Pool.resize(capacity);
    ps.PoolNext = 0;

    // Fill pool with active particles.
    for (std::uint32_t i = 0; i < capacity; ++i) {
        Particle& p = ps.NextSlot();
        p.LifetimeTotal     = 5.0f;
        p.LifetimeRemaining = 5.0f;
        p.Velocity          = glm::vec2(1.0f, 0.5f);
        p.SizeBegin         = 0.1f;
        p.SizeEnd           = 0.0f;
        p.ColorBegin        = glm::vec4(1.0f);
        p.ColorEnd          = glm::vec4(0.0f);
    }

    // Simulate 60 frames manually (no scene system — raw loop so no asset lookup cost).
    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 60; ++frame) {
        for (Particle& p : ps.Pool) {
            if (!p.IsActive()) continue;
            p.LifetimeRemaining -= dt;
            p.Position += p.Velocity * dt;
        }
    }

    int active = 0;
    for (const Particle& p : ps.Pool) {
        if (p.IsActive()) ++active;
    }
    CHECK(active > 0, "particles still active after 60 frames at 5s lifetime");
    CHECK(static_cast<std::uint32_t>(active) == capacity, "all 10 000 particles survived 1 second simulation");
}

} // namespace

int RunParticleTests() {
    const int before = g_particleFailures;
    XORA_LOG_INFO("TEST particles started");
    TestFloatCurve();
    TestColorGradient();
    TestParticleLifetime();
    TestExtensionClassification();
    TestParticleAssetRoundTrip();
    TestParticlePlayState();
    TestParticlePool();
    TestParticleECSIntegration();
    TestParticleSceneSerialization();
    TestParticlePerformance();
    if (g_particleFailures == before) {
        XORA_LOG_INFO("TEST particles passed");
    } else {
        XORA_LOG_ERROR("TEST particles failed");
    }
    return g_particleFailures - before;
}
