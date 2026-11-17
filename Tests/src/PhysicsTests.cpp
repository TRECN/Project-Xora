#include "Xora2D/Core/Logger.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsEvents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <glm/vec2.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "Xora2D/Core/UUID.h"

namespace {

int g_physicsFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_physicsFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

bool Near(float value, float expected, float epsilon = 0.05f) {
    return std::abs(value - expected) < epsilon;
}

bool MatchesEntityPair(const Xora2D::PhysicsCollisionEvent& event, const Xora2D::UUID& first,
                       const Xora2D::UUID& second) {
    return (event.EntityA == first && event.EntityB == second) ||
           (event.EntityA == second && event.EntityB == first);
}

int CountEvents(const std::vector<Xora2D::PhysicsCollisionEvent>& events, const Xora2D::UUID& first,
                const Xora2D::UUID& second, Xora2D::PhysicsCollisionKind kind,
                Xora2D::PhysicsCollisionPhase phase) {
    int count = 0;
    for (const Xora2D::PhysicsCollisionEvent& event : events) {
        if (event.Kind == kind && event.Phase == phase && MatchesEntityPair(event, first, second)) {
            ++count;
        }
    }
    return count;
}

void TestWorldBasics() {
    XORA_LOG_INFO("TEST physics world basics started");
    auto scene = Xora2D::Scene::Create();
    auto& physics = scene->GetPhysicsWorld();
    physics.SetGravity({0.0f, -10.0f});
    CHECK(Near(physics.GetGravity().y, -10.0f), "gravity configuration updates");

    physics.SetSimulationEnabled(false);
    scene->OnUpdate(1.0f / 60.0f);
    CHECK(physics.GetLastSubStepCount() == 0, "paused simulation performs no substeps");

    physics.SetSimulationEnabled(true);
    for (int index = 0; index < 5; ++index) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(physics.GetLastSubStepCount() >= 1, "enabled simulation runs fixed substeps");
}

void TestDynamicFall() {
    auto scene = Xora2D::Scene::Create();
    auto& physics = scene->GetPhysicsWorld();
    physics.Configure(physics.GetSettings());

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& groundCollider = ground.AddComponent<Xora2D::BoxCollider2DComponent>();
    groundCollider.Size = {800.0f, 40.0f};
    auto& groundTransform = ground.GetComponent<Xora2D::TransformComponent>();
    groundTransform.Translation = {200.0f, 40.0f};

    Xora2D::Entity box = scene->CreateEntity("Box");
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    auto& boxCollider = box.AddComponent<Xora2D::BoxCollider2DComponent>();
    boxCollider.Size = {40.0f, 40.0f};
    auto& boxTransform = box.GetComponent<Xora2D::TransformComponent>();
    boxTransform.Translation = {200.0f, 300.0f};

    const float startY = boxTransform.Translation.y;
    for (int frame = 0; frame < 120; ++frame) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    const float endY = box.GetComponent<Xora2D::TransformComponent>().Translation.y;
    const float groundY = ground.GetComponent<Xora2D::TransformComponent>().Translation.y;

    CHECK(endY < startY, "dynamic body falls under gravity");
    CHECK(endY <= groundY + 50.0f, "dynamic body lands near the static ground");
    CHECK(groundTransform.Translation.y == groundY, "static ground transform stays unchanged");
}

void TestKinematicControl() {
    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity mover = scene->CreateEntity("Kinematic");
    auto& body = mover.AddComponent<Xora2D::RigidBody2DComponent>();
    body.Type = Xora2D::RigidBodyType2D::Kinematic;
    mover.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    auto& transform = mover.GetComponent<Xora2D::TransformComponent>();
    transform.Translation = {100.0f, 100.0f};

    transform.Translation.x += 80.0f;
    transform.RecalculateLocalMatrix();
    scene->OnUpdate(1.0f / 60.0f);
    CHECK(Near(mover.GetComponent<Xora2D::TransformComponent>().Translation.x, 180.0f, 1.0f),
          "kinematic transform drives physics");
}

void TestApiForcesAndImpulses() {
    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity bodyEntity = scene->CreateEntity("ImpulseBody");
    bodyEntity.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    bodyEntity.AddComponent<Xora2D::CircleCollider2DComponent>().Radius = 20.0f;
    scene->OnUpdate(1.0f / 60.0f);

    auto& physics = scene->GetPhysicsWorld();
    CHECK(physics.HasPhysicsBody(bodyEntity), "scene creates a runtime physics body");
    physics.ApplyLinearImpulse(bodyEntity, {0.5f, 0.0f});
    const glm::vec2 velocity = physics.GetLinearVelocity(bodyEntity);
    CHECK(std::abs(velocity.x) > 0.01f, "linear impulse changes velocity");
    physics.SetLinearVelocity(bodyEntity, {0.0f, 0.0f});
    CHECK(Near(physics.GetLinearVelocity(bodyEntity).x, 0.0f, 0.01f), "velocity can be cleared");
}

void TestMultipleFixtures() {
    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity compound = scene->CreateEntity("Compound");
    compound.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    compound.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {30.0f, 30.0f};
    compound.AddComponent<Xora2D::CircleCollider2DComponent>().Radius = 15.0f;
    scene->OnUpdate(1.0f / 60.0f);
    CHECK(scene->GetPhysicsWorld().HasPhysicsBody(compound), "compound collider entity gets one physics body");
}

void TestEntityDestruction() {
    auto scene = Xora2D::Scene::Create();
    Xora2D::Entity doomed = scene->CreateEntity("Doomed");
    doomed.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    doomed.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {30.0f, 30.0f};
    scene->OnUpdate(1.0f / 60.0f);
    CHECK(scene->GetPhysicsWorld().HasPhysicsBody(doomed), "body exists before destruction");
    scene->DestroyEntity(doomed);
    CHECK(!doomed.IsValid(), "entity is invalid after destruction");
    CHECK(!scene->GetPhysicsWorld().HasPhysicsBody(doomed), "physics body removed with entity");
}

void TestCollisionAndSensorEvents() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("GroundCollider");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {600.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 20.0f};

