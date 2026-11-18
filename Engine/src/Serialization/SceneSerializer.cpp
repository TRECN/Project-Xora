#include "Xora2D/Serialization/SceneSerializer.h"

#include "EntitySerialization.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Tilemap/TilemapSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Xora2D {
namespace {

SceneSerializeResult Failure(std::string message) {
    return SceneSerializeResult{false, std::move(message), {}};
}

SceneSerializeResult Success() {
    return SceneSerializeResult{true, {}, {}};
}

std::string PathContext(const std::filesystem::path& filepath) {
    return filepath.string();
}

using SerializationDetail::ParsedEntity;

struct ParsedScene {
    int Version = 0;
    std::string Name;
    std::vector<ParsedEntity> Entities;
};

bool ReadVec2(const YAML::Node& node, glm::vec2& output, const std::string& field,
              const std::filesystem::path& filepath, std::string& error) {
    if (!node) {
        error = PathContext(filepath) + ": missing field '" + field + "'";
        return false;
    }
    if (!node.IsSequence() || node.size() != 2) {
        error = PathContext(filepath) + ": '" + field + "' must be a sequence of two numbers";
        return false;
    }
    output.x = node[0].as<float>();
    output.y = node[1].as<float>();
    if (!std::isfinite(output.x) || !std::isfinite(output.y)) {
        error = PathContext(filepath) + ": '" + field + "' contains non-finite values";
        return false;
    }
    return true;
}

bool ReadVec4(const YAML::Node& node, glm::vec4& output, const std::string& field,
              const std::filesystem::path& filepath, std::string& error) {
    if (!node) {
        error = PathContext(filepath) + ": missing field '" + field + "'";
        return false;
    }
    if (!node.IsSequence() || node.size() != 4) {
        error = PathContext(filepath) + ": '" + field + "' must be a sequence of four numbers";
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = node[static_cast<int>(index)].as<float>();
        if (!std::isfinite(output[index])) {
            error = PathContext(filepath) + ": '" + field + "' contains non-finite values";
            return false;
        }
    }
    return true;
}

bool ReadOptionalVec2(const YAML::Node& node, glm::vec2& output, const std::string& field,
                      const std::filesystem::path& filepath, std::string& error) {
    if (!node) {
        return true;
    }
    return ReadVec2(node, output, field, filepath, error);
}

std::optional<RigidBodyType2D> ParseRigidBodyType(const std::string& text, const std::filesystem::path& filepath,
                                                    std::string& error) {
    if (text == "Static") {
        return RigidBodyType2D::Static;
    }
    if (text == "Dynamic") {
        return RigidBodyType2D::Dynamic;
    }
    if (text == "Kinematic") {
        return RigidBodyType2D::Kinematic;
    }
    error = PathContext(filepath) + ": invalid RigidBody2D Type '" + text + "'";
    return std::nullopt;
}

YAML::Node EmitVec2(const glm::vec2& value) {
    YAML::Node node(YAML::NodeType::Sequence);
    node.push_back(value.x);
    node.push_back(value.y);
    return node;
}

YAML::Node EmitVec4(const glm::vec4& value) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (int index = 0; index < 4; ++index) {
        node.push_back(value[index]);
    }
    return node;
}

std::optional<AudioGroup> ParseAudioGroup(const std::string& text, const std::filesystem::path& filepath,
                                          std::string& error) {
    if (text == "Music") {
        return AudioGroup::Music;
    }
    if (text == "SoundEffects") {
        return AudioGroup::SoundEffects;
    }
    error = PathContext(filepath) + ": invalid AudioSourceComponent.Group '" + text + "'";
    return std::nullopt;
}

std::string AudioGroupToString(AudioGroup group) {
    switch (group) {
    case AudioGroup::Music:
        return "Music";
    case AudioGroup::SoundEffects:
        return "SoundEffects";
    default:
        return "SoundEffects";
    }
}

std::string RigidBodyTypeToString(RigidBodyType2D type) {
    switch (type) {
    case RigidBodyType2D::Static:
        return "Static";
    case RigidBodyType2D::Dynamic:
        return "Dynamic";
    case RigidBodyType2D::Kinematic:
        return "Kinematic";
    }
    return "Dynamic";
}

bool ValidateParsedScene(const ParsedScene& document, const std::filesystem::path& filepath, std::string& error) {
    if (document.Version != SceneSerializer::kFormatVersion &&
        document.Version != SceneSerializer::kLegacyFormatVersion) {
        std::ostringstream stream;
        stream << PathContext(filepath) << ": unsupported scene Version " << document.Version;
        error = stream.str();
        return false;
    }
    std::unordered_set<UUID> seenIds;
    for (const ParsedEntity& entity : document.Entities) {
        if (!entity.Id.IsValid()) {
            error = PathContext(filepath) + ": entity has invalid UUID";
            return false;
        }
        if (!seenIds.insert(entity.Id).second) {
            error = PathContext(filepath) + ": duplicate entity UUID " + entity.Id.ToString();
            return false;
        }
        if (entity.Tag.empty()) {
            error = PathContext(filepath) + ": entity " + entity.Id.ToString() + " missing TagComponent Tag";
            return false;
        }
        if (!entity.Translation.has_value()) {
            error = PathContext(filepath) + ": entity " + entity.Id.ToString() + " missing TransformComponent";
            return false;
        }
    }
    return true;
}

