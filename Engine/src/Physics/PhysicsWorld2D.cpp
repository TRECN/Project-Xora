#include "Xora2D/Physics/PhysicsWorld2D.h"

#include "Xora2D/Physics/PhysicsUtils.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/TilemapRendering.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <box2d/b2_body.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_world.h>

#include <entt/entt.hpp>
#include <glm/common.hpp>

#include "Scene/SceneRegistry.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Xora2D {
namespace {

std::uint32_t HandleFromBody(const b2Body* body) {
    if (body == nullptr) {
        return Entity::kInvalidHandle;
    }
    return static_cast<std::uint32_t>(
        const_cast<b2Body*>(body)->GetUserData().pointer);
}

b2BodyType ToBoxBodyType(RigidBodyType2D type) {
    switch (type) {
    case RigidBodyType2D::Static:
        return b2_staticBody;
    case RigidBodyType2D::Dynamic:
        return b2_dynamicBody;
    case RigidBodyType2D::Kinematic:
        return b2_kinematicBody;
    }
    return b2_dynamicBody;
}

// Fixture user data marking one-way platform fixtures of tilemaps.
constexpr std::uintptr_t kOneWayFixtureTag = 0x0E7A0001u;

// Lowest / highest world Y of a fixture's shape (exact, unlike the fattened broad-phase AABB).
float FixtureExtentY(const b2Fixture& fixture, bool top) {
    const b2Transform& transform = fixture.GetBody()->GetTransform();
    const b2Shape* shape = fixture.GetShape();
    if (shape->GetType() == b2Shape::e_circle) {
        const auto* circle = static_cast<const b2CircleShape*>(shape);
        const b2Vec2 center = b2Mul(transform, circle->m_p);
        return top ? center.y + circle->m_radius : center.y - circle->m_radius;
    }
    if (shape->GetType() == b2Shape::e_polygon) {
        const auto* polygon = static_cast<const b2PolygonShape*>(shape);
        float extent = top ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
        for (int index = 0; index < polygon->m_count; ++index) {
            const float y = b2Mul(transform, polygon->m_vertices[index]).y;
            extent = top ? std::max(extent, y) : std::min(extent, y);
        }
        return extent;
    }
    b2AABB aabb;
    shape->ComputeAABB(&aabb, transform, 0);
    return top ? aabb.upperBound.y : aabb.lowerBound.y;
}

struct QueuedContact {
    std::uint32_t HandleA = Entity::kInvalidHandle;
    std::uint32_t HandleB = Entity::kInvalidHandle;
    std::uintptr_t FixtureIdA = 0;
    std::uintptr_t FixtureIdB = 0;
    PhysicsCollisionKind Kind = PhysicsCollisionKind::Collision;
    PhysicsCollisionPhase Phase = PhysicsCollisionPhase::Begin;
};

class ContactListener final : public b2ContactListener {
public:
    std::vector<QueuedContact> Queue;

    void Clear() { Queue.clear(); }

    void BeginContact(b2Contact* contact) override {
        if (contact == nullptr) {
            return;
        }
        Queue.push_back(MakeQueued(*contact, PhysicsCollisionPhase::Begin));
    }

    // One-way tilemap platforms: only collide with bodies coming from above.
    void PreSolve(b2Contact* contact, const b2Manifold* /*oldManifold*/) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        const bool platformIsA = fixtureA->GetUserData().pointer == kOneWayFixtureTag;
        const bool platformIsB = fixtureB->GetUserData().pointer == kOneWayFixtureTag;
        if (platformIsA == platformIsB) {
            return;
        }
        const b2Fixture& platform = platformIsA ? *fixtureA : *fixtureB;
        const b2Fixture& other = platformIsA ? *fixtureB : *fixtureA;
        const float platformTop = FixtureExtentY(platform, true);
        const float otherBottom = FixtureExtentY(other, false);
        const float tolerance = 0.05f; // meters
        if (other.GetBody()->GetLinearVelocity().y > 0.01f || otherBottom < platformTop - tolerance) {
            contact->SetEnabled(false);
        }
    }

