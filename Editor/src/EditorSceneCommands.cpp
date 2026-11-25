#include "Xora2DEditor/EditorSceneCommands.h"

#include "Xora2D/Tilemap/TilemapComponent.h"

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"

namespace Xora2DEditor {

Xora2D::Entity CreateEmptyEntity(Xora2D::Scene& scene, const std::string& name) {
    return scene.CreateEntity(name.empty() ? "Entity" : name);
}

EditorCommandStatus DestroyEntity(Xora2D::Scene& scene, EditorSelection& selection, const Xora2D::UUID& uuid) {
    const Xora2D::Entity entity = scene.FindEntityByUUID(uuid);
    if (!entity.IsValid()) {
        return EditorCommandStatus::InvalidEntity;
    }
    selection.OnEntityDestroyed(uuid);
    scene.DestroyEntity(entity);
    return EditorCommandStatus::Success;
}

EditorCommandStatus RenameEntity(Xora2D::Entity entity, const std::string& newName) {
    if (!entity.IsValid()) {
        return EditorCommandStatus::InvalidEntity;
    }
    if (newName.empty()) {
        return EditorCommandStatus::InvalidName;
    }
    entity.SetName(newName);
    return EditorCommandStatus::Success;
}

static bool HasKind(const Xora2D::Entity& entity, SceneComponentKind kind) {
    switch (kind) {
    case SceneComponentKind::SpriteRenderer:
        return entity.HasComponent<Xora2D::SpriteRendererComponent>();
    case SceneComponentKind::Camera:
        return entity.HasComponent<Xora2D::CameraComponent>();
    case SceneComponentKind::RigidBody2D:
        return entity.HasComponent<Xora2D::RigidBody2DComponent>();
    case SceneComponentKind::BoxCollider2D:
        return entity.HasComponent<Xora2D::BoxCollider2DComponent>();
    case SceneComponentKind::CircleCollider2D:
        return entity.HasComponent<Xora2D::CircleCollider2DComponent>();
    case SceneComponentKind::AudioSource:
        return entity.HasComponent<Xora2D::AudioSourceComponent>();
    case SceneComponentKind::AudioListener:
        return entity.HasComponent<Xora2D::AudioListenerComponent>();
    case SceneComponentKind::PythonScript:
        return entity.HasComponent<Xora2D::PythonScriptComponent>();
    case SceneComponentKind::Animation:
        return entity.HasComponent<Xora2D::AnimationComponent>();
    case SceneComponentKind::Tilemap:
        return entity.HasComponent<Xora2D::TilemapComponent>();
    case SceneComponentKind::ParticleSystem:
        return entity.HasComponent<Xora2D::ParticleSystemComponent>();
    }
    return false;
}

bool CanAddComponent(const Xora2D::Entity& entity, SceneComponentKind kind) {
    return entity.IsValid() && !HasKind(entity, kind);
}

EditorCommandStatus AddComponentToEntity(Xora2D::Scene& scene, Xora2D::Entity& entity, SceneComponentKind kind) {
    if (!entity.IsValid()) {
        return EditorCommandStatus::InvalidEntity;
    }
    if (!CanAddComponent(entity, kind)) {
        return EditorCommandStatus::DuplicateComponent;
    }

    switch (kind) {
    case SceneComponentKind::SpriteRenderer:
        entity.AddComponent<Xora2D::SpriteRendererComponent>();
        scene.ResolveSpriteTextures();
        break;
    case SceneComponentKind::Camera:
        entity.AddComponent<Xora2D::CameraComponent>();
        break;
    case SceneComponentKind::RigidBody2D:
        entity.AddComponent<Xora2D::RigidBody2DComponent>();
        SyncPhysicsWithScene(scene);
        break;
    case SceneComponentKind::BoxCollider2D:
        entity.AddComponent<Xora2D::BoxCollider2DComponent>();
        if (entity.HasComponent<Xora2D::RigidBody2DComponent>()) {
            SyncPhysicsWithScene(scene);
        }
        break;
    case SceneComponentKind::CircleCollider2D:
        entity.AddComponent<Xora2D::CircleCollider2DComponent>();
        if (entity.HasComponent<Xora2D::RigidBody2DComponent>()) {
            SyncPhysicsWithScene(scene);
        }
        break;
    case SceneComponentKind::AudioSource:
        entity.AddComponent<Xora2D::AudioSourceComponent>();
        break;
    case SceneComponentKind::AudioListener:
        entity.AddComponent<Xora2D::AudioListenerComponent>();
        break;
    case SceneComponentKind::PythonScript:
        entity.AddComponent<Xora2D::PythonScriptComponent>();
        break;
    case SceneComponentKind::Animation:
        entity.AddComponent<Xora2D::AnimationComponent>();
        break;
    case SceneComponentKind::Tilemap:
        entity.AddComponent<Xora2D::TilemapComponent>().CreateDefaultLayers();
        break;
    case SceneComponentKind::ParticleSystem:
        entity.AddComponent<Xora2D::ParticleSystemComponent>();
        break;
    }
    return EditorCommandStatus::Success;
}

EditorCommandStatus RemoveComponentFromEntity(Xora2D::Scene& scene, Xora2D::Entity& entity, SceneComponentKind kind) {
    if (!entity.IsValid()) {
        return EditorCommandStatus::InvalidEntity;
    }
    if (!HasKind(entity, kind)) {
        return EditorCommandStatus::ComponentNotPresent;
    }

    switch (kind) {
    case SceneComponentKind::SpriteRenderer:
        entity.RemoveComponent<Xora2D::SpriteRendererComponent>();
        break;
    case SceneComponentKind::Camera:
        entity.RemoveComponent<Xora2D::CameraComponent>();
        break;
    case SceneComponentKind::RigidBody2D:
        scene.GetPhysicsWorld().OnEntityDestroyed(scene, entity);
        entity.RemoveComponent<Xora2D::RigidBody2DComponent>();
        break;
    case SceneComponentKind::BoxCollider2D:
        entity.RemoveComponent<Xora2D::BoxCollider2DComponent>();
        if (entity.HasComponent<Xora2D::RigidBody2DComponent>()) {
            SyncPhysicsWithScene(scene);
        }
        break;
    case SceneComponentKind::CircleCollider2D:
        entity.RemoveComponent<Xora2D::CircleCollider2DComponent>();
        if (entity.HasComponent<Xora2D::RigidBody2DComponent>()) {
            SyncPhysicsWithScene(scene);
        }
        break;
    case SceneComponentKind::AudioSource:
        scene.StopEntityAudio(entity);
        entity.RemoveComponent<Xora2D::AudioSourceComponent>();
        break;
    case SceneComponentKind::AudioListener:
        entity.RemoveComponent<Xora2D::AudioListenerComponent>();
        break;
    case SceneComponentKind::PythonScript:
        scene.ReleasePythonScriptInstance(entity);
        entity.RemoveComponent<Xora2D::PythonScriptComponent>();
        break;
    case SceneComponentKind::Animation:
        entity.RemoveComponent<Xora2D::AnimationComponent>();
        break;
    case SceneComponentKind::Tilemap:
        entity.RemoveComponent<Xora2D::TilemapComponent>(); // its collision body is released on the next physics sync
        break;
    case SceneComponentKind::ParticleSystem:
        entity.RemoveComponent<Xora2D::ParticleSystemComponent>();
        break;
    }
    return EditorCommandStatus::Success;
}

void SyncPhysicsWithScene(Xora2D::Scene& scene) {
    scene.SynchronizePhysicsBodies();
}

void EnsureSinglePrimaryCamera(Xora2D::Scene& scene, const Xora2D::UUID& primaryEntityUuid) {
    const auto uuids = scene.GetEntityUUIDs();
    for (const Xora2D::UUID& uuid : uuids) {
        if (uuid == primaryEntityUuid) {
            continue;
        }
        Xora2D::Entity other = scene.FindEntityByUUID(uuid);
        if (!other.IsValid() || !other.HasComponent<Xora2D::CameraComponent>()) {
            continue;
        }
        other.GetComponent<Xora2D::CameraComponent>().Primary = false;
    }
}

} // namespace Xora2DEditor