bool ParseEntity(const YAML::Node& node, ParsedEntity& output, const std::filesystem::path& filepath,
                 std::string& error) {
    const YAML::Node entityIdNode = node["Entity"];
    if (!entityIdNode) {
        error = PathContext(filepath) + ": entity entry missing 'Entity' UUID";
        return false;
    }
    const std::optional<UUID> uuid = UUID::FromString(entityIdNode.as<std::string>());
    if (!uuid.has_value()) {
        error = PathContext(filepath) + ": invalid entity UUID '" + entityIdNode.as<std::string>() + "'";
        return false;
    }
    output.Id = *uuid;

    const YAML::Node tagNode = node["TagComponent"];
    if (!tagNode || !tagNode["Tag"]) {
        error = PathContext(filepath) + ": entity " + output.Id.ToString() + " missing TagComponent.Tag";
        return false;
    }
    output.Tag = tagNode["Tag"].as<std::string>();

    const YAML::Node transformNode = node["TransformComponent"];
    if (!transformNode) {
        error = PathContext(filepath) + ": entity " + output.Id.ToString() + " missing TransformComponent";
        return false;
    }
    glm::vec2 translation{};
    if (!ReadVec2(transformNode["Translation"], translation, "TransformComponent.Translation", filepath, error)) {
        return false;
    }
    output.Translation = translation;
    if (transformNode["RotationRadians"]) {
        output.RotationRadians = transformNode["RotationRadians"].as<float>();
        if (!std::isfinite(*output.RotationRadians)) {
            error = PathContext(filepath) + ": TransformComponent.RotationRadians is not finite";
            return false;
        }
    } else {
        output.RotationRadians = 0.0f;
    }
    glm::vec2 scale{1.0f, 1.0f};
    if (transformNode["Scale"]) {
        if (!ReadVec2(transformNode["Scale"], scale, "TransformComponent.Scale", filepath, error)) {
            return false;
        }
    }
    output.Scale = scale;

    if (const YAML::Node spriteNode = node["SpriteRendererComponent"]) {
        SpriteRendererComponent sprite{};
        if (spriteNode["Color"]) {
            if (!ReadVec4(spriteNode["Color"], sprite.Color, "SpriteRendererComponent.Color", filepath, error)) {
                return false;
            }
        }
        if (spriteNode["Size"]) {
            if (!ReadVec2(spriteNode["Size"], sprite.Size, "SpriteRendererComponent.Size", filepath, error)) {
                return false;
            }
        }
        if (spriteNode["TextureAssetHandle"]) {
            const std::optional<AssetHandle> handle =
                AssetHandle::FromString(spriteNode["TextureAssetHandle"].as<std::string>());
            if (!handle.has_value()) {
                error = PathContext(filepath) + ": invalid SpriteRendererComponent.TextureAssetHandle";
                return false;
            }
            sprite.TextureAsset = *handle;
        }
        if (spriteNode["TexturePath"]) {
            sprite.TexturePath = spriteNode["TexturePath"].as<std::string>();
        }
        if (spriteNode["TilingFactor"]) {
            sprite.TilingFactor = spriteNode["TilingFactor"].as<float>();
        }
        if (spriteNode["Visible"]) {
            sprite.Visible = spriteNode["Visible"].as<bool>();
        }
        output.Sprite = sprite;
    }

    if (const YAML::Node cameraNode = node["CameraComponent"]) {
        CameraComponent camera{};
        if (cameraNode["Enabled"]) {
            camera.Enabled = cameraNode["Enabled"].as<bool>();
        }
        if (cameraNode["Primary"]) {
            camera.Primary = cameraNode["Primary"].as<bool>();
        }
        if (cameraNode["Zoom"]) {
            camera.Zoom = cameraNode["Zoom"].as<float>();
            if (!std::isfinite(camera.Zoom) || camera.Zoom <= 0.0f) {
                error = PathContext(filepath) + ": CameraComponent.Zoom must be a positive finite number";
                return false;
            }
        }
        output.Camera = camera;
    }

    if (const YAML::Node bodyNode = node["RigidBody2DComponent"]) {
        RigidBody2DComponent body{};
        if (!bodyNode["Type"]) {
            error = PathContext(filepath) + ": RigidBody2DComponent missing Type";
            return false;
        }
        const std::optional<RigidBodyType2D> type =
            ParseRigidBodyType(bodyNode["Type"].as<std::string>(), filepath, error);
        if (!type.has_value()) {
            return false;
        }
        body.Type = *type;
        if (bodyNode["FixedRotation"]) {
            body.FixedRotation = bodyNode["FixedRotation"].as<bool>();
        }
        if (bodyNode["GravityScale"]) {
            body.GravityScale = bodyNode["GravityScale"].as<float>();
        }
        if (bodyNode["LinearDamping"]) {
            body.LinearDamping = bodyNode["LinearDamping"].as<float>();
        }
        if (bodyNode["AngularDamping"]) {
            body.AngularDamping = bodyNode["AngularDamping"].as<float>();
        }
        if (bodyNode["AllowSleep"]) {
            body.AllowSleep = bodyNode["AllowSleep"].as<bool>();
        }
        if (bodyNode["Bullet"]) {
            body.Bullet = bodyNode["Bullet"].as<bool>();
        }
        if (bodyNode["Enabled"]) {
            body.Enabled = bodyNode["Enabled"].as<bool>();
        }
        if (bodyNode["DriveTransformFromECS"]) {
            body.DriveTransformFromECS = bodyNode["DriveTransformFromECS"].as<bool>();
        }
        output.RigidBody = body;
    }

    if (const YAML::Node boxNode = node["BoxCollider2DComponent"]) {
        BoxCollider2DComponent box{};
        if (boxNode["Offset"]) {
            if (!ReadOptionalVec2(boxNode["Offset"], box.Offset, "BoxCollider2DComponent.Offset", filepath, error)) {
                return false;
            }
        }
        if (boxNode["Size"]) {
            if (!ReadVec2(boxNode["Size"], box.Size, "BoxCollider2DComponent.Size", filepath, error)) {
                return false;
            }
        }
        if (box.Size.x <= 0.0f || box.Size.y <= 0.0f) {
            error = PathContext(filepath) + ": BoxCollider2DComponent.Size must be positive";
            return false;
        }
        if (boxNode["Density"]) {
            box.Density = boxNode["Density"].as<float>();
        }
        if (boxNode["Friction"]) {
            box.Friction = boxNode["Friction"].as<float>();
        }
        if (boxNode["Restitution"]) {
            box.Restitution = boxNode["Restitution"].as<float>();
        }
        if (boxNode["RestitutionThreshold"]) {
            box.RestitutionThreshold = boxNode["RestitutionThreshold"].as<float>();
        }
        if (boxNode["IsSensor"]) {
            box.IsSensor = boxNode["IsSensor"].as<bool>();
        }
        output.BoxCollider = box;
    }

    if (const YAML::Node circleNode = node["CircleCollider2DComponent"]) {
        CircleCollider2DComponent circle{};
        if (circleNode["Offset"]) {
            if (!ReadOptionalVec2(circleNode["Offset"], circle.Offset, "CircleCollider2DComponent.Offset", filepath,
                                  error)) {
                return false;
            }
        }
        if (!circleNode["Radius"]) {
            error = PathContext(filepath) + ": CircleCollider2DComponent missing Radius";
            return false;
        }
        circle.Radius = circleNode["Radius"].as<float>();
        if (!std::isfinite(circle.Radius) || circle.Radius <= 0.0f) {
            error = PathContext(filepath) + ": CircleCollider2DComponent.Radius must be positive and finite";
            return false;
        }
        if (circleNode["Density"]) {
            circle.Density = circleNode["Density"].as<float>();
        }
        if (circleNode["Friction"]) {
            circle.Friction = circleNode["Friction"].as<float>();
        }
        if (circleNode["Restitution"]) {
            circle.Restitution = circleNode["Restitution"].as<float>();
        }
        if (circleNode["RestitutionThreshold"]) {
            circle.RestitutionThreshold = circleNode["RestitutionThreshold"].as<float>();
        }
        if (circleNode["IsSensor"]) {
            circle.IsSensor = circleNode["IsSensor"].as<bool>();
        }
        output.CircleCollider = circle;
    }

    if (const YAML::Node audioNode = node["AudioSourceComponent"]) {
        AudioSourceComponent audio{};
        if (audioNode["ClipAssetHandle"]) {
            const std::optional<AssetHandle> handle =
                AssetHandle::FromString(audioNode["ClipAssetHandle"].as<std::string>());
            if (!handle.has_value()) {
                error = PathContext(filepath) + ": invalid AudioSourceComponent.ClipAssetHandle";
                return false;
            }
            audio.ClipAsset = *handle;
        }
        if (audioNode["Volume"]) {
            audio.Volume = audioNode["Volume"].as<float>();
        }
        if (audioNode["Pitch"]) {
            audio.Pitch = audioNode["Pitch"].as<float>();
        }
        if (audioNode["Loop"]) {
            audio.Loop = audioNode["Loop"].as<bool>();
        }
        if (audioNode["PlayOnStart"]) {
            audio.PlayOnStart = audioNode["PlayOnStart"].as<bool>();
        }
        if (audioNode["Muted"]) {
            audio.Muted = audioNode["Muted"].as<bool>();
        }
        if (audioNode["Group"]) {
            const std::optional<AudioGroup> group =
                ParseAudioGroup(audioNode["Group"].as<std::string>(), filepath, error);
            if (!group.has_value()) {
                return false;
            }
            audio.Group = *group;
        }
        if (audioNode["SpatialAudio"]) {
            audio.SpatialAudio = audioNode["SpatialAudio"].as<bool>();
        }
        output.AudioSource = audio;
    }

    if (const YAML::Node listenerNode = node["AudioListenerComponent"]) {
        AudioListenerComponent listener{};
        if (listenerNode["Active"]) {
            listener.Active = listenerNode["Active"].as<bool>();
        }
        output.AudioListener = listener;
    }

    if (const YAML::Node scriptNode = node["PythonScriptComponent"]) {
        PythonScriptComponent script{};
        if (scriptNode["ScriptAssetHandle"]) {
            const std::optional<AssetHandle> handle =
                AssetHandle::FromString(scriptNode["ScriptAssetHandle"].as<std::string>());
            if (!handle.has_value()) {
                error = PathContext(filepath) + ": invalid PythonScriptComponent.ScriptAssetHandle";
                return false;
            }
            script.ScriptAsset = *handle;
        }
        if (scriptNode["ClassName"]) {
            script.ClassName = scriptNode["ClassName"].as<std::string>();
        }
        if (scriptNode["Enabled"]) {
            script.Enabled = scriptNode["Enabled"].as<bool>();
        }
        if (const YAML::Node propsNode = scriptNode["Properties"]) {
            if (!propsNode.IsSequence()) {
                error = PathContext(filepath) + ": PythonScriptComponent.Properties must be a sequence";
                return false;
            }
            for (const YAML::Node& propertyNode : propsNode) {
                if (!propertyNode["Name"] || !propertyNode["Type"]) {
                    error = PathContext(filepath) + ": script property requires Name and Type";
                    return false;
                }
                ScriptProperty property{};
                property.Name = propertyNode["Name"].as<std::string>();
                const std::string type = propertyNode["Type"].as<std::string>();
                if (type == "Number") {
                    property.Type = ScriptPropertyType::Number;
                    property.NumberValue = propertyNode["Value"] ? propertyNode["Value"].as<double>() : 0.0;
                } else if (type == "Boolean") {
                    property.Type = ScriptPropertyType::Boolean;
                    property.BooleanValue = propertyNode["Value"] ? propertyNode["Value"].as<bool>() : false;
                } else if (type == "String") {
                    property.Type = ScriptPropertyType::String;
                    property.StringValue = propertyNode["Value"] ? propertyNode["Value"].as<std::string>() : "";
                } else {
                    error = PathContext(filepath) + ": invalid script property Type '" + type + "'";
                    return false;
                }
                script.Properties.push_back(std::move(property));
            }
        }
        output.PythonScript = script;
    }

    if (const YAML::Node animNode = node["AnimationComponent"]) {
        AnimationComponent animation{};
        if (animNode["DefaultClip"]) {
            const std::optional<AssetHandle> handle =
                AssetHandle::FromString(animNode["DefaultClip"].as<std::string>());
            if (!handle.has_value()) {
                error = PathContext(filepath) + ": invalid AnimationComponent.DefaultClip";
                return false;
            }
            animation.DefaultClip = *handle;
        }
        if (animNode["DefaultClipName"]) {
            animation.DefaultClipName = animNode["DefaultClipName"].as<std::string>();
        }
        if (animNode["PlaybackSpeed"]) {
            animation.PlaybackSpeed = animNode["PlaybackSpeed"].as<float>();
        }
        if (animNode["Looping"]) {
            animation.Looping = animNode["Looping"].as<bool>();
        }
        if (animNode["Autoplay"]) {
            animation.Autoplay = animNode["Autoplay"].as<bool>();
        }
        if (const YAML::Node clips = animNode["ClipAssets"]) {
            if (!clips.IsSequence()) {
                error = PathContext(filepath) + ": AnimationComponent.ClipAssets must be a sequence";
                return false;
            }
            for (const YAML::Node& clipNode : clips) {
                const std::optional<AssetHandle> handle = AssetHandle::FromString(clipNode.as<std::string>());
                if (!handle.has_value()) {
                    error = PathContext(filepath) + ": invalid AnimationComponent clip handle";
                    return false;
                }
                animation.ClipAssets.push_back(*handle);
            }
        }
        output.Animation = animation;
    }

    if (const YAML::Node tilemapNode = node["TilemapComponent"]) {
        TilemapComponent tilemap{};
        std::string tilemapError;
        if (!TilemapSerializer::ParseBody(tilemapNode, tilemap, tilemapError)) {
            error = PathContext(filepath) + ": entity " + output.Id.ToString() + " TilemapComponent: " + tilemapError;
            return false;
        }
        output.Tilemap = std::move(tilemap);
    }

    if (const YAML::Node prefabNode = node["PrefabInstance"]) {
        PrefabInstanceComponent link{};
        const auto readUuid = [&](const char* key, UUID& target) {
            if (!prefabNode[key]) {
                return false;
            }
            const std::optional<UUID> parsedId = UUID::FromString(prefabNode[key].as<std::string>());
            if (!parsedId.has_value()) {
                return false;
            }
            target = *parsedId;
            return true;
        };
        const std::optional<AssetHandle> prefabHandle =
            prefabNode["Prefab"] ? AssetHandle::FromString(prefabNode["Prefab"].as<std::string>()) : std::nullopt;
        if (!prefabHandle.has_value() || !readUuid("Instance", link.InstanceId) ||
            !readUuid("SourceEntity", link.SourceEntity)) {
            error = PathContext(filepath) + ": entity " + output.Id.ToString() +
                    " has an invalid PrefabInstance (needs Prefab, Instance and SourceEntity)";
            return false;
        }
        link.Prefab = *prefabHandle;
        if (prefabNode["Revision"]) {
            link.Revision = prefabNode["Revision"].as<std::uint32_t>();
        }
        if (const YAML::Node overrides = prefabNode["Overrides"]) {
            if (!overrides.IsSequence()) {
                error = PathContext(filepath) + ": PrefabInstance.Overrides must be a sequence";
                return false;
            }
            for (const YAML::Node& item : overrides) {
                link.Overrides.push_back(item.as<std::string>());
            }
            std::sort(link.Overrides.begin(), link.Overrides.end());
            link.Overrides.erase(std::unique(link.Overrides.begin(), link.Overrides.end()), link.Overrides.end());
        }
        output.PrefabInstance = std::move(link);
    }

    return true;
}