    void EndContact(b2Contact* contact) override {
        if (contact == nullptr) {
            return;
        }
        Queue.push_back(MakeQueued(*contact, PhysicsCollisionPhase::End));
    }

private:
    static QueuedContact MakeQueued(const b2Contact& contact, PhysicsCollisionPhase phase) {
        const b2Fixture* fixtureA = contact.GetFixtureA();
        const b2Fixture* fixtureB = contact.GetFixtureB();
        const b2Body* bodyA = fixtureA != nullptr ? fixtureA->GetBody() : nullptr;
        const b2Body* bodyB = fixtureB != nullptr ? fixtureB->GetBody() : nullptr;

        QueuedContact queued{};
        queued.HandleA = HandleFromBody(bodyA);
        queued.HandleB = HandleFromBody(bodyB);
        if (fixtureA != nullptr) {
            queued.FixtureIdA = reinterpret_cast<std::uintptr_t>(fixtureA);
        }
        if (fixtureB != nullptr) {
            queued.FixtureIdB = reinterpret_cast<std::uintptr_t>(fixtureB);
        }
        const bool sensor =
            (fixtureA != nullptr && fixtureA->IsSensor()) || (fixtureB != nullptr && fixtureB->IsSensor());
        queued.Kind = sensor ? PhysicsCollisionKind::Sensor : PhysicsCollisionKind::Collision;
        queued.Phase = phase;
        return queued;
    }
};

struct BodyRecord {
    b2Body* Body = nullptr;
};

struct TilemapChunkFixtures {
    bool Built = false;
    std::uint64_t Signature = 0;
    std::vector<b2Fixture*> Fixtures;
};

struct TilemapBodyRecord {
    b2Body* Body = nullptr;
    std::unordered_map<std::uint64_t, TilemapChunkFixtures> Chunks;
    // Settings the fixtures were built with; any change rebuilds every chunk.
    TilemapCollisionSettings Settings;
    glm::ivec2 TileSize{0};
    glm::vec2 Scale{1.0f};
};

struct EntityContactPairKey {
    std::uint32_t HandleLo = Entity::kInvalidHandle;
    std::uint32_t HandleHi = Entity::kInvalidHandle;
    PhysicsCollisionKind Kind = PhysicsCollisionKind::Collision;

    bool operator==(const EntityContactPairKey& other) const {
        return HandleLo == other.HandleLo && HandleHi == other.HandleHi && Kind == other.Kind;
    }
};

struct EntityContactPairKeyHash {
    std::size_t operator()(const EntityContactPairKey& key) const {
        const std::size_t kind = static_cast<std::size_t>(key.Kind);
        return (static_cast<std::size_t>(key.HandleLo) << 32U) ^
               static_cast<std::size_t>(key.HandleHi) ^ (kind << 16U);
    }
};

EntityContactPairKey MakeEntityContactPairKey(std::uint32_t handleA, std::uint32_t handleB,
                                              PhysicsCollisionKind kind) {
    EntityContactPairKey key{};
    if (handleA < handleB) {
        key.HandleLo = handleA;
        key.HandleHi = handleB;
    } else {
        key.HandleLo = handleB;
        key.HandleHi = handleA;
    }
    key.Kind = kind;
    return key;
}

} // namespace

struct PhysicsWorld2D::Impl {
    PhysicsWorld2D::Settings Settings{};
    std::unique_ptr<b2World> World;
    ContactListener Listener;
    std::unordered_map<std::uint32_t, BodyRecord> Bodies;
    std::unordered_map<std::uint32_t, TilemapBodyRecord> Tilemaps;
    std::uint64_t TilemapChunkRebuilds = 0;
    std::unordered_map<EntityContactPairKey, std::uint32_t, EntityContactPairKeyHash> EntityContactDepth;
    float Accumulator = 0.0f;
    bool SimulationEnabled = true;
    std::uint32_t LastSubStepCount = 0;
    bool DispatchContactEvents = true;
    std::function<void(const PhysicsCollisionEvent&)> EventCallback;
};

PhysicsWorld2D::PhysicsWorld2D()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->World = std::make_unique<b2World>(b2Vec2(m_impl->Settings.Gravity.x, m_impl->Settings.Gravity.y));
    m_impl->World->SetContactListener(&m_impl->Listener);
}

PhysicsWorld2D::~PhysicsWorld2D() {
    PrepareShutdown();
    if (m_impl && m_impl->World) {
        m_impl->World->SetContactListener(nullptr);
        for (auto& pair : m_impl->Bodies) {
            if (pair.second.Body != nullptr) {
                m_impl->World->DestroyBody(pair.second.Body);
                pair.second.Body = nullptr;
            }
        }
        m_impl->Bodies.clear();
        for (auto& pair : m_impl->Tilemaps) {
            if (pair.second.Body != nullptr) {
                m_impl->World->DestroyBody(pair.second.Body);
            }
        }
        m_impl->Tilemaps.clear();
    }
}

void PhysicsWorld2D::Configure(const Settings& settings) {
    m_impl->Settings = settings;
    const float ppm = std::max(settings.PixelsPerMeter, 0.0001f);
    (void)ppm;
    if (m_impl->World) {
        m_impl->World->SetGravity(b2Vec2(settings.Gravity.x, settings.Gravity.y));
    }
}

