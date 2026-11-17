#pragma once

#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scene/Components.h"

#include <entt/entt.hpp>
#include <cstdint>

namespace Xora2D {
namespace {

entt::entity ToEntt(std::uint32_t handle) {
    return static_cast<entt::entity>(handle);
}

} // namespace

struct Scene::Registry {
    entt::registry Registry;

    bool Valid(std::uint32_t handle) const {
        const entt::entity entity = ToEntt(handle);
        return entity != entt::null && Registry.valid(entity);
    }

    template <typename Component, typename... Args>
    Component& Emplace(std::uint32_t handle, Args&&... args) {
        return Registry.emplace<Component>(ToEntt(handle), std::forward<Args>(args)...);
    }

    template <typename Component>
    Component& Get(std::uint32_t handle) {
        return Registry.get<Component>(ToEntt(handle));
    }

    template <typename Component>
    const Component& Get(std::uint32_t handle) const {
        return Registry.get<Component>(ToEntt(handle));
    }

    template <typename Component>
    bool Has(std::uint32_t handle) const {
        return Registry.all_of<Component>(ToEntt(handle));
    }

    template <typename Component>
    void Remove(std::uint32_t handle) {
        Registry.remove<Component>(ToEntt(handle));
    }

    void Destroy(std::uint32_t handle) {
        Registry.destroy(ToEntt(handle));
    }
};

} // namespace Xora2D