bool ParseSceneDocument(const YAML::Node& root, ParsedScene& output, const std::filesystem::path& filepath,
                        std::string& error) {
    if (!root["Version"]) {
        error = PathContext(filepath) + ": missing root 'Version'";
        return false;
    }
    output.Version = root["Version"].as<int>();
    if (root["Name"]) {
        output.Name = root["Name"].as<std::string>();
    }
    const YAML::Node entitiesNode = root["Entities"];
    if (!entitiesNode) {
        output.Entities.clear();
        return true;
    }
    if (!entitiesNode.IsSequence()) {
        error = PathContext(filepath) + ": 'Entities' must be a sequence";
        return false;
    }
    for (const YAML::Node& entityNode : entitiesNode) {
        ParsedEntity entity{};
        if (!ParseEntity(entityNode, entity, filepath, error)) {
            return false;
        }
        output.Entities.push_back(std::move(entity));
    }
    return ValidateParsedScene(output, filepath, error);
}

bool ValidateEntityTextures(const ParsedEntity& parsed, const std::filesystem::path& filepath,
                            AssetManager* assetManager, std::string& error) {
    if (!parsed.Sprite.has_value()) {
        return true;
    }
    if (parsed.Sprite->TextureAsset.IsValid()) {
        if (assetManager == nullptr) {
            error = PathContext(filepath) + ": scene references TextureAssetHandle but no AssetManager was provided";
            return false;
        }
        if (assetManager->GetRegistry().FindByHandle(parsed.Sprite->TextureAsset) == nullptr) {
            std::ostringstream stream;
            stream << PathContext(filepath) << ": unknown texture asset handle "
                   << parsed.Sprite->TextureAsset.ToString();
            error = stream.str();
            return false;
        }
        return true;
    }
    if (!parsed.Sprite->TexturePath.empty()) {
        const std::filesystem::path absolute = ResolveContentPath(parsed.Sprite->TexturePath);
        if (!std::filesystem::exists(absolute)) {
            std::ostringstream stream;
            stream << PathContext(filepath) << ": texture asset not found '" << parsed.Sprite->TexturePath
                   << "' (resolved " << absolute.string() << ")";
            error = stream.str();
            return false;
        }
    }
    return true;
}