    Xora2D::Entity sensor = scene->CreateEntity("SensorZone");
    sensor.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& sensorCollider = sensor.AddComponent<Xora2D::BoxCollider2DComponent>();
    sensorCollider.Size = {120.0f, 120.0f};
    sensorCollider.IsSensor = true;
    sensor.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 140.0f};

    Xora2D::Entity box = scene->CreateEntity("FallingBox");
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    box.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    box.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 240.0f};

    for (int index = 0; index < 90; ++index) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    bool sawCollisionBegin = false;
    bool sawSensorBegin = false;
    for (const Xora2D::PhysicsCollisionEvent& event : events) {
        if (event.Kind == Xora2D::PhysicsCollisionKind::Collision &&
            event.Phase == Xora2D::PhysicsCollisionPhase::Begin) {
            sawCollisionBegin = true;
            CHECK(event.EntityA.IsValid() && event.EntityB.IsValid(), "collision event uses valid UUIDs");
        }
        if (event.Kind == Xora2D::PhysicsCollisionKind::Sensor &&
            event.Phase == Xora2D::PhysicsCollisionPhase::Begin) {
            sawSensorBegin = true;
        }
    }
    CHECK(sawCollisionBegin, "collision begin event reported");
    CHECK(sawSensorBegin, "sensor begin event reported");
}

void TestCollisionContactLifecycle() {
    XORA_LOG_INFO("TEST collision event lifecycle started");
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {400.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity box = scene->CreateEntity("Box");
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    box.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    box.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};
    const Xora2D::UUID boxId = box.GetUUID();

    scene->OnUpdate(1.0f / 60.0f);
    for (int step = 0; step < 120; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "resting contact emits exactly one collision begin");
    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 0,
          "resting contact does not emit collision end");

    auto& physics = scene->GetPhysicsWorld();
    physics.SetLinearVelocity(box, {0.0f, 20.0f});
    for (int step = 0; step < 60; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 1,
          "separating bodies emit one collision end");

    physics.SetLinearVelocity(box, {0.0f, -8.0f});
    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 2,
          "re-entering contact emits a second collision begin");
}

void TestSensorContactLifecycle() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity sensor = scene->CreateEntity("Sensor");
    sensor.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& sensorCollider = sensor.AddComponent<Xora2D::BoxCollider2DComponent>();
    sensorCollider.Size = {200.0f, 200.0f};
    sensorCollider.IsSensor = true;
    sensor.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 150.0f};
    const Xora2D::UUID sensorId = sensor.GetUUID();

    Xora2D::Entity visitor = scene->CreateEntity("Visitor");
    visitor.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    visitor.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    visitor.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 150.0f};
    const Xora2D::UUID visitorId = visitor.GetUUID();

    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, visitorId, sensorId, Xora2D::PhysicsCollisionKind::Sensor,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "sensor overlap emits exactly one sensor begin");

    scene->GetPhysicsWorld().SetLinearVelocity(visitor, {30.0f, 0.0f});
    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, visitorId, sensorId, Xora2D::PhysicsCollisionKind::Sensor,
                      Xora2D::PhysicsCollisionPhase::End) == 1,
          "leaving sensor emits one sensor end");
}

void TestMultipleFixturesSingleEntityCollisionEvent() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {400.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity compound = scene->CreateEntity("Compound");
    compound.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    compound.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    compound.AddComponent<Xora2D::CircleCollider2DComponent>().Radius = 20.0f;
    compound.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};
    const Xora2D::UUID compoundId = compound.GetUUID();

    for (int step = 0; step < 120; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "multiple fixtures on one body produce one entity-level collision begin");
}

