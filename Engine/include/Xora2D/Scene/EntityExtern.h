#pragma once

#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Scene/Components.h"

namespace Xora2D {

class Entity;

#define XORA_ENTITY_EXTERN_ACCESS(Component)                       \
    extern template Component& Entity::GetComponent<Component>();  \
    extern template const Component& Entity::GetComponent<Component>() const; \
    extern template bool Entity::HasComponent<Component>() const;  \
    extern template void Entity::RemoveComponent<Component>();

#define XORA_ENTITY_EXTERN_ADDABLE(Component)                    \
    extern template Component& Entity::AddComponent<>();           \
    XORA_ENTITY_EXTERN_ACCESS(Component)

XORA_ENTITY_EXTERN_ADDABLE(TransformComponent)
XORA_ENTITY_EXTERN_ADDABLE(SpriteRendererComponent)
XORA_ENTITY_EXTERN_ADDABLE(CameraComponent)
XORA_ENTITY_EXTERN_ACCESS(IDComponent)
XORA_ENTITY_EXTERN_ACCESS(TagComponent)
XORA_ENTITY_EXTERN_ADDABLE(RigidBody2DComponent)
XORA_ENTITY_EXTERN_ADDABLE(BoxCollider2DComponent)
XORA_ENTITY_EXTERN_ADDABLE(CircleCollider2DComponent)
XORA_ENTITY_EXTERN_ADDABLE(AudioSourceComponent)
XORA_ENTITY_EXTERN_ADDABLE(AudioListenerComponent)

#undef XORA_ENTITY_EXTERN_ADDABLE
#undef XORA_ENTITY_EXTERN_ACCESS

} // namespace Xora2D