bool ValidateTextureAssets(const ParsedScene& document, const std::filesystem::path& filepath,
                             AssetManager* assetManager, std::string& error) {
    for (const ParsedEntity& parsed : document.Entities) {
        if (!ValidateEntityTextures(parsed, filepath, assetManager, error)) {
            return false;
        }
    }
    return true;
}

Entity ApplyEntity(Scene& scene, const ParsedEntity& parsed, const UUID& id, AssetManager* assetManager) {
    Entity entity = scene.CreateEntityWithUUID(parsed.Tag, id);

    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Translation = *parsed.Translation;
    transform.RotationRadians = parsed.RotationRadians.value_or(0.0f);
    transform.Scale = parsed.Scale.value_or(glm::vec2{1.0f, 1.0f});
    transform.RecalculateLocalMatrix();

    if (parsed.Sprite.has_value()) {
        auto& sprite = entity.AddComponent<SpriteRendererComponent>();
        sprite = *parsed.Sprite;
        sprite.Texture.reset();
        if (sprite.TextureAsset.IsValid() && assetManager != nullptr) {
            (void)assetManager;
        } else if (!sprite.TexturePath.empty() && assetManager != nullptr) {
            const AssetResult legacy = assetManager->EnsureLegacyTexturePath(sprite.TexturePath);
            if (legacy.Success) {
                sprite.TextureAsset = legacy.Handle;
            }
            sprite.TexturePath.clear();
        }
    }
    if (parsed.Camera.has_value()) {
        entity.AddComponent<CameraComponent>() = *parsed.Camera;
    }
    if (parsed.RigidBody.has_value()) {
        entity.AddComponent<RigidBody2DComponent>() = *parsed.RigidBody;
    }
    if (parsed.BoxCollider.has_value()) {
        entity.AddComponent<BoxCollider2DComponent>() = *parsed.BoxCollider;
    }
    if (parsed.CircleCollider.has_value()) {
        entity.AddComponent<CircleCollider2DComponent>() = *parsed.CircleCollider;
    }
    if (parsed.AudioSource.has_value()) {
        entity.AddComponent<AudioSourceComponent>() = *parsed.AudioSource;
    }
    if (parsed.PythonScript.has_value()) {
        entity.AddComponent<PythonScriptComponent>() = *parsed.PythonScript;
    }
    if (parsed.Animation.has_value()) {
        auto& animation = entity.AddComponent<AnimationComponent>();
        animation = *parsed.Animation;
        animation.ActiveClip = {};
        animation.CurrentFrameIndex = 0;
        animation.FrameUVValid = false;
        animation.State = AnimationPlaybackState::Stopped;
        animation.Controller.Reset();
    }
    if (parsed.AudioListener.has_value()) {
        entity.AddComponent<AudioListenerComponent>() = *parsed.AudioListener;
    }
    if (parsed.PrefabInstance.has_value()) {
        entity.AddComponent<PrefabInstanceComponent>() = *parsed.PrefabInstance;
    }
    if (parsed.Tilemap.has_value()) {
        entity.AddComponent<TilemapComponent>() = *parsed.Tilemap;
    }
    return entity;
}