const PhysicsWorld2D::Settings& PhysicsWorld2D::GetSettings() const {
    return m_impl->Settings;
}

void PhysicsWorld2D::SetGravity(const glm::vec2& gravityMetersPerSecondSquared) {
    m_impl->Settings.Gravity = gravityMetersPerSecondSquared;
    if (m_impl->World) {
        m_impl->World->SetGravity(b2Vec2(gravityMetersPerSecondSquared.x, gravityMetersPerSecondSquared.y));
    }
}

glm::vec2 PhysicsWorld2D::GetGravity() const {
    return m_impl->Settings.Gravity;
}

void PhysicsWorld2D::SetSimulationEnabled(bool enabled) {
    m_impl->SimulationEnabled = enabled;
    if (!enabled) {
        m_impl->Accumulator = 0.0f;
    }
}

bool PhysicsWorld2D::IsSimulationEnabled() const {
    return m_impl->SimulationEnabled;
}

float PhysicsWorld2D::GetPixelsPerMeter() const {
    return m_impl->Settings.PixelsPerMeter;
}

void PhysicsWorld2D::SetEventCallback(std::function<void(const PhysicsCollisionEvent&)> callback) {
    m_impl->EventCallback = std::move(callback);
}

void PhysicsWorld2D::PrepareShutdown() {
    if (!m_impl) {
        return;
    }
    m_impl->DispatchContactEvents = false;
    m_impl->SimulationEnabled = false;
    m_impl->Accumulator = 0.0f;
    m_impl->EventCallback = nullptr;
    m_impl->Listener.Clear();
    m_impl->EntityContactDepth.clear();
}

bool PhysicsWorld2D::IsDispatchingContactEvents() const {
    return m_impl && m_impl->DispatchContactEvents;
}

float PhysicsWorld2D::GetAccumulatedTimeSeconds() const {
    return m_impl->Accumulator;
}

std::uint32_t PhysicsWorld2D::GetLastSubStepCount() const {
    return m_impl->LastSubStepCount;
}

bool PhysicsWorld2D::HasPhysicsBody(const Entity& entity) const {
    return entity.IsValid() && m_impl->Bodies.contains(entity.m_handle);
}

struct PhysicsWorld2DDetail {
    static b2Body* RequireBody(PhysicsWorld2D::Impl& impl, const Entity& entity) {
        if (!entity.IsValid()) {
            throw std::runtime_error("Physics operation on invalid entity");
        }
        const auto found = impl.Bodies.find(entity.m_handle);
        if (found == impl.Bodies.end() || found->second.Body == nullptr) {
            throw std::runtime_error("Entity does not have a physics body");
        }
        return found->second.Body;
    }

    static const b2Body* RequireBody(const PhysicsWorld2D::Impl& impl, const Entity& entity) {
        if (!entity.IsValid()) {
            throw std::runtime_error("Physics operation on invalid entity");
        }
        const auto found = impl.Bodies.find(entity.m_handle);
        if (found == impl.Bodies.end() || found->second.Body == nullptr) {
            throw std::runtime_error("Entity does not have a physics body");
        }
        return found->second.Body;
    }

    static void BuildFixtures(Scene& scene, const PhysicsWorld2D::Impl& impl, std::uint32_t handle, b2Body* body) {
        const float ppm = impl.Settings.PixelsPerMeter;

        while (body->GetFixtureList() != nullptr) {
            body->DestroyFixture(body->GetFixtureList());
        }

        if (scene.HasComponent<BoxCollider2DComponent>(handle)) {
            const auto& box = scene.GetComponent<BoxCollider2DComponent>(handle);
            if (box.Size.x > 0.0f && box.Size.y > 0.0f && box.Density >= 0.0f) {
                b2PolygonShape shape;
                const glm::vec2 halfSize = box.Size * 0.5f;
                const glm::vec2 offsetMeters = PhysicsUtils::PixelsToMeters(box.Offset, ppm);
                shape.SetAsBox(PhysicsUtils::PixelsToMeters(halfSize.x, ppm),
                               PhysicsUtils::PixelsToMeters(halfSize.y, ppm),
                               b2Vec2(offsetMeters.x, offsetMeters.y), 0.0f);

                b2FixtureDef fixtureDef{};
                fixtureDef.shape = &shape;
                fixtureDef.density = box.Density;
                fixtureDef.friction = box.Friction;
                fixtureDef.restitution = box.Restitution;
                fixtureDef.restitutionThreshold =
                    PhysicsUtils::PixelsToMeters(box.RestitutionThreshold, ppm);
                fixtureDef.isSensor = box.IsSensor;
                body->CreateFixture(&fixtureDef);
            }
        }

        if (scene.HasComponent<CircleCollider2DComponent>(handle)) {
            const auto& circle = scene.GetComponent<CircleCollider2DComponent>(handle);
            if (circle.Radius > 0.0f && circle.Density >= 0.0f) {
                b2CircleShape shape;
                const glm::vec2 offsetMeters = PhysicsUtils::PixelsToMeters(circle.Offset, ppm);
                shape.m_p = b2Vec2(offsetMeters.x, offsetMeters.y);
                shape.m_radius = PhysicsUtils::PixelsToMeters(circle.Radius, ppm);

                b2FixtureDef fixtureDef{};
                fixtureDef.shape = &shape;
                fixtureDef.density = circle.Density;
                fixtureDef.friction = circle.Friction;
                fixtureDef.restitution = circle.Restitution;
                fixtureDef.restitutionThreshold =
                    PhysicsUtils::PixelsToMeters(circle.RestitutionThreshold, ppm);
                fixtureDef.isSensor = circle.IsSensor;
                body->CreateFixture(&fixtureDef);
            }
        }
    }

