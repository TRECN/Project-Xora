#include "Xora2D/Scene/Scene.h"

namespace Xora2D {

template <typename Component, typename... Args>
Component& Entity::AddComponent(Args&&... args) {
    if (!IsValid()) {
        throw std::runtime_error("Cannot add a component to an invalid entity");
    }
    return m_scene->EmplaceComponent<Component>(m_handle, std::forward<Args>(args)...);
}

template <typename Component>
Component& Entity::GetComponent() {
    if (!IsValid()) {
        throw std::runtime_error("Cannot get a component from an invalid entity");
    }
    return m_scene->GetComponent<Component>(m_handle);
}

template <typename Component>
const Component& Entity::GetComponent() const {
    if (!IsValid()) {
        throw std::runtime_error("Cannot get a component from an invalid entity");
    }
    return m_scene->GetComponent<Component>(m_handle);
}

template <typename Component>
bool Entity::HasComponent() const {
    if (!IsValid()) {
        return false;
    }
    return m_scene->HasComponent<Component>(m_handle);
}

template <typename Component>
void Entity::RemoveComponent() {
    if (!IsValid()) {
        throw std::runtime_error("Cannot remove a component from an invalid entity");
    }
    m_scene->RemoveComponent<Component>(m_handle);
}

} // namespace Xora2D