bool ApplyParsedScene(Scene& scene, const ParsedScene& document, AssetManager* assetManager) {
    scene.Clear();
    scene.SetSceneName(document.Name.empty() ? "Untitled" : document.Name);
    for (const ParsedEntity& parsed : document.Entities) {
        (void)ApplyEntity(scene, parsed, parsed.Id, assetManager);
    }
    return true;
}

void EmitEntity(YAML::Emitter& emitter, const Entity& entity, bool includePrefabLink) {
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "Entity" << YAML::Value << entity.GetUUID().ToString();
    emitter << YAML::Key << "TagComponent" << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "Tag" << YAML::Value << entity.GetName();
    emitter << YAML::EndMap;

    const TransformComponent& transform = entity.GetComponent<TransformComponent>();
    emitter << YAML::Key << "TransformComponent" << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "Translation" << YAML::Value << EmitVec2(transform.Translation);
    emitter << YAML::Key << "RotationRadians" << YAML::Value << transform.RotationRadians;
    emitter << YAML::Key << "Scale" << YAML::Value << EmitVec2(transform.Scale);
    emitter << YAML::EndMap;

    if (entity.HasComponent<SpriteRendererComponent>()) {
        const SpriteRendererComponent& sprite = entity.GetComponent<SpriteRendererComponent>();
        emitter << YAML::Key << "SpriteRendererComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Color" << YAML::Value << EmitVec4(sprite.Color);
        emitter << YAML::Key << "Size" << YAML::Value << EmitVec2(sprite.Size);
        if (sprite.TextureAsset.IsValid()) {
            emitter << YAML::Key << "TextureAssetHandle" << YAML::Value << sprite.TextureAsset.ToString();
        } else if (!sprite.TexturePath.empty()) {
            emitter << YAML::Key << "TexturePath" << YAML::Value << sprite.TexturePath;
        }
        emitter << YAML::Key << "TilingFactor" << YAML::Value << sprite.TilingFactor;
        emitter << YAML::Key << "Visible" << YAML::Value << sprite.Visible;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<CameraComponent>()) {
        const CameraComponent& camera = entity.GetComponent<CameraComponent>();
        emitter << YAML::Key << "CameraComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Enabled" << YAML::Value << camera.Enabled;
        emitter << YAML::Key << "Primary" << YAML::Value << camera.Primary;
        emitter << YAML::Key << "Zoom" << YAML::Value << camera.Zoom;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<RigidBody2DComponent>()) {
        const RigidBody2DComponent& body = entity.GetComponent<RigidBody2DComponent>();
        emitter << YAML::Key << "RigidBody2DComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Type" << YAML::Value << RigidBodyTypeToString(body.Type);
        emitter << YAML::Key << "FixedRotation" << YAML::Value << body.FixedRotation;
        emitter << YAML::Key << "GravityScale" << YAML::Value << body.GravityScale;
        emitter << YAML::Key << "LinearDamping" << YAML::Value << body.LinearDamping;
        emitter << YAML::Key << "AngularDamping" << YAML::Value << body.AngularDamping;
        emitter << YAML::Key << "AllowSleep" << YAML::Value << body.AllowSleep;
        emitter << YAML::Key << "Bullet" << YAML::Value << body.Bullet;
        emitter << YAML::Key << "Enabled" << YAML::Value << body.Enabled;
        emitter << YAML::Key << "DriveTransformFromECS" << YAML::Value << body.DriveTransformFromECS;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<BoxCollider2DComponent>()) {
        const BoxCollider2DComponent& box = entity.GetComponent<BoxCollider2DComponent>();
        emitter << YAML::Key << "BoxCollider2DComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Offset" << YAML::Value << EmitVec2(box.Offset);
        emitter << YAML::Key << "Size" << YAML::Value << EmitVec2(box.Size);
        emitter << YAML::Key << "Density" << YAML::Value << box.Density;
        emitter << YAML::Key << "Friction" << YAML::Value << box.Friction;
        emitter << YAML::Key << "Restitution" << YAML::Value << box.Restitution;
        emitter << YAML::Key << "RestitutionThreshold" << YAML::Value << box.RestitutionThreshold;
        emitter << YAML::Key << "IsSensor" << YAML::Value << box.IsSensor;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<CircleCollider2DComponent>()) {
        const CircleCollider2DComponent& circle = entity.GetComponent<CircleCollider2DComponent>();
        emitter << YAML::Key << "CircleCollider2DComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Offset" << YAML::Value << EmitVec2(circle.Offset);
        emitter << YAML::Key << "Radius" << YAML::Value << circle.Radius;
        emitter << YAML::Key << "Density" << YAML::Value << circle.Density;
        emitter << YAML::Key << "Friction" << YAML::Value << circle.Friction;
        emitter << YAML::Key << "Restitution" << YAML::Value << circle.Restitution;
        emitter << YAML::Key << "RestitutionThreshold" << YAML::Value << circle.RestitutionThreshold;
        emitter << YAML::Key << "IsSensor" << YAML::Value << circle.IsSensor;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<AudioSourceComponent>()) {
        const AudioSourceComponent& audio = entity.GetComponent<AudioSourceComponent>();
        emitter << YAML::Key << "AudioSourceComponent" << YAML::Value << YAML::BeginMap;
        if (audio.ClipAsset.IsValid()) {
            emitter << YAML::Key << "ClipAssetHandle" << YAML::Value << audio.ClipAsset.ToString();
        }
        emitter << YAML::Key << "Volume" << YAML::Value << audio.Volume;
        emitter << YAML::Key << "Pitch" << YAML::Value << audio.Pitch;
        emitter << YAML::Key << "Loop" << YAML::Value << audio.Loop;
        emitter << YAML::Key << "PlayOnStart" << YAML::Value << audio.PlayOnStart;
        emitter << YAML::Key << "Muted" << YAML::Value << audio.Muted;
        emitter << YAML::Key << "Group" << YAML::Value << AudioGroupToString(audio.Group);
        emitter << YAML::Key << "SpatialAudio" << YAML::Value << audio.SpatialAudio;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<AudioListenerComponent>()) {
        const AudioListenerComponent& listener = entity.GetComponent<AudioListenerComponent>();
        emitter << YAML::Key << "AudioListenerComponent" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Active" << YAML::Value << listener.Active;
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<AnimationComponent>()) {
        const AnimationComponent& animation = entity.GetComponent<AnimationComponent>();
        emitter << YAML::Key << "AnimationComponent" << YAML::Value << YAML::BeginMap;
        if (animation.DefaultClip.IsValid()) {
            emitter << YAML::Key << "DefaultClip" << YAML::Value << animation.DefaultClip.ToString();
        }
        if (!animation.DefaultClipName.empty()) {
            emitter << YAML::Key << "DefaultClipName" << YAML::Value << animation.DefaultClipName;
        }
        emitter << YAML::Key << "PlaybackSpeed" << YAML::Value << animation.PlaybackSpeed;
        emitter << YAML::Key << "Looping" << YAML::Value << animation.Looping;
        emitter << YAML::Key << "Autoplay" << YAML::Value << animation.Autoplay;
        if (!animation.ClipAssets.empty()) {
            emitter << YAML::Key << "ClipAssets" << YAML::Value << YAML::BeginSeq;
            for (const AssetHandle& handle : animation.ClipAssets) {
                emitter << handle.ToString();
            }
            emitter << YAML::EndSeq;
        }
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<PythonScriptComponent>()) {
        const PythonScriptComponent& script = entity.GetComponent<PythonScriptComponent>();
        emitter << YAML::Key << "PythonScriptComponent" << YAML::Value << YAML::BeginMap;
        if (script.ScriptAsset.IsValid()) {
            emitter << YAML::Key << "ScriptAssetHandle" << YAML::Value << script.ScriptAsset.ToString();
        }
        if (!script.ClassName.empty()) {
            emitter << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
        }
        emitter << YAML::Key << "Enabled" << YAML::Value << script.Enabled;
        if (!script.Properties.empty()) {
            emitter << YAML::Key << "Properties" << YAML::Value << YAML::BeginSeq;
            for (const ScriptProperty& property : script.Properties) {
                emitter << YAML::BeginMap;
                emitter << YAML::Key << "Name" << YAML::Value << property.Name;
                switch (property.Type) {
                case ScriptPropertyType::Number:
                    emitter << YAML::Key << "Type" << YAML::Value << "Number";
                    emitter << YAML::Key << "Value" << YAML::Value << property.NumberValue;
                    break;
                case ScriptPropertyType::Boolean:
                    emitter << YAML::Key << "Type" << YAML::Value << "Boolean";
                    emitter << YAML::Key << "Value" << YAML::Value << property.BooleanValue;
                    break;
                case ScriptPropertyType::String:
                    emitter << YAML::Key << "Type" << YAML::Value << "String";
                    emitter << YAML::Key << "Value" << YAML::Value << property.StringValue;
                    break;
                }
                emitter << YAML::EndMap;
            }
            emitter << YAML::EndSeq;
        }
        emitter << YAML::EndMap;
    }

    if (entity.HasComponent<TilemapComponent>()) {
        emitter << YAML::Key << "TilemapComponent" << YAML::Value << YAML::BeginMap;
        TilemapSerializer::EmitBody(emitter, entity.GetComponent<TilemapComponent>());
        emitter << YAML::EndMap;
    }

    if (includePrefabLink && entity.HasComponent<PrefabInstanceComponent>()) {
        const PrefabInstanceComponent& link = entity.GetComponent<PrefabInstanceComponent>();
        emitter << YAML::Key << "PrefabInstance" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Prefab" << YAML::Value << link.Prefab.ToString();
        emitter << YAML::Key << "Instance" << YAML::Value << link.InstanceId.ToString();
        emitter << YAML::Key << "SourceEntity" << YAML::Value << link.SourceEntity.ToString();
        emitter << YAML::Key << "Revision" << YAML::Value << link.Revision;
        emitter << YAML::Key << "Overrides" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const std::string& property : link.Overrides) {
            emitter << property;
        }
        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;
    }

    emitter << YAML::EndMap;
}

} // namespace