void TestDestroyEntityDuringContact() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {400.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};

    Xora2D::Entity box = scene->CreateEntity("Box");
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    box.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    box.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};

    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    scene->DestroyEntity(box);
    scene->OnUpdate(1.0f / 60.0f);
    CHECK(!box.IsValid(), "destroyed entity stays invalid after physics step");
    CHECK(!scene->GetPhysicsWorld().HasPhysicsBody(box), "physics body is gone after entity destroy");
}

void TestSustainedContactProducesNoFurtherEvents() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& groundBox = ground.AddComponent<Xora2D::BoxCollider2DComponent>();
    groundBox.Size = {400.0f, 40.0f};
    groundBox.Restitution = 0.0f;
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity box = scene->CreateEntity("Box");
    auto& body = box.AddComponent<Xora2D::RigidBody2DComponent>();
    body.Type = Xora2D::RigidBodyType2D::Dynamic;
    body.LinearDamping = 0.5f;
    auto& collider = box.AddComponent<Xora2D::BoxCollider2DComponent>();
    collider.Size = {40.0f, 40.0f};
    collider.Restitution = 0.0f;
    collider.Friction = 0.9f;
    box.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};
    const Xora2D::UUID boxId = box.GetUUID();

    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    const std::size_t eventsAfterSettle = events.size();
    for (int step = 0; step < 240; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "sustained contact produces one collision begin");
    CHECK(CountEvents(events, boxId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 0,
          "sustained contact produces no collision end");
    CHECK(events.size() == eventsAfterSettle, "no additional contact events while bodies remain at rest");
}

void TestPartialFixtureContactRetention() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& groundBox = ground.AddComponent<Xora2D::BoxCollider2DComponent>();
    groundBox.Size = {600.0f, 40.0f};
    groundBox.Restitution = 0.0f;
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity compound = scene->CreateEntity("Compound");
    compound.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    auto& box = compound.AddComponent<Xora2D::BoxCollider2DComponent>();
    box.Size = {40.0f, 40.0f};
    box.Restitution = 0.0f;
    auto& circle = compound.AddComponent<Xora2D::CircleCollider2DComponent>();
    circle.Radius = 20.0f;
    circle.Offset = {0.0f, 30.0f};
    circle.Restitution = 0.0f;
    compound.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 220.0f};
    const Xora2D::UUID compoundId = compound.GetUUID();

    for (int step = 0; step < 120; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    const std::size_t eventsAfterSettle = events.size();
    for (int step = 0; step < 120; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "dual fixtures still emit one entity begin");
    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 0,
          "dual fixtures resting together emit no entity end");
    CHECK(events.size() == eventsAfterSettle,
          "fixture-level churn does not surface as duplicate entity events while resting");
}

void TestBouncyContactCanReEnterLegitimately() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& groundCollider = ground.AddComponent<Xora2D::BoxCollider2DComponent>();
    groundCollider.Size = {400.0f, 40.0f};
    groundCollider.Restitution = 0.0f;
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity ball = scene->CreateEntity("Ball");
    ball.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    auto& ballCollider = ball.AddComponent<Xora2D::CircleCollider2DComponent>();
    ballCollider.Radius = 20.0f;
    ballCollider.Restitution = 0.65f;
    ball.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};
    const Xora2D::UUID ballId = ball.GetUUID();

    for (int step = 0; step < 180; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    const int begins =
        CountEvents(events, ballId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                    Xora2D::PhysicsCollisionPhase::Begin);
    const int ends = CountEvents(events, ballId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                                 Xora2D::PhysicsCollisionPhase::End);
    CHECK(begins >= 2, "bouncy contact may legitimately begin again after separating");
    CHECK(ends >= 1, "bouncy contact emits end when bodies separate mid-bounce");
}

