#pragma once

#include "Xora2DEditor/EditorSelection.h"

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <string>

namespace Xora2DEditor {

enum class SceneComponentKind {
    SpriteRenderer,
    Camera,
    RigidBody2D,
    BoxCollider2D,
    CircleCollider2D,
    AudioSource,
    AudioListener,
    PythonScript,
    Animation,
    Tilemap,
};

enum class EditorCommandStatus {
    Success,
    InvalidEntity,
    DuplicateComponent,
    ComponentNotPresent,
    InvalidName,
};

[[nodiscard]] Xora2D::Entity CreateEmptyEntity(Xora2D::Scene& scene, const std::string& name = "Entity");

[[nodiscard]] EditorCommandStatus DestroyEntity(Xora2D::Scene& scene, EditorSelection& selection, const Xora2D::UUID& uuid);

[[nodiscard]] EditorCommandStatus RenameEntity(Xora2D::Entity entity, const std::string& newName);

[[nodiscard]] bool CanAddComponent(const Xora2D::Entity& entity, SceneComponentKind kind);
[[nodiscard]] EditorCommandStatus AddComponentToEntity(Xora2D::Scene& scene, Xora2D::Entity& entity, SceneComponentKind kind);
[[nodiscard]] EditorCommandStatus RemoveComponentFromEntity(Xora2D::Scene& scene, Xora2D::Entity& entity, SceneComponentKind kind);

void SyncPhysicsWithScene(Xora2D::Scene& scene);


void EnsureSinglePrimaryCamera(Xora2D::Scene& scene, const Xora2D::UUID& primaryEntityUuid);

} // namespace Xora2DEditor