namespace SerializationDetail {

bool ParseEntityNode(const YAML::Node& node, ParsedEntity& output, const std::filesystem::path& source,
                     std::string& error) {
    return ParseEntity(node, output, source, error);
}

bool ValidateEntityAssets(const ParsedEntity& parsed, const std::filesystem::path& source, AssetManager* assetManager,
                          std::string& error) {
    return ValidateEntityTextures(parsed, source, assetManager, error);
}

Entity ApplyParsedEntity(Scene& scene, const ParsedEntity& parsed, const UUID& id, AssetManager* assetManager) {
    return ApplyEntity(scene, parsed, id, assetManager);
}

void EmitEntityNode(YAML::Emitter& emitter, const Entity& entity, bool includePrefabLink) {
    EmitEntity(emitter, entity, includePrefabLink);
}

} // namespace SerializationDetail

SceneSerializeResult SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& filepath) {
    try {
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "Version" << YAML::Value << kFormatVersion;
        emitter << YAML::Key << "Name" << YAML::Value << scene.GetSceneName();
        emitter << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        std::vector<UUID> uuids = scene.GetEntityUUIDs();
        std::sort(uuids.begin(), uuids.end(),
                  [](const UUID& left, const UUID& right) { return left.ToString() < right.ToString(); });

        Scene& mutableScene = const_cast<Scene&>(scene);
        for (const UUID& uuid : uuids) {
            Entity entity = mutableScene.FindEntityByUUID(uuid);
            if (!entity.IsValid()) {
                continue;
            }
            EmitEntity(emitter, entity, true);
        }

        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;

        std::filesystem::create_directories(filepath.parent_path());
        std::ofstream output(filepath);
        if (!output.is_open()) {
            return Failure("Failed to open '" + filepath.string() + "' for writing");
        }
        output << emitter.c_str();
        if (!output.good()) {
            return Failure("Failed while writing '" + filepath.string() + "'");
        }
        return Success();
    } catch (const std::exception& exception) {
        return Failure(std::string("Serialize failed for '") + filepath.string() + "': " + exception.what());
    }
}