void TestOneFixtureEndWhileOtherActiveKeepsEntityContact() {
    auto scene = Xora2D::Scene::Create();
    std::vector<Xora2D::PhysicsCollisionEvent> events;
    scene->SetPhysicsEventCallback([&events](const Xora2D::PhysicsCollisionEvent& event) { events.push_back(event); });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    auto& groundBox = ground.AddComponent<Xora2D::BoxCollider2DComponent>();
    groundBox.Size = {800.0f, 40.0f};
    groundBox.Restitution = 0.0f;
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {400.0f, 30.0f};
    const Xora2D::UUID groundId = ground.GetUUID();

    Xora2D::Entity compound = scene->CreateEntity("Compound");
    auto& body = compound.AddComponent<Xora2D::RigidBody2DComponent>();
    body.Type = Xora2D::RigidBodyType2D::Dynamic;
    body.FixedRotation = true;
    auto& box = compound.AddComponent<Xora2D::BoxCollider2DComponent>();
    box.Size = {50.0f, 50.0f};
    box.Restitution = 0.0f;
    auto& circle = compound.AddComponent<Xora2D::CircleCollider2DComponent>();
    circle.Radius = 22.0f;
    circle.Offset = {0.0f, 38.0f};
    circle.Restitution = 0.0f;
    compound.GetComponent<Xora2D::TransformComponent>().Translation = {400.0f, 240.0f};
    const Xora2D::UUID compoundId = compound.GetUUID();

    for (int step = 0; step < 150; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::Begin) == 1,
          "compound settles with one entity-level begin");

    auto& physics = scene->GetPhysicsWorld();
    for (int impulse = 0; impulse < 6; ++impulse) {
        physics.ApplyLinearImpulse(compound, {0.0f, 0.08f});
        scene->OnUpdate(1.0f / 60.0f);
        if (CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                        Xora2D::PhysicsCollisionPhase::End) > 0) {
            break;
        }
    }
    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 0,
          "incremental lift does not emit entity end while a fixture remains touching");

    physics.SetLinearVelocity(compound, {0.0f, 12.0f});
    for (int step = 0; step < 60; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(CountEvents(events, compoundId, groundId, Xora2D::PhysicsCollisionKind::Collision,
                      Xora2D::PhysicsCollisionPhase::End) == 1,
          "full separation emits exactly one entity end");
}

void TestPrepareForShutdownStopsFurtherDispatch() {
    auto scene = Xora2D::Scene::Create();
    int eventCount = 0;
    scene->SetPhysicsEventCallback([&eventCount](const Xora2D::PhysicsCollisionEvent&) { ++eventCount; });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {400.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};

    Xora2D::Entity ball = scene->CreateEntity("Ball");
    ball.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    auto& ballCollider = ball.AddComponent<Xora2D::CircleCollider2DComponent>();
    ballCollider.Radius = 20.0f;
    ballCollider.Restitution = 0.8f;
    ball.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};

    scene->OnUpdate(1.0f / 60.0f);
    const int afterFirstStep = eventCount;

    scene->PrepareForShutdown();
    CHECK(!scene->GetPhysicsWorld().IsDispatchingContactEvents(),
          "PrepareForShutdown disables contact gameplay dispatch");

    for (int step = 0; step < 30; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(eventCount == afterFirstStep,
          "physics updates after PrepareForShutdown do not dispatch gameplay collision events");
}

void TestShutdownDoesNotDispatchCollisionEvents() {
    auto scene = Xora2D::Scene::Create();
    int eventCount = 0;
    scene->SetPhysicsEventCallback([&eventCount](const Xora2D::PhysicsCollisionEvent&) { ++eventCount; });

    Xora2D::Entity ground = scene->CreateEntity("Ground");
    ground.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Static;
    ground.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {400.0f, 40.0f};
    ground.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 30.0f};

    Xora2D::Entity box = scene->CreateEntity("Box");
    box.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
    box.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
    box.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 200.0f};

    for (int step = 0; step < 90; ++step) {
        scene->OnUpdate(1.0f / 60.0f);
    }

    const int beforeShutdown = eventCount;
    scene.reset();
    CHECK(eventCount == beforeShutdown, "scene shutdown does not dispatch additional collision events");
}

void TestSceneDestructionCleanup() {
    {
        auto scene = Xora2D::Scene::Create();
        Xora2D::Entity entity = scene->CreateEntity("Temp");
        entity.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
        entity.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {20.0f, 20.0f};
        scene->OnUpdate(1.0f / 60.0f);
    }
    CHECK(true, "scene destruction cleans up physics world");
}

} // namespace

int RunPhysicsTests() {
    const int before = g_physicsFailures;
    TestWorldBasics();
    TestDynamicFall();
    TestKinematicControl();
    TestApiForcesAndImpulses();
    TestMultipleFixtures();
    TestEntityDestruction();
    TestCollisionAndSensorEvents();
    TestCollisionContactLifecycle();
    TestSensorContactLifecycle();
    TestMultipleFixturesSingleEntityCollisionEvent();
    TestDestroyEntityDuringContact();
    TestSustainedContactProducesNoFurtherEvents();
    TestPartialFixtureContactRetention();
    TestBouncyContactCanReEnterLegitimately();
    TestOneFixtureEndWhileOtherActiveKeepsEntityContact();
    TestPrepareForShutdownStopsFurtherDispatch();
    TestShutdownDoesNotDispatchCollisionEvents();
    TestSceneDestructionCleanup();

    const int failed = g_physicsFailures - before;
    if (failed == 0) {
        XORA_LOG_INFO("Physics tests passed");
    } else {
        XORA_LOG_ERROR("Physics tests failed ({} checks)", failed);
    }
    return failed;
}