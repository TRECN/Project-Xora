#include "Xora2D/Scene/Entity.h"

#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"

namespace Xora2D {

bool Entity::IsValid() const {
    return m_scene != nullptr && m_scene->IsValid(*this);
}

UUID Entity::GetUUID() const {
    return GetComponent<IDComponent>().ID;
}

const std::string& Entity::GetName() const {
    return GetComponent<TagComponent>().Tag;
}

void Entity::SetName(const std::string& name) {
    GetComponent<TagComponent>().Tag = name;
    m_scene->ReindexEntityName(m_handle);
}

} // namespace Xora2D