    // One static body per tilemap; fixtures are tracked per chunk and rebuilt only for chunks whose collision
    // signature changed (see TilemapComponent::CollisionSignature). Adjacent cells are merged into rectangles.
    static void SyncTilemapBodies(Scene& scene, PhysicsWorld2D::Impl& impl) {
        auto& registry = scene.m_registry->Registry;
        const float ppm = impl.Settings.PixelsPerMeter;
        AssetManager* assets = scene.m_assetManager;
        const auto getTileset = [assets](AssetHandle handle) -> const Tileset* {
            if (assets == nullptr || !handle.IsValid() || !assets->LoadTileset(handle).Success) {
                return nullptr;
            }
            return assets->GetLoadedTileset(handle).get();
        };

        std::unordered_set<std::uint32_t> alive;
        for (const auto entity : registry.view<TilemapComponent, TransformComponent>()) {
            const auto handle = static_cast<std::uint32_t>(entt::to_integral(entity));
            const auto& tilemap = registry.get<TilemapComponent>(entity);
            const auto& transform = registry.get<TransformComponent>(entity);
            if (!tilemap.Collision.Enabled) {
                continue;
            }
            alive.insert(handle);
            auto [iterator, created] = impl.Tilemaps.try_emplace(handle);
            TilemapBodyRecord& record = iterator->second;
            const glm::vec2 positionMeters = PhysicsUtils::PixelsToMeters(transform.Translation, ppm);
            if (created) {
                b2BodyDef bodyDef{};
                bodyDef.type = b2_staticBody;
                bodyDef.position.Set(positionMeters.x, positionMeters.y);
                bodyDef.angle = transform.RotationRadians;
                bodyDef.userData.pointer = static_cast<std::uintptr_t>(handle);
                record.Body = impl.World->CreateBody(&bodyDef);
            } else {
                const b2Vec2 current = record.Body->GetPosition();
                if (current.x != positionMeters.x || current.y != positionMeters.y ||
                    record.Body->GetAngle() != transform.RotationRadians) {
                    record.Body->SetTransform(b2Vec2(positionMeters.x, positionMeters.y), transform.RotationRadians);
                }
            }
            if (!(record.Settings == tilemap.Collision) || record.TileSize != tilemap.TileSize ||
                record.Scale != transform.Scale) {
                for (auto& [key, chunk] : record.Chunks) {
                    (void)key;
                    for (b2Fixture* fixture : chunk.Fixtures) {
                        record.Body->DestroyFixture(fixture);
                    }
                }
                record.Chunks.clear();
                record.Settings = tilemap.Collision;
                record.TileSize = tilemap.TileSize;
                record.Scale = transform.Scale;
            }

            const std::vector<std::uint64_t> keys = CollectCollisionChunks(tilemap);
            const std::unordered_set<std::uint64_t> present(keys.begin(), keys.end());
            for (auto chunk = record.Chunks.begin(); chunk != record.Chunks.end();) {
                if (!present.contains(chunk->first)) {
                    for (b2Fixture* fixture : chunk->second.Fixtures) {
                        record.Body->DestroyFixture(fixture);
                    }
                    chunk = record.Chunks.erase(chunk);
                } else {
                    ++chunk;
                }
            }
            const glm::vec2 cellPixels = glm::vec2(tilemap.TileSize) * transform.Scale;
            for (const std::uint64_t key : keys) {
                const std::uint64_t signature = tilemap.CollisionSignature(key);
                TilemapChunkFixtures& chunk = record.Chunks[key];
                if (chunk.Built && chunk.Signature == signature) {
                    continue;
                }
                for (b2Fixture* fixture : chunk.Fixtures) {
                    record.Body->DestroyFixture(fixture);
                }
                chunk.Fixtures.clear();
                chunk.Signature = signature;
                chunk.Built = true;
                ++impl.TilemapChunkRebuilds;
                for (const TileCollisionRect& rect : BuildChunkCollisionRects(tilemap, key, getTileset)) {
                    const glm::vec2 halfPixels = glm::abs(glm::vec2(rect.Width, rect.Height) * cellPixels) * 0.5f;
                    const glm::vec2 centerPixels =
                        glm::vec2(static_cast<float>(rect.X) + static_cast<float>(rect.Width) * 0.5f,
                                  static_cast<float>(rect.Y) + static_cast<float>(rect.Height) * 0.5f) *
                        cellPixels;
                    const glm::vec2 centerMeters = PhysicsUtils::PixelsToMeters(centerPixels, ppm);
                    b2PolygonShape shape;
                    shape.SetAsBox(PhysicsUtils::PixelsToMeters(halfPixels.x, ppm),
                                   PhysicsUtils::PixelsToMeters(halfPixels.y, ppm),
                                   b2Vec2(centerMeters.x, centerMeters.y), 0.0f);
                    b2FixtureDef fixtureDef{};
                    fixtureDef.shape = &shape;
                    fixtureDef.friction = tilemap.Collision.Friction;
                    fixtureDef.restitution = tilemap.Collision.Restitution;
                    fixtureDef.filter.categoryBits = tilemap.Collision.CategoryBits;
                    fixtureDef.filter.maskBits = tilemap.Collision.MaskBits;
                    if (rect.Kind == TileCollision::OneWay) {
                        fixtureDef.userData.pointer = kOneWayFixtureTag;
                    }
                    chunk.Fixtures.push_back(record.Body->CreateFixture(&fixtureDef));
                }
            }
        }
        // Tilemaps whose component was removed or whose collision was switched off.
        for (auto record = impl.Tilemaps.begin(); record != impl.Tilemaps.end();) {
            if (!alive.contains(record->first)) {
                if (record->second.Body != nullptr) {
                    impl.World->DestroyBody(record->second.Body);
                }
                PurgeEntityContactPairs(impl, record->first);
                record = impl.Tilemaps.erase(record);
            } else {
                ++record;
            }
        }
    }

