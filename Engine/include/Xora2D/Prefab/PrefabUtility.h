#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Xora2D {

class AssetManager;
class Prefab;
class Scene;

// ---------------------------------------------------------------------------------------------------------------
// Override properties
// ---------------------------------------------------------------------------------------------------------------
//
// Fine-grained properties: "Name", "Transform.Translation", "Transform.Rotation", "Transform.Scale", "Sprite.Color",
// "Sprite.Visible", "Sprite.Size", "Sprite.Texture".
// Whole-component properties (every serialized field of the component, including whether it exists): "Sprite",
// "Camera", "RigidBody2D", "BoxCollider2D", "CircleCollider2D", "AudioSource", "AudioListener", "PythonScript",
// "Animation". An edit marks the fine properties it touched; an edit to anything else in a component marks the
// component as a whole, so no instance change is ever untracked.
struct PrefabPropertyInfo {
    const char* Path = "";
    const char* Label = "";
    // Empty for fine properties of Transform / Name; otherwise the whole-component property it belongs to.
    const char* Component = "";
    bool WholeComponent = false;
};

[[nodiscard]] const std::vector<PrefabPropertyInfo>& GetPrefabProperties();
[[nodiscard]] const PrefabPropertyInfo* FindPrefabProperty(std::string_view path);

// Encoded value of every property of `entity` (exact: floats are bit-exact). Used to find what an edit changed.
using PrefabPropertySnapshot = std::map<std::string, std::string>;
[[nodiscard]] PrefabPropertySnapshot CapturePrefabProperties(const Entity& entity);

// Properties of `instance` whose value differs from its source entity in `prefab` (inspection / tests; not used for
// per-frame tracking).
[[nodiscard]] std::vector<std::string> DiffAgainstPrefab(const Entity& instance, const Prefab& prefab);

// Event-driven override tracking: call after an edit with the snapshot taken before it. Marks the changed
// properties as overrides of the instance. When the instance is up to date with `prefab` (same revision), marks
// whose value equals the prefab again are dropped. Returns the properties newly marked. No-op for non-instances.
std::vector<std::string> RecordPrefabEdits(Entity instance, const PrefabPropertySnapshot& before,
                                           const Prefab* prefab);

[[nodiscard]] bool IsPrefabInstance(const Entity& entity);
[[nodiscard]] bool HasPrefabOverride(const Entity& entity, std::string_view property);
// Entities of one instance (same InstanceId), in scene order.
[[nodiscard]] std::vector<Entity> FindPrefabInstanceEntities(Scene& scene, const UUID& instanceId);
// Distinct instance ids of `prefab` in the scene.
[[nodiscard]] std::vector<UUID> FindPrefabInstances(Scene& scene, AssetHandle prefab);

// Copies every serialized component (and the name) of `source` into `target`, adding/removing components so the
// target mirrors the source. PrefabInstanceComponent is never copied. Entities may live in different scenes.
void CopyEntityData(const Entity& source, Entity& target);

// ---------------------------------------------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------------------------------------------

struct PrefabResult {
    bool Success = false;
    std::string Message;
};

// Prefab names become file names: 1-64 characters, no path separators or reserved characters, no leading '.'.
[[nodiscard]] std::string ValidatePrefabName(std::string_view name);

struct PrefabCreateOptions {
    std::string Name;
    // Folder under the project's Assets/ (e.g. "Prefabs"); created when missing.
    std::string AssetFolder = "Prefabs";
    // Turn the source entities into instances of the new prefab.
    bool LinkSourceEntities = true;
};

struct PrefabCreateResult : PrefabResult {
    AssetHandle Handle{};
    // Assets-relative path of the new prefab ("Prefabs/Crate.prefab.yaml").
    std::string RelativePath;
    UUID InstanceId{};
};

// Serializes the entities (in the given order) into a new prefab asset, registers it with a stable handle and
// optionally links the entities as its first instance. Never overwrites an existing file or registered path. On
// any failure nothing is left behind (no file, no registry entry, entities unchanged).
PrefabCreateResult CreatePrefabFromEntities(const std::vector<Entity>& entities, const PrefabCreateOptions& options,
                                            AssetManager& assets);

struct PrefabInstantiateOptions {
    // Moves the instance so its first entity lands here (other entities keep their offsets); marks the translations
    // as overrides. Without it the prefab's own positions are used.
    std::optional<glm::vec2> Position;
    // Test hook: called before each entity is created; returning false aborts (verifies the rollback).
    std::function<bool(std::size_t index)> BeforeEntity;
};

struct PrefabInstantiateResult : PrefabResult {
    UUID InstanceId{};
    std::vector<Entity> Entities;
};

// Creates a new, independent instance (fresh UUIDs). Transactional: on failure every entity created so far is
// destroyed again.
PrefabInstantiateResult InstantiatePrefab(Scene& scene, AssetHandle prefab, AssetManager& assets,
                                          const PrefabInstantiateOptions& options = {});

// Restores one property of one instance entity from the prefab and clears its override mark.
PrefabResult RevertPrefabOverride(Entity instance, std::string_view property, AssetManager& assets);
// Restores every entity of the instance from the prefab (current revision) and clears all overrides.
PrefabResult RevertAllPrefabOverrides(Scene& scene, const UUID& instanceId, AssetManager& assets);

// Writes the instance's current data into its prefab asset (new revision, same handle and file). Validated before
// writing; on failure the asset file and cache stay unchanged. Only this instance is updated (overrides cleared,
// revision bumped); other instances change only through RefreshPrefabInstance.
PrefabResult ApplyPrefabInstance(Scene& scene, const UUID& instanceId, AssetManager& assets);

// Explicit update of an instance to the prefab's current revision. Overridden properties keep the instance values;
// entities added to the prefab are created; instance entities whose source was removed from the prefab are kept and
// unlinked (never deleted).
PrefabResult RefreshPrefabInstance(Scene& scene, const UUID& instanceId, AssetManager& assets);

// Detaches the instance: entities stay, the prefab link and overrides are removed.
PrefabResult UnpackPrefabInstance(Scene& scene, const UUID& instanceId);

// Saves an edited prefab (from the isolated editing workflow) to its asset: revision + 1, same handle, atomic.
PrefabResult SavePrefabAsset(const Prefab& edited, AssetHandle handle, AssetManager& assets);

} // namespace Xora2D
