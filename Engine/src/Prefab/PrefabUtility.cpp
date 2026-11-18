#include "Xora2D/Prefab/PrefabUtility.h"

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Prefab/PrefabSerializer.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Tilemap/TilemapComponent.h"

#include "../Serialization/EntitySerialization.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <system_error>
#include <unordered_set>

namespace Xora2D {

namespace {

constexpr const char* kAbsent = "<absent>";

std::string EncodeFloat(float value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%a", static_cast<double>(value));
    return buffer;
}

std::string EncodeVec2(const glm::vec2& value) {
    return EncodeFloat(value.x) + "," + EncodeFloat(value.y);
}

std::string EncodeVec4(const glm::vec4& value) {
    return EncodeFloat(value.x) + "," + EncodeFloat(value.y) + "," + EncodeFloat(value.z) + "," +
           EncodeFloat(value.w);
}

template <typename Component>
void CopyComponent(const Entity& from, Entity& to) {
    if (from.HasComponent<Component>()) {
        if (!to.HasComponent<Component>()) {
            to.AddComponent<Component>();
        }
        to.GetComponent<Component>() = from.GetComponent<Component>();
    } else if (to.HasComponent<Component>()) {
        to.RemoveComponent<Component>();
    }
}

void CopySpriteTexture(const SpriteRendererComponent& from, SpriteRendererComponent& to) {
    to.TextureAsset = from.TextureAsset;
    to.TexturePath = from.TexturePath;
    to.Texture.reset(); // re-resolved from the handle in the target scene
}

template <typename Function>
void WithTransforms(const Entity& from, Entity& to, Function&& function) {
    const TransformComponent& source = from.GetComponent<TransformComponent>();
    TransformComponent& target = to.GetComponent<TransformComponent>();
    function(source, target);
    target.RecalculateLocalMatrix();
}

template <typename Function>
void WithSprites(const Entity& from, Entity& to, Function&& function) {
    if (from.HasComponent<SpriteRendererComponent>() && to.HasComponent<SpriteRendererComponent>()) {
        function(from.GetComponent<SpriteRendererComponent>(), to.GetComponent<SpriteRendererComponent>());
    }
}

std::string EncodeSprite(const Entity& entity, std::string (*field)(const SpriteRendererComponent&)) {
    return entity.HasComponent<SpriteRendererComponent>() ? field(entity.GetComponent<SpriteRendererComponent>())
                                                          : std::string(kAbsent);
}

// Whole-component encodings reuse the scene serializer: every serialized field counts.
std::map<std::string, std::string> EncodeComponents(const Entity& entity) {
    static const std::pair<const char*, const char*> kKeys[] = {
        {"SpriteRendererComponent", "Sprite"},       {"CameraComponent", "Camera"},
        {"RigidBody2DComponent", "RigidBody2D"},     {"BoxCollider2DComponent", "BoxCollider2D"},
        {"CircleCollider2DComponent", "CircleCollider2D"}, {"AudioSourceComponent", "AudioSource"},
        {"AudioListenerComponent", "AudioListener"}, {"PythonScriptComponent", "PythonScript"},
        {"AnimationComponent", "Animation"},          {"TilemapComponent", "Tilemap"},
    };
    YAML::Emitter emitter;
    SerializationDetail::EmitEntityNode(emitter, entity, false);
    const YAML::Node node = YAML::Load(emitter.c_str());
    std::map<std::string, std::string> encoded;
    for (const auto& [yamlKey, property] : kKeys) {
        if (const YAML::Node component = node[yamlKey]) {
            encoded[property] = YAML::Dump(component);
        } else {
            encoded[property] = kAbsent;
        }
    }
    return encoded;
}

struct PropertyDescriptor {
    PrefabPropertyInfo Info;
    std::string (*Encode)(const Entity&) = nullptr; // null for whole-component properties
    void (*Copy)(const Entity& from, Entity& to) = nullptr;
};

const std::vector<PropertyDescriptor>& Descriptors() {
    static const std::vector<PropertyDescriptor> descriptors = {
        {{"Name", "Name", "", false}, [](const Entity& e) { return e.GetName(); },
         [](const Entity& from, Entity& to) { to.SetName(from.GetName()); }},
        {{"Transform.Translation", "Position", "", false},
         [](const Entity& e) { return EncodeVec2(e.GetComponent<TransformComponent>().Translation); },
         [](const Entity& from, Entity& to) {
             WithTransforms(from, to, [](const auto& s, auto& t) { t.Translation = s.Translation; });
         }},
        {{"Transform.Rotation", "Rotation", "", false},
         [](const Entity& e) { return EncodeFloat(e.GetComponent<TransformComponent>().RotationRadians); },
         [](const Entity& from, Entity& to) {
             WithTransforms(from, to, [](const auto& s, auto& t) { t.RotationRadians = s.RotationRadians; });
         }},
        {{"Transform.Scale", "Scale", "", false},
         [](const Entity& e) { return EncodeVec2(e.GetComponent<TransformComponent>().Scale); },
         [](const Entity& from, Entity& to) {
             WithTransforms(from, to, [](const auto& s, auto& t) { t.Scale = s.Scale; });
         }},
        {{"Sprite.Color", "Sprite color", "Sprite", false},
         [](const Entity& e) { return EncodeSprite(e, [](const SpriteRendererComponent& s) { return EncodeVec4(s.Color); }); },
         [](const Entity& from, Entity& to) {
             WithSprites(from, to, [](const auto& s, auto& t) { t.Color = s.Color; });
         }},
        {{"Sprite.Visible", "Sprite visibility", "Sprite", false},
         [](const Entity& e) {
             return EncodeSprite(e, [](const SpriteRendererComponent& s) { return std::string(s.Visible ? "1" : "0"); });
         },
         [](const Entity& from, Entity& to) {
             WithSprites(from, to, [](const auto& s, auto& t) { t.Visible = s.Visible; });
         }},
        {{"Sprite.Size", "Sprite size", "Sprite", false},
         [](const Entity& e) { return EncodeSprite(e, [](const SpriteRendererComponent& s) { return EncodeVec2(s.Size); }); },
         [](const Entity& from, Entity& to) {
             WithSprites(from, to, [](const auto& s, auto& t) { t.Size = s.Size; });
         }},
        {{"Sprite.Texture", "Sprite texture", "Sprite", false},
         [](const Entity& e) {
             return EncodeSprite(e, [](const SpriteRendererComponent& s) {
                 return s.TextureAsset.ToString() + "|" + s.TexturePath;
             });
         },
         [](const Entity& from, Entity& to) { WithSprites(from, to, CopySpriteTexture); }},
        {{"Sprite", "Sprite Renderer", "Sprite", true}, nullptr, CopyComponent<SpriteRendererComponent>},
        {{"Camera", "Camera", "Camera", true}, nullptr, CopyComponent<CameraComponent>},
        {{"RigidBody2D", "Rigid Body 2D", "RigidBody2D", true}, nullptr, CopyComponent<RigidBody2DComponent>},
        {{"BoxCollider2D", "Box Collider 2D", "BoxCollider2D", true}, nullptr, CopyComponent<BoxCollider2DComponent>},
        {{"CircleCollider2D", "Circle Collider 2D", "CircleCollider2D", true}, nullptr,
         CopyComponent<CircleCollider2DComponent>},
        {{"AudioSource", "Audio Source", "AudioSource", true}, nullptr, CopyComponent<AudioSourceComponent>},
        {{"AudioListener", "Audio Listener", "AudioListener", true}, nullptr, CopyComponent<AudioListenerComponent>},
        {{"PythonScript", "Python Script", "PythonScript", true}, nullptr, CopyComponent<PythonScriptComponent>},
        {{"Animation", "Animation", "Animation", true}, nullptr, CopyComponent<AnimationComponent>},
        {{"Tilemap", "Tilemap", "Tilemap", true}, nullptr, CopyComponent<TilemapComponent>},
    };
    return descriptors;
}

const PropertyDescriptor* FindDescriptor(std::string_view path) {
    for (const PropertyDescriptor& descriptor : Descriptors()) {
        if (path == descriptor.Info.Path) {
            return &descriptor;
        }
    }
    return nullptr;
}

void CopyProperty(std::string_view path, const Entity& from, Entity& to) {
    if (const PropertyDescriptor* descriptor = FindDescriptor(path)) {
        descriptor->Copy(from, to);
    }
}

void SortUnique(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

// Whole-component overrides are re-applied before fine ones (a whole component contains the fine fields).
std::vector<std::string> OrderedOverrides(const std::vector<std::string>& overrides) {
    std::vector<std::string> ordered;
    for (const bool whole : {true, false}) {
        for (const std::string& path : overrides) {
            const PropertyDescriptor* descriptor = FindDescriptor(path);
            if (descriptor != nullptr && descriptor->Info.WholeComponent == whole) {
                ordered.push_back(path);
            }
        }
    }
    return ordered;
}

void SyncPhysicsIfSimulating(Scene& scene) {
    if (scene.GetPhysicsWorld().IsSimulationEnabled()) {
        scene.SynchronizePhysicsBodies();
    }
}

std::shared_ptr<const Prefab> LoadPrefabOrFail(AssetHandle handle, AssetManager& assets, std::string& error) {
    const AssetResult load = assets.LoadPrefab(handle);
    if (!load.Success) {
        error = load.Message;
        return nullptr;
    }
    std::shared_ptr<const Prefab> prefab = assets.GetLoadedPrefab(handle);
    if (prefab == nullptr || prefab->GetEntityCount() == 0) {
        error = "Prefab " + handle.ToString() + " has no entities";
        return nullptr;
    }
    return prefab;
}

PrefabResult Fail(std::string message) {
    PrefabResult result{};
    result.Message = std::move(message);
    return result;
}

// Shared by Refresh (keepOverrides) and Revert All.
PrefabResult SyncInstance(Scene& scene, const UUID& instanceId, AssetManager& assets, bool keepOverrides) {
    std::vector<Entity> entities = FindPrefabInstanceEntities(scene, instanceId);
    if (entities.empty()) {
        return Fail("No prefab instance " + instanceId.ToString() + " in the scene");
    }
    const AssetHandle handle = entities.front().GetComponent<PrefabInstanceComponent>().Prefab;
    std::string error;
    const std::shared_ptr<const Prefab> prefab = LoadPrefabOrFail(handle, assets, error);
    if (prefab == nullptr) {
        return Fail("Cannot update the instance: " + error);
    }

    auto scratch = Scene::Create();
    std::unordered_set<UUID> covered;
    std::size_t unlinked = 0;
    for (Entity& entity : entities) {
        PrefabInstanceComponent& link = entity.GetComponent<PrefabInstanceComponent>();
        const Entity source = prefab->FindEntity(link.SourceEntity);
        if (!source.IsValid()) {
            // Removed from the prefab: keep the user's entity, just detach it.
            entity.RemoveComponent<PrefabInstanceComponent>();
            ++unlinked;
            continue;
        }
        covered.insert(link.SourceEntity);
        Entity saved;
        if (keepOverrides && !link.Overrides.empty()) {
            saved = scratch->CreateEntity("saved");
            CopyEntityData(entity, saved);
        }
        CopyEntityData(source, entity);
        PrefabInstanceComponent& refreshed = entity.GetComponent<PrefabInstanceComponent>();
        if (keepOverrides) {
            if (saved.IsValid()) {
                for (const std::string& path : OrderedOverrides(refreshed.Overrides)) {
                    CopyProperty(path, saved, entity);
                }
            }
        } else {
            refreshed.Overrides.clear();
        }
        refreshed.Revision = prefab->GetRevision();
    }

    std::size_t added = 0;
    for (const UUID& sourceId : prefab->GetEntityIds()) {
        if (covered.contains(sourceId)) {
            continue;
        }
        const Entity source = prefab->FindEntity(sourceId);
        Entity created = scene.CreateEntity(source.GetName());
        CopyEntityData(source, created);
        auto& link = created.AddComponent<PrefabInstanceComponent>();
        link.Prefab = handle;
        link.InstanceId = instanceId;
        link.SourceEntity = sourceId;
        link.Revision = prefab->GetRevision();
        ++added;
    }
    SyncPhysicsIfSimulating(scene);

    PrefabResult result{};
    result.Success = true;
    result.Message = std::string(keepOverrides ? "Refreshed" : "Reverted") + " instance of '" + prefab->GetName() +
                     "' to revision " + std::to_string(prefab->GetRevision());
    if (added > 0) {
        result.Message += "; " + std::to_string(added) + " new entity(ies) added";
    }
    if (unlinked > 0) {
        result.Message += "; " + std::to_string(unlinked) + " entity(ies) no longer in the prefab were unlinked";
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------

const std::vector<PrefabPropertyInfo>& GetPrefabProperties() {
    static const std::vector<PrefabPropertyInfo> properties = [] {
        std::vector<PrefabPropertyInfo> list;
        for (const PropertyDescriptor& descriptor : Descriptors()) {
            list.push_back(descriptor.Info);
        }
        return list;
    }();
    return properties;
}

const PrefabPropertyInfo* FindPrefabProperty(std::string_view path) {
    const PropertyDescriptor* descriptor = FindDescriptor(path);
    return descriptor != nullptr ? &descriptor->Info : nullptr;
}

PrefabPropertySnapshot CapturePrefabProperties(const Entity& entity) {
    PrefabPropertySnapshot snapshot;
    if (!entity.IsValid()) {
        return snapshot;
    }
    for (const PropertyDescriptor& descriptor : Descriptors()) {
        if (descriptor.Encode != nullptr) {
            snapshot[descriptor.Info.Path] = descriptor.Encode(entity);
        }
    }
    for (auto& [property, encoded] : EncodeComponents(entity)) {
        snapshot[property] = std::move(encoded);
    }
    return snapshot;
}

namespace {

// Properties whose values differ between two snapshots. A whole component is reported only when something other
// than its fine properties changed.
std::vector<std::string> ChangedProperties(const PrefabPropertySnapshot& before, const PrefabPropertySnapshot& after,
                                           const Entity* lhs, const Entity* rhs) {
    std::vector<std::string> changed;
    std::unordered_set<std::string> componentsWithFineChange;
    for (const PropertyDescriptor& descriptor : Descriptors()) {
        if (descriptor.Info.WholeComponent) {
            continue;
        }
        const auto a = before.find(descriptor.Info.Path);
        const auto b = after.find(descriptor.Info.Path);
        if (a == before.end() || b == after.end() || a->second == b->second) {
            continue;
        }
        // A fine property of a component one side does not have is covered by the component itself.
        if (a->second == kAbsent || b->second == kAbsent) {
            continue;
        }
        changed.push_back(descriptor.Info.Path);
        componentsWithFineChange.insert(descriptor.Info.Component);
    }
    for (const PropertyDescriptor& descriptor : Descriptors()) {
        if (!descriptor.Info.WholeComponent) {
            continue;
        }
        const auto a = before.find(descriptor.Info.Path);
        const auto b = after.find(descriptor.Info.Path);
        if (a == before.end() || b == after.end() || a->second == b->second) {
            continue;
        }
        if (!componentsWithFineChange.contains(descriptor.Info.Component)) {
            changed.push_back(descriptor.Info.Path);
            continue;
        }
        // Both sides have the component and fine fields differ: the component is an override only if something
        // else differs too. Neutralize the fine fields and compare again (precise path; needs both entities).
        if (lhs != nullptr && rhs != nullptr) {
            auto scratch = Scene::Create();
            Entity probe = scratch->CreateEntity("probe");
            CopyEntityData(*lhs, probe);
            for (const PropertyDescriptor& fine : Descriptors()) {
                if (!fine.Info.WholeComponent && descriptor.Info.Component == std::string_view(fine.Info.Component)) {
                    fine.Copy(*rhs, probe);
                }
            }
            if (EncodeComponents(probe)[descriptor.Info.Path] != b->second) {
                changed.push_back(descriptor.Info.Path);
            }
        }
    }
    SortUnique(changed);
    return changed;
}

} // namespace

std::vector<std::string> DiffAgainstPrefab(const Entity& instance, const Prefab& prefab) {
    if (!IsPrefabInstance(instance)) {
        return {};
    }
    const Entity source = prefab.FindEntity(instance.GetComponent<PrefabInstanceComponent>().SourceEntity);
    if (!source.IsValid()) {
        return {};
    }
    return ChangedProperties(CapturePrefabProperties(source), CapturePrefabProperties(instance), &source, &instance);
}

std::vector<std::string> RecordPrefabEdits(Entity instance, const PrefabPropertySnapshot& before,
                                           const Prefab* prefab) {
    if (!IsPrefabInstance(instance)) {
        return {};
    }
    const PrefabPropertySnapshot after = CapturePrefabProperties(instance);
    const std::vector<std::string> changed = ChangedProperties(before, after, nullptr, nullptr);
    PrefabInstanceComponent& link = instance.GetComponent<PrefabInstanceComponent>();
    std::vector<std::string> added;
    for (const std::string& property : changed) {
        if (std::find(link.Overrides.begin(), link.Overrides.end(), property) == link.Overrides.end()) {
            added.push_back(property);
        }
        link.Overrides.push_back(property);
    }
    SortUnique(link.Overrides);

    // Values edited back to the prefab value stop being overrides (only meaningful when the instance is in sync).
    if (prefab != nullptr && link.Revision == prefab->GetRevision()) {
        const Entity source = prefab->FindEntity(link.SourceEntity);
        if (source.IsValid()) {
            const PrefabPropertySnapshot reference = CapturePrefabProperties(source);
            std::erase_if(link.Overrides, [&](const std::string& property) {
                const auto a = reference.find(property);
                const auto b = after.find(property);
                return a != reference.end() && b != after.end() && a->second == b->second;
            });
            std::erase_if(added, [&](const std::string& property) {
                return std::find(link.Overrides.begin(), link.Overrides.end(), property) == link.Overrides.end();
            });
        }
    }
    return added;
}

bool IsPrefabInstance(const Entity& entity) {
    return entity.IsValid() && entity.HasComponent<PrefabInstanceComponent>();
}

bool HasPrefabOverride(const Entity& entity, std::string_view property) {
    if (!IsPrefabInstance(entity)) {
        return false;
    }
    const auto& overrides = entity.GetComponent<PrefabInstanceComponent>().Overrides;
    return std::find(overrides.begin(), overrides.end(), property) != overrides.end();
}

std::vector<Entity> FindPrefabInstanceEntities(Scene& scene, const UUID& instanceId) {
    std::vector<Entity> entities;
    const std::vector<UUID> ids = scene.GetEntityUUIDs();
    // GetEntityUUIDs lists newest first; report creation order.
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        Entity entity = scene.FindEntityByUUID(*it);
        if (IsPrefabInstance(entity) && entity.GetComponent<PrefabInstanceComponent>().InstanceId == instanceId) {
            entities.push_back(entity);
        }
    }
    return entities;
}

std::vector<UUID> FindPrefabInstances(Scene& scene, AssetHandle prefab) {
    std::vector<UUID> instances;
    std::unordered_set<UUID> seen;
    const std::vector<UUID> ids = scene.GetEntityUUIDs();
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        const Entity entity = scene.FindEntityByUUID(*it);
        if (!IsPrefabInstance(entity)) {
            continue;
        }
        const auto& link = entity.GetComponent<PrefabInstanceComponent>();
        if (link.Prefab == prefab && seen.insert(link.InstanceId).second) {
            instances.push_back(link.InstanceId);
        }
    }
    return instances;
}

void CopyEntityData(const Entity& source, Entity& target) {
    for (const PropertyDescriptor& descriptor : Descriptors()) {
        // Fine Sprite properties are part of the whole Sprite component; Name and Transform are always fine.
        if (descriptor.Info.WholeComponent || descriptor.Info.Component[0] == '\0') {
            descriptor.Copy(source, target);
        }
    }
    if (target.HasComponent<SpriteRendererComponent>()) {
        target.GetComponent<SpriteRendererComponent>().Texture.reset();
    }
    if (target.HasComponent<AnimationComponent>()) {
        // Playback state is runtime-only; the target starts fresh.
        auto& animation = target.GetComponent<AnimationComponent>();
        animation.ActiveClip = {};
        animation.CurrentFrameIndex = 0;
        animation.FrameUVValid = false;
        animation.State = AnimationPlaybackState::Stopped;
        animation.Controller.Reset();
    }
}

std::string ValidatePrefabName(std::string_view name) {
    if (name.empty()) {
        return "Prefab name is empty";
    }
    if (name.size() > 64) {
        return "Prefab name is longer than 64 characters";
    }
    if (name.front() == '.' || name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return "Prefab name cannot start with '.' or a space, or end with a space or '.'";
    }
    constexpr std::string_view kReserved = "/\\:*?\"<>|";
    for (const char character : name) {
        if (static_cast<unsigned char>(character) < 0x20 || kReserved.find(character) != std::string_view::npos) {
            return "Prefab name contains a character that is not allowed in file names";
        }
    }
    return {};
}

PrefabCreateResult CreatePrefabFromEntities(const std::vector<Entity>& entities, const PrefabCreateOptions& options,
                                            AssetManager& assets) {
    PrefabCreateResult result{};
    if (std::string error = ValidatePrefabName(options.Name); !error.empty()) {
        result.Message = error;
        return result;
    }
    if (entities.empty()) {
        result.Message = "Select at least one entity";
        return result;
    }
    std::unordered_set<UUID> unique;
    for (const Entity& entity : entities) {
        if (!entity.IsValid() || !unique.insert(entity.GetUUID()).second) {
            result.Message = "Invalid or duplicate entity in the selection";
            return result;
        }
    }
    std::string folder;
    if (!options.AssetFolder.empty()) {
        const std::optional<std::string> normalized = NormalizeAssetRelativePath(options.AssetFolder);
        if (!normalized.has_value()) {
            result.Message = "Invalid prefab folder '" + options.AssetFolder + "'";
            return result;
        }
        folder = *normalized;
        while (!folder.empty() && folder.back() == '/') {
            folder.pop_back();
        }
    }
    if (folder == "Registry" || folder.starts_with("Registry/")) {
        result.Message = "Prefabs cannot be stored in the asset registry folder";
        return result;
    }
    const std::string relative = (folder.empty() ? std::string() : folder + "/") + options.Name + ".prefab.yaml";
    const std::filesystem::path absolute = ResolveAssetPath(relative);
    std::error_code errorCode;
    if (absolute.empty()) {
        result.Message = "Invalid prefab path";
        return result;
    }
    if (std::filesystem::exists(absolute, errorCode) || assets.GetRegistry().ContainsPath(relative)) {
        result.Message = "A prefab named '" + options.Name + "' already exists in Assets/" +
                         (folder.empty() ? std::string() : folder + "/") + " (prefabs are never overwritten)";
        return result;
    }

    Prefab prefab;
    prefab.SetName(options.Name);
    prefab.SetRevision(1);
    std::vector<UUID> sourceIds;
    for (const Entity& entity : entities) {
        const UUID id = UUID::Generate();
        Entity copy = prefab.CreateEntity(entity.GetName(), id);
        CopyEntityData(entity, copy);
        sourceIds.push_back(id);
    }

    const PrefabSaveResult save = PrefabSerializer::Save(prefab, absolute, false);
    if (!save.Success) {
        result.Message = save.Message;
        return result;
    }
    const AssetResult registered = assets.RegisterPrefabAtRelativePath(relative, options.Name);
    const AssetResult loaded = registered.Success ? assets.LoadPrefab(registered.Handle) : registered;
    if (!registered.Success || !loaded.Success) {
        // Roll back: no orphan file, no registry entry.
        if (registered.Success) {
            (void)assets.GetRegistry().RemoveAsset(registered.Handle);
        }
        std::filesystem::remove(absolute, errorCode);
        result.Message = "Cannot register the prefab: " + (registered.Success ? loaded.Message : registered.Message);
        return result;
    }

    result.Success = true;
    result.Handle = registered.Handle;
    result.RelativePath = relative;
    if (options.LinkSourceEntities) {
        result.InstanceId = UUID::Generate();
        for (std::size_t index = 0; index < entities.size(); ++index) {
            Entity entity = entities[index];
            if (!entity.HasComponent<PrefabInstanceComponent>()) {
                entity.AddComponent<PrefabInstanceComponent>();
            }
            PrefabInstanceComponent& link = entity.GetComponent<PrefabInstanceComponent>();
            link = PrefabInstanceComponent{};
            link.Prefab = registered.Handle;
            link.InstanceId = result.InstanceId;
            link.SourceEntity = sourceIds[index];
            link.Revision = 1;
        }
    }
    result.Message = "Created prefab Assets/" + relative + " from " + std::to_string(entities.size()) + " entity(ies)";
    return result;
}

PrefabInstantiateResult InstantiatePrefab(Scene& scene, AssetHandle handle, AssetManager& assets,
                                          const PrefabInstantiateOptions& options) {
    PrefabInstantiateResult result{};
    std::string error;
    const std::shared_ptr<const Prefab> prefab = LoadPrefabOrFail(handle, assets, error);
    if (prefab == nullptr) {
        result.Message = "Cannot instantiate prefab: " + error;
        return result;
    }

    glm::vec2 offset{0.0f};
    if (options.Position.has_value()) {
        const Entity first = prefab->FindEntity(prefab->GetEntityIds().front());
        offset = *options.Position - first.GetComponent<TransformComponent>().Translation;
    }

    const UUID instanceId = UUID::Generate();
    std::vector<Entity> created;
    const auto rollback = [&scene, &created]() {
        for (Entity& entity : created) {
            scene.DestroyEntity(entity);
        }
        created.clear();
    };
    try {
        const std::vector<UUID>& ids = prefab->GetEntityIds();
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (options.BeforeEntity && !options.BeforeEntity(index)) {
                rollback();
                result.Message = "Instantiation aborted at entity " + std::to_string(index) + "; nothing was created";
                return result;
            }
            const Entity source = prefab->FindEntity(ids[index]);
            if (!source.IsValid()) {
                rollback();
                result.Message = "Prefab '" + prefab->GetName() + "' is inconsistent; nothing was created";
                return result;
            }
            Entity entity = scene.CreateEntity(source.GetName());
            created.push_back(entity);
            CopyEntityData(source, entity);
            auto& link = entity.AddComponent<PrefabInstanceComponent>();
            link.Prefab = handle;
            link.InstanceId = instanceId;
            link.SourceEntity = ids[index];
            link.Revision = prefab->GetRevision();
            if (options.Position.has_value() && offset != glm::vec2{0.0f}) {
                auto& transform = entity.GetComponent<TransformComponent>();
                transform.Translation += offset;
                transform.RecalculateLocalMatrix();
                link.Overrides.push_back("Transform.Translation");
            }
        }
    } catch (const std::exception& exception) {
        rollback();
        result.Message = std::string("Instantiation failed; nothing was created: ") + exception.what();
        return result;
    }
    SyncPhysicsIfSimulating(scene);

    result.Success = true;
    result.InstanceId = instanceId;
    result.Entities = std::move(created);
    result.Message = "Instantiated prefab '" + prefab->GetName() + "' (" + std::to_string(result.Entities.size()) +
                     " entity(ies))";
    return result;
}

PrefabResult RevertPrefabOverride(Entity instance, std::string_view property, AssetManager& assets) {
    if (!IsPrefabInstance(instance)) {
        return Fail("Not a prefab instance");
    }
    const PropertyDescriptor* descriptor = FindDescriptor(property);
    if (descriptor == nullptr) {
        return Fail("Unknown prefab property '" + std::string(property) + "'");
    }
    PrefabInstanceComponent& link = instance.GetComponent<PrefabInstanceComponent>();
    std::string error;
    const std::shared_ptr<const Prefab> prefab = LoadPrefabOrFail(link.Prefab, assets, error);
    if (prefab == nullptr) {
        return Fail("Cannot revert: " + error);
    }
    const Entity source = prefab->FindEntity(link.SourceEntity);
    if (!source.IsValid()) {
        return Fail("Cannot revert: this entity no longer exists in prefab '" + prefab->GetName() + "'");
    }
    descriptor->Copy(source, instance);
    PrefabInstanceComponent& updated = instance.GetComponent<PrefabInstanceComponent>();
    std::erase(updated.Overrides, std::string(property));
    if (descriptor->Info.WholeComponent) {
        // The whole component now matches the prefab, so do its fine properties.
        std::erase_if(updated.Overrides, [&](const std::string& path) {
            const PropertyDescriptor* other = FindDescriptor(path);
            return other != nullptr && std::string_view(other->Info.Component) == descriptor->Info.Component;
        });
    }
    PrefabResult result{};
    result.Success = true;
    result.Message = std::string("Reverted ") + descriptor->Info.Label + " to the prefab value";
    return result;
}

PrefabResult RevertAllPrefabOverrides(Scene& scene, const UUID& instanceId, AssetManager& assets) {
    return SyncInstance(scene, instanceId, assets, false);
}

PrefabResult RefreshPrefabInstance(Scene& scene, const UUID& instanceId, AssetManager& assets) {
    return SyncInstance(scene, instanceId, assets, true);
}

PrefabResult UnpackPrefabInstance(Scene& scene, const UUID& instanceId) {
    std::vector<Entity> entities = FindPrefabInstanceEntities(scene, instanceId);
    if (entities.empty()) {
        return Fail("No prefab instance " + instanceId.ToString() + " in the scene");
    }
    for (Entity& entity : entities) {
        entity.RemoveComponent<PrefabInstanceComponent>();
    }
    PrefabResult result{};
    result.Success = true;
    result.Message = "Unpacked " + std::to_string(entities.size()) + " entity(ies); they are no longer linked";
    return result;
}

PrefabResult SavePrefabAsset(const Prefab& edited, AssetHandle handle, AssetManager& assets) {
    const AssetMetadata* metadata = assets.GetRegistry().FindByHandle(handle);
    if (metadata == nullptr || metadata->Type != AssetType::Prefab) {
        return Fail("Prefab asset " + handle.ToString() + " is not registered");
    }
    if (edited.GetEntityCount() == 0) {
        return Fail("A prefab needs at least one entity");
    }
    std::uint32_t current = edited.GetRevision();
    if (assets.LoadPrefab(handle).Success) {
        current = std::max(current, assets.GetLoadedPrefab(handle)->GetRevision());
    }
    const std::shared_ptr<Prefab> toSave = edited.Clone();
    toSave->SetRevision(current + 1);
    const PrefabSaveResult save = PrefabSerializer::Save(*toSave, ResolveAssetPath(metadata->RelativePath), true);
    if (!save.Success) {
        return Fail(save.Message);
    }
    const AssetResult reload = assets.ReloadAsset(handle);
    if (!reload.Success) {
        return Fail("Prefab saved but could not be reloaded: " + reload.Message);
    }
    PrefabResult result{};
    result.Success = true;
    result.Message = "Saved prefab '" + toSave->GetName() + "' (revision " + std::to_string(toSave->GetRevision()) + ")";
    return result;
}

PrefabResult ApplyPrefabInstance(Scene& scene, const UUID& instanceId, AssetManager& assets) {
    std::vector<Entity> entities = FindPrefabInstanceEntities(scene, instanceId);
    if (entities.empty()) {
        return Fail("No prefab instance " + instanceId.ToString() + " in the scene");
    }
    const AssetHandle handle = entities.front().GetComponent<PrefabInstanceComponent>().Prefab;
    std::string error;
    const std::shared_ptr<const Prefab> current = LoadPrefabOrFail(handle, assets, error);
    if (current == nullptr) {
        return Fail("Cannot apply: " + error);
    }
    const std::shared_ptr<Prefab> edited = current->Clone();
    for (const Entity& entity : entities) {
        const auto& link = entity.GetComponent<PrefabInstanceComponent>();
        if (link.Prefab != handle) {
            return Fail("Instance entities refer to different prefabs; nothing was applied");
        }
        Entity target = edited->FindEntity(link.SourceEntity);
        if (!target.IsValid()) {
            target = edited->CreateEntity(entity.GetName(), link.SourceEntity);
        }
        CopyEntityData(entity, target);
    }
    const PrefabResult saved = SavePrefabAsset(*edited, handle, assets);
    if (!saved.Success) {
        return Fail("Apply failed, the prefab is unchanged: " + saved.Message);
    }
    const std::uint32_t revision = assets.GetLoadedPrefab(handle)->GetRevision();
    for (Entity& entity : entities) {
        auto& link = entity.GetComponent<PrefabInstanceComponent>();
        link.Overrides.clear();
        link.Revision = revision;
    }
    PrefabResult result{};
    result.Success = true;
    result.Message = "Applied instance to prefab '" + current->GetName() + "' (revision " + std::to_string(revision) +
                     "). Other instances update only when refreshed.";
    return result;
}

} // namespace Xora2D
