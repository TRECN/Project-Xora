#include "Xora2D/Prefab/Prefab.h"

#include "Xora2D/Scene/Scene.h"

#include <algorithm>
#include <unordered_set>

namespace Xora2D {

Prefab::Prefab()
    : m_content(Scene::Create()) {
    m_content->SetSceneName("Prefab");
}

std::shared_ptr<Prefab> Prefab::Clone() const {
    auto copy = std::make_shared<Prefab>();
    copy->m_name = m_name;
    copy->m_revision = m_revision;
    copy->m_content = Scene::Copy(*m_content);
    copy->m_order = m_order;
    return copy;
}

Entity Prefab::FindEntity(const UUID& id) const {
    return m_content->FindEntityByUUID(id);
}

Entity Prefab::CreateEntity(const std::string& name, const UUID& id) {
    Entity entity = m_content->CreateEntityWithUUID(name, id);
    m_order.push_back(id);
    return entity;
}

void Prefab::SyncOrderFromContent() {
    std::vector<UUID> present = m_content->GetEntityUUIDs();
    std::unordered_set<UUID> presentSet(present.begin(), present.end());
    std::vector<UUID> order;
    std::unordered_set<UUID> kept;
    for (const UUID& id : m_order) {
        if (presentSet.contains(id)) {
            order.push_back(id);
            kept.insert(id);
        }
    }
    // GetEntityUUIDs lists newest first; append new entities oldest first.
    for (auto it = present.rbegin(); it != present.rend(); ++it) {
        if (!kept.contains(*it)) {
            order.push_back(*it);
        }
    }
    m_order = std::move(order);
}

} // namespace Xora2D