    static void PurgeEntityContactPairs(PhysicsWorld2D::Impl& impl, std::uint32_t handle) {
        for (auto iterator = impl.EntityContactDepth.begin(); iterator != impl.EntityContactDepth.end();) {
            const EntityContactPairKey& key = iterator->first;
            if (key.HandleLo == handle || key.HandleHi == handle) {
                iterator = impl.EntityContactDepth.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    static void PurgeListenerQueueForHandle(PhysicsWorld2D::Impl& impl, std::uint32_t handle) {
        auto& queue = impl.Listener.Queue;
        queue.erase(std::remove_if(queue.begin(), queue.end(),
                                   [handle](const QueuedContact& queued) {
                                       return queued.HandleA == handle || queued.HandleB == handle;
                                   }),
                    queue.end());
    }

    static void LogBodyState(const char* label, std::uint32_t handle, const PhysicsWorld2D::Impl& impl,
                             float ppm) {
        const auto found = impl.Bodies.find(handle);
        if (found == impl.Bodies.end() || found->second.Body == nullptr) {
            XORA_LOG_INFO("    {} handle={} (no body)", label, handle);
            return;
        }
        const b2Body* body = found->second.Body;
        const b2Vec2 position = body->GetPosition();
        const b2Vec2 velocity = body->GetLinearVelocity();
        const glm::vec2 positionPx = PhysicsUtils::MetersToPixels(glm::vec2(position.x, position.y), ppm);
        XORA_LOG_INFO("    {} handle={} posPx=({:.2f},{:.2f}) velMps=({:.3f},{:.3f})", label, handle,
                      positionPx.x, positionPx.y, velocity.x, velocity.y);
    }
};

void PhysicsWorld2D::SyncSceneBodies(Scene& scene) {
    auto& registry = scene.m_registry->Registry;
    const float ppm = m_impl->Settings.PixelsPerMeter;

    for (const auto entity : registry.view<RigidBody2DComponent, TransformComponent>()) {
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        auto& bodyComponent = registry.get<RigidBody2DComponent>(entity);
        const auto& transform = registry.get<TransformComponent>(entity);

        auto found = m_impl->Bodies.find(handle);
        if (found == m_impl->Bodies.end()) {
            b2BodyDef bodyDef{};
            bodyDef.type = ToBoxBodyType(bodyComponent.Type);
            const glm::vec2 positionMeters = PhysicsUtils::PixelsToMeters(transform.Translation, ppm);
            bodyDef.position.Set(positionMeters.x, positionMeters.y);
            bodyDef.angle = transform.RotationRadians;
            bodyDef.fixedRotation = bodyComponent.FixedRotation;
            bodyDef.gravityScale = bodyComponent.GravityScale;
            bodyDef.linearDamping = bodyComponent.LinearDamping;
            bodyDef.angularDamping = bodyComponent.AngularDamping;
            bodyDef.bullet = bodyComponent.Bullet;
            bodyDef.allowSleep = bodyComponent.AllowSleep;
            bodyDef.enabled = bodyComponent.Enabled;
            bodyDef.userData.pointer = static_cast<std::uintptr_t>(handle);

            b2Body* body = m_impl->World->CreateBody(&bodyDef);
            PhysicsWorld2DDetail::BuildFixtures(scene, *m_impl, handle, body);
            m_impl->Bodies.emplace(handle, BodyRecord{body});
            continue;
        }

        b2Body* body = found->second.Body;
        if (body == nullptr) {
            continue;
        }

        if (body->GetFixtureList() == nullptr) {
            PhysicsWorld2DDetail::BuildFixtures(scene, *m_impl, handle, body);
        }
    }
    PhysicsWorld2DDetail::SyncTilemapBodies(scene, *m_impl);
}

void PhysicsWorld2D::SyncEcsToPhysics(Scene& scene) {
    auto& registry = scene.m_registry->Registry;
    const float ppm = m_impl->Settings.PixelsPerMeter;

    for (const auto entity : registry.view<RigidBody2DComponent, TransformComponent>()) {
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        const auto& bodyComponent = registry.get<RigidBody2DComponent>(entity);
        const auto& transform = registry.get<TransformComponent>(entity);

        const auto found = m_impl->Bodies.find(handle);
        if (found == m_impl->Bodies.end() || found->second.Body == nullptr) {
            continue;
        }

        b2Body* body = found->second.Body;
        const RigidBodyType2D type = bodyComponent.Type;
        const bool shouldDrive = bodyComponent.DriveTransformFromECS || type == RigidBodyType2D::Kinematic;

        if (!shouldDrive) {
            continue;
        }

        const glm::vec2 positionMeters = PhysicsUtils::PixelsToMeters(transform.Translation, ppm);
        const b2Vec2 current = body->GetPosition();
        const float angle = body->GetAngle();
        constexpr float kPositionEpsilon = 0.00001f;
        constexpr float kAngleEpsilon = 0.00001f;
        if (std::abs(current.x - positionMeters.x) < kPositionEpsilon &&
            std::abs(current.y - positionMeters.y) < kPositionEpsilon &&
            std::abs(angle - transform.RotationRadians) < kAngleEpsilon) {
            continue;
        }
        body->SetTransform(b2Vec2(positionMeters.x, positionMeters.y), transform.RotationRadians);
    }
}

void PhysicsWorld2D::SyncPhysicsToEcs(Scene& scene) {
    auto& registry = scene.m_registry->Registry;
    const float ppm = m_impl->Settings.PixelsPerMeter;

    for (const auto entity : registry.view<RigidBody2DComponent, TransformComponent>()) {
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        const auto& bodyComponent = registry.get<RigidBody2DComponent>(entity);
        if (bodyComponent.Type != RigidBodyType2D::Dynamic) {
            continue;
        }

        const auto found = m_impl->Bodies.find(handle);
        if (found == m_impl->Bodies.end() || found->second.Body == nullptr) {
            continue;
        }

        b2Body* body = found->second.Body;
        const b2Vec2 position = body->GetPosition();
        auto& transform = registry.get<TransformComponent>(entity);
        transform.Translation = PhysicsUtils::MetersToPixels(glm::vec2(position.x, position.y), ppm);
        transform.RotationRadians = body->GetAngle();
        transform.RecalculateLocalMatrix();
    }
}

void PhysicsWorld2D::FlushContactEvents(Scene& scene) {
    if (!m_impl->DispatchContactEvents) {
        m_impl->Listener.Clear();
        return;
    }

    const bool diagnostics = m_impl->Settings.EnableContactDiagnostics;
    const float ppm = m_impl->Settings.PixelsPerMeter;

    for (const QueuedContact& queued : m_impl->Listener.Queue) {
        if (queued.HandleA == Entity::kInvalidHandle || queued.HandleB == Entity::kInvalidHandle) {
            continue;
        }

        const EntityContactPairKey pairKey =
            MakeEntityContactPairKey(queued.HandleA, queued.HandleB, queued.Kind);
        const std::uint32_t depthBefore = m_impl->EntityContactDepth.contains(pairKey)
                                              ? m_impl->EntityContactDepth[pairKey]
                                              : 0U;
        std::uint32_t& depth = m_impl->EntityContactDepth[pairKey];

        bool emitEntityEvent = false;
        if (queued.Phase == PhysicsCollisionPhase::Begin) {
            const std::uint32_t previousDepth = depth;
            depth = previousDepth + 1U;
            emitEntityEvent = previousDepth == 0;
        } else {
            if (depth == 0) {
                if (diagnostics) {
                    XORA_LOG_INFO(
                        "  contact diag: fixture {:x}<->{:x} End ignored (entity depth already 0) sensor={}",
                        queued.FixtureIdA, queued.FixtureIdB,
                        queued.Kind == PhysicsCollisionKind::Sensor ? "yes" : "no");
                }
                continue;
            }
            depth -= 1U;
            emitEntityEvent = depth == 0;
            if (depth == 0) {
                m_impl->EntityContactDepth.erase(pairKey);
            }
        }

        if (diagnostics) {
            Entity entityA(&scene, queued.HandleA);
            Entity entityB(&scene, queued.HandleB);
            const char* phase = queued.Phase == PhysicsCollisionPhase::Begin ? "Begin" : "End";
            const char* sensor = queued.Kind == PhysicsCollisionKind::Sensor ? "yes" : "no";
            XORA_LOG_INFO(
                "  contact diag: {} fixture {:x}<->{:x} handles {}<->{} depth {}->{} emitEntity={} sensor={}",
                phase, queued.FixtureIdA, queued.FixtureIdB, queued.HandleA, queued.HandleB, depthBefore,
                queued.Phase == PhysicsCollisionPhase::Begin ? depthBefore + 1U : depthBefore - 1U,
                emitEntityEvent ? "yes" : "no", sensor);
            if (entityA.IsValid()) {
                XORA_LOG_INFO("      entityA {}", entityA.GetUUID().ToString());
            }
            if (entityB.IsValid()) {
                XORA_LOG_INFO("      entityB {}", entityB.GetUUID().ToString());
            }
            PhysicsWorld2DDetail::LogBodyState("bodyA", queued.HandleA, *m_impl, ppm);
            PhysicsWorld2DDetail::LogBodyState("bodyB", queued.HandleB, *m_impl, ppm);
        }

        if (!emitEntityEvent || !m_impl->EventCallback) {
            continue;
        }

        Entity entityA(&scene, queued.HandleA);
        Entity entityB(&scene, queued.HandleB);
        if (!entityA.IsValid() || !entityB.IsValid()) {
            if (queued.Phase == PhysicsCollisionPhase::Begin) {
                if (depth > 0) {
                    --depth;
                }
                if (depth == 0) {
                    m_impl->EntityContactDepth.erase(pairKey);
                }
            }
            continue;
        }

        PhysicsCollisionEvent event{};
        event.Kind = queued.Kind;
        event.Phase = queued.Phase;
        event.EntityA = entityA.GetUUID();
        event.EntityB = entityB.GetUUID();
        m_impl->EventCallback(event);
    }
    m_impl->Listener.Clear();
}

void PhysicsWorld2D::Step(Scene& scene, float deltaSeconds) {
    if (deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }

    if (!m_impl->DispatchContactEvents) {
        m_impl->Listener.Clear();
        m_impl->Accumulator = 0.0f;
        return;
    }

    SyncSceneBodies(scene);
    SyncEcsToPhysics(scene);

    m_impl->LastSubStepCount = 0;
    if (!m_impl->SimulationEnabled) {
        FlushContactEvents(scene);
        return;
    }

    const float step = std::max(m_impl->Settings.FixedTimestep, 0.0001f);
    const int maxSteps = std::max(m_impl->Settings.MaxSubSteps, 1);

    m_impl->Accumulator += deltaSeconds;
    if (m_impl->Accumulator > step * static_cast<float>(maxSteps)) {
        m_impl->Accumulator = step * static_cast<float>(maxSteps);
    }

    while (m_impl->Accumulator >= step && m_impl->LastSubStepCount < static_cast<std::uint32_t>(maxSteps)) {
        m_impl->Listener.Clear();
        m_impl->World->Step(step, m_impl->Settings.VelocityIterations, m_impl->Settings.PositionIterations);
        m_impl->Accumulator -= step;
        ++m_impl->LastSubStepCount;
        SyncPhysicsToEcs(scene);
        FlushContactEvents(scene);
    }
}

void PhysicsWorld2D::OnEntityDestroyed(Scene& /*scene*/, const Entity& entity) {
    if (!entity.IsValid()) {
        return;
    }
    PhysicsWorld2DDetail::PurgeEntityContactPairs(*m_impl, entity.m_handle);
    if (const auto tilemap = m_impl->Tilemaps.find(entity.m_handle); tilemap != m_impl->Tilemaps.end()) {
        if (tilemap->second.Body != nullptr && m_impl->World) {
            m_impl->World->DestroyBody(tilemap->second.Body);
        }
        m_impl->Tilemaps.erase(tilemap);
        PhysicsWorld2DDetail::PurgeListenerQueueForHandle(*m_impl, entity.m_handle);
    }
    const auto found = m_impl->Bodies.find(entity.m_handle);
    if (found == m_impl->Bodies.end()) {
        return;
    }
    if (found->second.Body != nullptr && m_impl->World) {
        m_impl->World->DestroyBody(found->second.Body);
    }
    m_impl->Bodies.erase(found);
    PhysicsWorld2DDetail::PurgeListenerQueueForHandle(*m_impl, entity.m_handle);
}

bool PhysicsWorld2D::HasTilemapBody(const Entity& entity) const {
    return entity.IsValid() && m_impl->Tilemaps.contains(entity.m_handle);
}

std::size_t PhysicsWorld2D::GetTilemapFixtureCount(const Entity& entity) const {
    if (!entity.IsValid()) {
        return 0;
    }
    const auto found = m_impl->Tilemaps.find(entity.m_handle);
    if (found == m_impl->Tilemaps.end()) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& [key, chunk] : found->second.Chunks) {
        (void)key;
        count += chunk.Fixtures.size();
    }
    return count;
}

std::uint64_t PhysicsWorld2D::GetTilemapChunkRebuildCount() const {
    return m_impl->TilemapChunkRebuilds;
}

std::size_t PhysicsWorld2D::GetTilemapBodyCount() const {
    return m_impl->Tilemaps.size();
}

void PhysicsWorld2D::ApplyForce(const Entity& entity, const glm::vec2& forceNewtons) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->ApplyForce(b2Vec2(forceNewtons.x, forceNewtons.y), body->GetWorldCenter(), true);
}

void PhysicsWorld2D::ApplyForceAtPoint(const Entity& entity, const glm::vec2& forceNewtons,
                                       const glm::vec2& pointPixels) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    const glm::vec2 pointMeters =
        PhysicsUtils::PixelsToMeters(pointPixels, m_impl->Settings.PixelsPerMeter);
    body->ApplyForce(b2Vec2(forceNewtons.x, forceNewtons.y), b2Vec2(pointMeters.x, pointMeters.y), true);
}