SceneSerializeResult SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& filepath,
                                                  AssetManager* assetManager) {
    if (!std::filesystem::exists(filepath)) {
        return Failure("Scene file does not exist: " + filepath.string());
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(filepath.string());
    } catch (const YAML::Exception& exception) {
        return Failure(std::string("Malformed YAML in '") + filepath.string() + "': " + exception.what());
    }

    ParsedScene document;
    std::string error;
    if (!ParseSceneDocument(root, document, filepath, error)) {
        return Failure(error);
    }
    if (!ValidateTextureAssets(document, filepath, assetManager, error)) {
        return Failure(error);
    }
    std::vector<std::string> warnings;
    if (assetManager != nullptr) {
        // Tilemaps: missing tilesets / textures and undefined tile ids are reported, never fatal.
        for (const ParsedEntity& parsed : document.Entities) {
            if (parsed.Tilemap.has_value()) {
                for (const std::string& warning : TilemapSerializer::ValidateReferences(*parsed.Tilemap, *assetManager)) {
                    warnings.push_back(PathContext(filepath) + ": tilemap '" + parsed.Tag + "': " + warning);
                }
            }
        }
        std::unordered_set<AssetHandle> reported;
        for (const ParsedEntity& parsed : document.Entities) {
            if (!parsed.PrefabInstance.has_value()) {
                continue;
            }
            const AssetHandle prefab = parsed.PrefabInstance->Prefab;
            const AssetMetadata* meta = assetManager->GetRegistry().FindByHandle(prefab);
            if ((meta == nullptr || meta->Type != AssetType::Prefab) && reported.insert(prefab).second) {
                // The entity keeps its data and its link; it simply cannot be reverted/refreshed until the prefab
                // asset is back.
                warnings.push_back(PathContext(filepath) + ": prefab asset " + prefab.ToString() +
                                   " is missing; its instances keep their data");
            }
        }
    }

    try {
        auto staging = Scene::Create();
        staging->SetViewportSize(scene.GetViewportWidth(), scene.GetViewportHeight());
        staging->SetAssetManager(assetManager);
        ApplyParsedScene(*staging, document, assetManager);
        scene.ReplaceContentsFrom(*staging);
    } catch (const std::exception& exception) {
        return Failure(std::string("Deserialize failed for '") + filepath.string() + "': " + exception.what());
    }

    SceneSerializeResult result = Success();
    for (const std::string& warning : warnings) {
        XORA_LOG_WARN("{}", warning);
    }
    result.Warnings = std::move(warnings);
    return result;
}

} // namespace Xora2D