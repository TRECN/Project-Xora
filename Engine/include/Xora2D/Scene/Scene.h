#pragma once

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"

#include "Xora2D/Physics/PhysicsEvents.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Xora2D {

class PhysicsWorld2D;
struct PhysicsWorld2DDetail;
class Renderer2D;

// Owns the ECS registry and entity lifecycle for one logical level.
class Scene : public std::enable_shared_from_this<Scene> {
public:
    static std::shared_ptr<Scene> Create();

    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    bool IsValid(const Entity& entity) const;

    Entity FindEntityByUUID(const UUID& uuid) const;
    Entity FindEntityByName(const std::string& name) const;

    void SetViewportSize(float width, float height);
    float GetViewportWidth() const { return m_viewportWidth; }
    float GetViewportHeight() const { return m_viewportHeight; }

    void OnUpdate(float deltaSeconds);
    void OnRender(Renderer2D& renderer);

    // Stops physics simulation and suppresses gameplay collision callbacks (e.g. on application quit).
    void PrepareForShutdown();

    std::size_t GetEntityCount() const;

    PhysicsWorld2D& GetPhysicsWorld();
    const PhysicsWorld2D& GetPhysicsWorld() const;

    void SetPhysicsEventCallback(std::function<void(const PhysicsCollisionEvent&)> callback);

private:
    friend class Entity;
    friend class SceneCamera;
    friend class PhysicsWorld2D;
    friend struct PhysicsWorld2DDetail;

    template <typename Component, typename... Args>
    Component& EmplaceComponent(std::uint32_t handle, Args&&... args);

    template <typename Component>
    Component& GetComponent(std::uint32_t handle);

    template <typename Component>
    const Component& GetComponent(std::uint32_t handle) const;

    template <typename Component>
    bool HasComponent(std::uint32_t handle) const;

    template <typename Component>
    void RemoveComponent(std::uint32_t handle);

    void IndexEntity(std::uint32_t handle);
    void UnindexEntity(std::uint32_t handle);
    void ReindexEntityName(std::uint32_t handle);

    struct Registry;
    std::unique_ptr<Registry> m_registry;

    std::unordered_map<UUID, std::uint32_t> m_uuidIndex;
    std::unordered_map<std::string, std::uint32_t> m_nameIndex;

    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;

    std::unique_ptr<PhysicsWorld2D> m_physics;
};

} // namespace Xora2D