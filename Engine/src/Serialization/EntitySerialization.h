#pragma once

// Internal: per-entity YAML parse / emit / apply shared by SceneSerializer and PrefabSerializer, so scenes and
// prefabs use exactly the same component format.

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Tilemap/TilemapComponent.h"

#include <glm/vec2.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace YAML {
class Emitter;
class Node;
} // namespace YAML

namespace Xora2D {

class AssetManager;
class Scene;

namespace SerializationDetail {

struct ParsedEntity {
    UUID Id;
    std::string Tag;
    std::optional<glm::vec2> Translation;
    std::optional<float> RotationRadians;
    std::optional<glm::vec2> Scale;
    std::optional<SpriteRendererComponent> Sprite;
    std::optional<CameraComponent> Camera;
    std::optional<RigidBody2DComponent> RigidBody;
    std::optional<BoxCollider2DComponent> BoxCollider;
    std::optional<CircleCollider2DComponent> CircleCollider;
    std::optional<AudioSourceComponent> AudioSource;
    std::optional<AudioListenerComponent> AudioListener;
    std::optional<PythonScriptComponent> PythonScript;
    std::optional<AnimationComponent> Animation;
    std::optional<PrefabInstanceComponent> PrefabInstance;
    std::optional<TilemapComponent> Tilemap;
};

// Parses one entry of an "Entities" sequence. `source` is only used in error messages.
bool ParseEntityNode(const YAML::Node& node, ParsedEntity& output, const std::filesystem::path& source,
                     std::string& error);
// Texture references must resolve (registered handle, or an existing legacy path).
bool ValidateEntityAssets(const ParsedEntity& parsed, const std::filesystem::path& source, AssetManager* assetManager,
                          std::string& error);
// Creates the entity with `id` (the parsed UUID for scenes, a fresh one for prefab instances) and its components.
Entity ApplyParsedEntity(Scene& scene, const ParsedEntity& parsed, const UUID& id, AssetManager* assetManager);
// Emits one entity map. `includePrefabLink` = false drops PrefabInstanceComponent (prefab files never nest).
void EmitEntityNode(YAML::Emitter& emitter, const Entity& entity, bool includePrefabLink);

} // namespace SerializationDetail
} // namespace Xora2D
