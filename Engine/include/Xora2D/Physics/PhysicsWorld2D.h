#pragma once

#include "Xora2D/Physics/PhysicsEvents.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace Xora2D {

class Entity;
class Scene;

class PhysicsWorld2D {
public:
    struct Settings {
        glm::vec2 Gravity{0.0f, -9.81f};
        float PixelsPerMeter = 50.0f;
        float FixedTimestep = 1.0f / 60.0f;
        std::int32_t VelocityIterations = 8;
        std::int32_t PositionIterations = 3;
        std::int32_t MaxSubSteps = 8;
        // When true, logs per-fixture contact callbacks, entity depth, body motion (intended for dev builds).
        bool EnableContactDiagnostics = false;
    };

    PhysicsWorld2D();
    ~PhysicsWorld2D();

    PhysicsWorld2D(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;

    void Configure(const Settings& settings);
    const Settings& GetSettings() const;

    void SetGravity(const glm::vec2& gravityMetersPerSecondSquared);
    glm::vec2 GetGravity() const;

    void SetSimulationEnabled(bool enabled);
    bool IsSimulationEnabled() const;

    float GetPixelsPerMeter() const;

    void SetEventCallback(std::function<void(const PhysicsCollisionEvent&)> callback);

    // Stops dispatching collision callbacks and clears contact tracking before world teardown.
    void PrepareShutdown();

    bool IsDispatchingContactEvents() const;

    void Step(Scene& scene, float deltaSeconds);
    void OnEntityDestroyed(Scene& scene, const Entity& entity);

    bool HasPhysicsBody(const Entity& entity) const;

    void ApplyForce(const Entity& entity, const glm::vec2& forceNewtons);
    void ApplyForceAtPoint(const Entity& entity, const glm::vec2& forceNewtons, const glm::vec2& pointPixels);
    void ApplyLinearImpulse(const Entity& entity, const glm::vec2& impulse);
    void ApplyAngularImpulse(const Entity& entity, float impulse);
    void SetLinearVelocity(const Entity& entity, const glm::vec2& velocityMetersPerSecond);
    glm::vec2 GetLinearVelocity(const Entity& entity) const;
    void SetAngularVelocity(const Entity& entity, float radiansPerSecond);
    float GetAngularVelocity(const Entity& entity) const;
    void ApplyTorque(const Entity& entity, float torque);
    void SetBodyType(const Entity& entity, RigidBodyType2D type);
    void SetBodyEnabled(const Entity& entity, bool enabled);
    void SetGravityScale(Entity& entity, float scale);

    float GetAccumulatedTimeSeconds() const;
    std::uint32_t GetLastSubStepCount() const;

private:
    friend class Scene;
    friend struct PhysicsWorld2DDetail;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void SyncSceneBodies(Scene& scene);
    void SyncEcsToPhysics(Scene& scene);
    void SyncPhysicsToEcs(Scene& scene);
    void FlushContactEvents(Scene& scene);
};

} // namespace Xora2D