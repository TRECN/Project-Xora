namespace Xora2D {

template <typename Component, typename... Args>
Component& Scene::EmplaceComponent(std::uint32_t handle, Args&&... args) {
    return m_registry->Emplace<Component>(handle, std::forward<Args>(args)...);
}

template <typename Component>
Component& Scene::GetComponent(std::uint32_t handle) {
    if (!m_registry->Valid(handle)) {
        throw std::runtime_error("Entity handle is invalid");
    }
    if (!m_registry->Has<Component>(handle)) {
        throw std::runtime_error("Entity does not have the requested component");
    }
    return m_registry->Get<Component>(handle);
}

template <typename Component>
const Component& Scene::GetComponent(std::uint32_t handle) const {
    if (!m_registry->Valid(handle)) {
        throw std::runtime_error("Entity handle is invalid");
    }
    if (!m_registry->Has<Component>(handle)) {
        throw std::runtime_error("Entity does not have the requested component");
    }
    return m_registry->Get<Component>(handle);
}

template <typename Component>
bool Scene::HasComponent(std::uint32_t handle) const {
    if (!m_registry->Valid(handle)) {
        return false;
    }
    return m_registry->Has<Component>(handle);
}

template <typename Component>
void Scene::RemoveComponent(std::uint32_t handle) {
    if (!m_registry->Valid(handle)) {
        throw std::runtime_error("Entity handle is invalid");
    }
    m_registry->Remove<Component>(handle);
}

} // namespace Xora2D