void PhysicsWorld2D::ApplyLinearImpulse(const Entity& entity, const glm::vec2& impulse) {
    if (!HasPhysicsBody(entity)) {
        return;
    }
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->ApplyLinearImpulseToCenter(b2Vec2(impulse.x, impulse.y), true);
}

void PhysicsWorld2D::SetGravityScale(Entity& entity, float scale) {
    if (!entity.HasComponent<RigidBody2DComponent>()) {
        return;
    }
    entity.GetComponent<RigidBody2DComponent>().GravityScale = scale;
    if (HasPhysicsBody(entity)) {
        b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
        body->SetGravityScale(scale);
    }
}

void PhysicsWorld2D::ApplyAngularImpulse(const Entity& entity, float impulse) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->ApplyAngularImpulse(impulse, true);
}

void PhysicsWorld2D::SetLinearVelocity(const Entity& entity, const glm::vec2& velocityMetersPerSecond) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->SetLinearVelocity(b2Vec2(velocityMetersPerSecond.x, velocityMetersPerSecond.y));
}

glm::vec2 PhysicsWorld2D::GetLinearVelocity(const Entity& entity) const {
    const b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    const b2Vec2 velocity = body->GetLinearVelocity();
    return {velocity.x, velocity.y};
}

void PhysicsWorld2D::SetAngularVelocity(const Entity& entity, float radiansPerSecond) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->SetAngularVelocity(radiansPerSecond);
}

float PhysicsWorld2D::GetAngularVelocity(const Entity& entity) const {
    const b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    return body->GetAngularVelocity();
}

void PhysicsWorld2D::ApplyTorque(const Entity& entity, float torque) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->ApplyTorque(torque, true);
}

void PhysicsWorld2D::SetBodyType(const Entity& entity, RigidBodyType2D type) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->SetType(ToBoxBodyType(type));
}

void PhysicsWorld2D::SetBodyEnabled(const Entity& entity, bool enabled) {
    b2Body* body = PhysicsWorld2DDetail::RequireBody(*m_impl, entity);
    body->SetEnabled(enabled);
}

} // namespace Xora2D