#pragma once

#include "Xora2D/Core/UUID.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace Xora2D {

class Scene;
struct PhysicsWorld2DDetail;

// Lightweight, non-owning handle to an entity in a scene. Do not use after the
// entity or scene has been destroyed.
class Entity {
public:
    static constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFFu;

    Entity() = default;

    bool IsValid() const;

    UUID GetUUID() const;
    const std::string& GetName() const;
    void SetName(const std::string& name);

    template <typename Component, typename... Args>
    Component& AddComponent(Args&&... args);

    template <typename Component>
    Component& GetComponent();

    template <typename Component>
    const Component& GetComponent() const;

    template <typename Component>
    bool HasComponent() const;

    template <typename Component>
    void RemoveComponent();

private:
    friend class Scene;
    friend class PhysicsWorld2D;
    friend struct PhysicsWorld2DDetail;

    Entity(Scene* scene, std::uint32_t handle)
        : m_scene(scene), m_handle(handle) {}

    Scene* m_scene = nullptr;
    std::uint32_t m_handle = kInvalidHandle;
};

} // namespace Xora2D

#include "Xora2D/Scene/EntityExtern.h"