#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include "SceneRegistry.h"
#include "ScenePrivate.inl"

#include "Xora2D/Scene/Entity.inl"

namespace Xora2D {

#define XORA_ENTITY_INST_ACCESS(Component)                             \
    template Component& Entity::GetComponent<Component>();             \
    template const Component& Entity::GetComponent<Component>() const; \
    template bool Entity::HasComponent<Component>() const;             \
    template void Entity::RemoveComponent<Component>();

#define XORA_ENTITY_INST_ADDABLE(Component)                          \
    template Component& Entity::AddComponent<>();                      \
    XORA_ENTITY_INST_ACCESS(Component)

XORA_ENTITY_INST_ADDABLE(TransformComponent)
XORA_ENTITY_INST_ADDABLE(SpriteRendererComponent)
XORA_ENTITY_INST_ADDABLE(CameraComponent)
XORA_ENTITY_INST_ACCESS(IDComponent)
XORA_ENTITY_INST_ACCESS(TagComponent)
XORA_ENTITY_INST_ADDABLE(RigidBody2DComponent)
XORA_ENTITY_INST_ADDABLE(BoxCollider2DComponent)
XORA_ENTITY_INST_ADDABLE(CircleCollider2DComponent)
XORA_ENTITY_INST_ADDABLE(AudioSourceComponent)
XORA_ENTITY_INST_ADDABLE(AudioListenerComponent)
XORA_ENTITY_INST_ADDABLE(PythonScriptComponent)
XORA_ENTITY_INST_ADDABLE(AnimationComponent)
XORA_ENTITY_INST_ADDABLE(PrefabInstanceComponent)
XORA_ENTITY_INST_ADDABLE(TilemapComponent)
XORA_ENTITY_INST_ADDABLE(ParticleSystemComponent)

#undef XORA_ENTITY_INST_ADDABLE
#undef XORA_ENTITY_INST_ACCESS

} // namespace Xora2D