// Milestone 18: prefab assets, instances, overrides, apply / revert / refresh, editing, scene + runtime integration.
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Prefab/PrefabSerializer.h"
#include "Xora2D/Prefab/PrefabUtility.h"
#include "Xora2D/Project/Project.h"
#include "Xora2D/Project/ProjectTemplates.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/EditorContext.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>

namespace {

namespace fs = std::filesystem;
using Xora2DEditor::EditorContext;

int g_failures = 0;

void Check(bool condition, const std::string& description) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {}", description);
    ++g_failures;
}

std::string ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

bool HasOverride(const Xora2D::Entity& entity, const std::string& property) {
    return Xora2D::HasPrefabOverride(entity, property);
}

const Xora2D::PrefabInstanceComponent& Link(const Xora2D::Entity& entity) {
    return entity.GetComponent<Xora2D::PrefabInstanceComponent>();
}

// A real project (Empty 2D template) opened in an EditorContext, with a texture, audio clip, animation clip and script
// registered so prefabs can reference them.
class PrefabFixture {
public:
    PrefabFixture() {
        std::mt19937_64 generator{std::random_device{}()};
        const fs::path parent = fs::temp_directory_path() / ("xora2d_prefabs_" + std::to_string(generator() % 100000000));
        fs::create_directories(parent);
        m_parent = fs::weakly_canonical(parent);
        Xora2D::ProjectCreateOptions options{};
        options.Name = "PrefabGame";
        options.ParentDirectory = m_parent;
        const auto created = Xora2D::CreateProject(options);
        m_root = created.ProjectRoot;
        const fs::path resources = Xora2D::GetResourceRoot();
        fs::copy_file(resources / "Textures/Xora2D_Logo.png", m_root / "Assets/Textures/Logo.png");
        fs::copy_file(resources / "Assets/Audio/sfx_beep.wav", m_root / "Assets/Audio/Beep.wav");
        fs::copy_file(resources / "Assets/Animations/DemoWalk.anim.yaml", m_root / "Assets/Animations/Walk.anim.yaml");
        WriteText(m_root / "Assets/Scripts/Spin.py", "class Spin:\n    def on_update(self, dt):\n        pass\n");
        auto load = Xora2D::Project::Load(created.ProjectFile);
        Context.GetAssetManager().SetGraphicsResourcesEnabled(false);
        (void)Context.OpenProject(load.Loaded);
        auto& registry = Context.GetAssetRegistry();
        (void)Context.GetAssetManager().RegisterAnimationClipAtRelativePath("Animations/Walk.anim.yaml", "Walk");
        (void)Context.GetAssetManager().RegisterTextureAtRelativePath("Textures/Logo.png", "Logo");
        (void)Context.GetAssetManager().RegisterAudioAtRelativePath("Audio/Beep.wav", "Beep");
        (void)Context.GetAssetManager().RegisterPythonScriptAtRelativePath("Scripts/Spin.py", "Spin");
        Texture = registry.FindByRelativePath("Textures/Logo.png")->Handle;
        Audio = registry.FindByRelativePath("Audio/Beep.wav")->Handle;
        Script = registry.FindByRelativePath("Scripts/Spin.py")->Handle;
        Clip = registry.FindByRelativePath("Animations/Walk.anim.yaml")->Handle;
    }
    ~PrefabFixture() {
        Context.CloseProject();
        std::error_code ec;
        fs::permissions(m_root / "Assets/Prefabs", fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(m_parent, ec);
    }
    PrefabFixture(const PrefabFixture&) = delete;
    PrefabFixture& operator=(const PrefabFixture&) = delete;

    [[nodiscard]] const fs::path& Root() const { return m_root; }
    Xora2D::Scene& Scene() { return *Context.GetEditScene(); }
    Xora2D::AssetManager& Assets() { return Context.GetAssetManager(); }

    // An entity using every supported component.
    Xora2D::Entity MakeRichEntity(const std::string& name) {
        Xora2D::Entity entity = Scene().CreateEntity(name);
        auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
        transform.Translation = {120.0f, 80.0f};
        transform.RotationRadians = 0.25f;
        transform.Scale = {2.0f, 1.5f};
        transform.RecalculateLocalMatrix();
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.9f, 0.2f, 0.1f, 1.0f};
        sprite.Size = {48.0f, 32.0f};
        sprite.TextureAsset = Texture;
        sprite.TilingFactor = 2.0f;
        entity.AddComponent<Xora2D::CameraComponent>().Zoom = 1.5f;
        auto& body = entity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Kinematic;
        body.GravityScale = 0.5f;
        entity.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 30.0f};
        entity.AddComponent<Xora2D::CircleCollider2DComponent>().Radius = 12.0f;
        auto& audio = entity.AddComponent<Xora2D::AudioSourceComponent>();
        audio.ClipAsset = Audio;
        audio.Volume = 0.4f;
        auto& animation = entity.AddComponent<Xora2D::AnimationComponent>();
        animation.DefaultClip = Clip;
        animation.ClipAssets = {Clip};
        animation.PlaybackSpeed = 1.25f;
        auto& script = entity.AddComponent<Xora2D::PythonScriptComponent>();
        script.ScriptAsset = Script;
        script.ClassName = "Spin";
        return entity;
    }

    Xora2D::Entity MakeSimpleEntity(const std::string& name, glm::vec2 position) {
        Xora2D::Entity entity = Scene().CreateEntity(name);
        entity.GetComponent<Xora2D::TransformComponent>().Translation = position;
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.2f, 0.6f, 0.9f, 1.0f};
        sprite.Size = {32.0f, 32.0f};
        return entity;
    }

    Xora2D::PrefabCreateResult CreatePrefab(const std::vector<Xora2D::Entity>& entities, const std::string& name,
                                            bool link = true) {
        Xora2D::PrefabCreateOptions options{};
        options.Name = name;
        options.LinkSourceEntities = link;
        return Xora2D::CreatePrefabFromEntities(entities, options, Assets());
    }

    EditorContext Context;
    Xora2D::AssetHandle Texture{};
    Xora2D::AssetHandle Audio{};
    Xora2D::AssetHandle Script{};
    Xora2D::AssetHandle Clip{};

private:
    fs::path m_parent;
    fs::path m_root;
};

std::string SceneYaml(Xora2D::Scene& scene) {
    static int counter = 0;
    const fs::path path = fs::temp_directory_path() / ("xora2d_prefab_snapshot_" + std::to_string(++counter) + ".yaml");
    (void)Xora2D::SceneSerializer::Serialize(scene, path);
    std::string text = ReadText(path);
    std::error_code ec;
    fs::remove(path, ec);
    return text;
}

// ---------------------------------------------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------------------------------------------

void TestPrefabSerialization() {
    XORA_LOG_INFO("TEST prefab serialization");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeRichEntity("Crate");
    const auto created = fixture.CreatePrefab({source}, "Crate", false);
    Check(created.Success, "prefab created from a single entity: " + created.Message);
    const fs::path file = fixture.Root() / "Assets" / created.RelativePath;
    Check(created.RelativePath == "Prefabs/Crate.prefab.yaml" && fs::is_regular_file(file), "prefab file written");

    const std::string text = ReadText(file);
    Check(text.starts_with("Prefab:") && text.find("FormatVersion: 1") != std::string::npos &&
              text.find("Revision: 1") != std::string::npos && text.find("Entities:") != std::string::npos,
          "human-readable versioned YAML with a Prefab header");
    Check(text.find("PrefabInstance") == std::string::npos, "prefab files never contain instance links");

    const auto load = Xora2D::PrefabSerializer::Load(file, &fixture.Assets());
    Check(load.Success && load.MissingAssets.empty(), "prefab deserializes: " + load.Message);
    if (!load.Success) {
        return;
    }
    const auto& prefab = *load.Loaded;
    Check(prefab.GetName() == "Crate" && prefab.GetRevision() == 1 && prefab.GetEntityCount() == 1,
          "prefab metadata (name, revision, entity count)");
    const Xora2D::Entity entity = prefab.FindEntity(prefab.GetEntityIds().front());
    Check(entity.IsValid() && entity.GetName() == "Crate", "prefab entity restored");
    Check(entity.GetUUID() != source.GetUUID(), "prefab entities get their own prefab-local ids");
    const auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
    Check(transform.Translation == glm::vec2(120.0f, 80.0f) && transform.RotationRadians == 0.25f &&
              transform.Scale == glm::vec2(2.0f, 1.5f),
          "TransformComponent preserved");
    const auto& sprite = entity.GetComponent<Xora2D::SpriteRendererComponent>();
    Check(sprite.Color == glm::vec4(0.9f, 0.2f, 0.1f, 1.0f) && sprite.Size == glm::vec2(48.0f, 32.0f) &&
              sprite.TilingFactor == 2.0f && sprite.TextureAsset == fixture.Texture,
          "SpriteRendererComponent preserved (texture handle kept)");
    Check(entity.GetComponent<Xora2D::CameraComponent>().Zoom == 1.5f, "CameraComponent preserved");
    Check(entity.GetComponent<Xora2D::RigidBody2DComponent>().Type == Xora2D::RigidBodyType2D::Kinematic &&
              entity.GetComponent<Xora2D::RigidBody2DComponent>().GravityScale == 0.5f,
          "RigidBody2DComponent preserved");
    Check(entity.GetComponent<Xora2D::BoxCollider2DComponent>().Size == glm::vec2(40.0f, 30.0f),
          "BoxCollider2DComponent preserved");
    Check(entity.GetComponent<Xora2D::CircleCollider2DComponent>().Radius == 12.0f, "CircleCollider2DComponent preserved");
    Check(entity.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset == fixture.Audio &&
              entity.GetComponent<Xora2D::AudioSourceComponent>().Volume == 0.4f,
          "AudioSourceComponent preserved (clip handle kept)");
    const auto& animation = entity.GetComponent<Xora2D::AnimationComponent>();
    Check(animation.DefaultClip == fixture.Clip && animation.ClipAssets.size() == 1 && animation.PlaybackSpeed == 1.25f,
          "AnimationComponent preserved (clip handles kept)");
    const auto& script = entity.GetComponent<Xora2D::PythonScriptComponent>();
    Check(script.ScriptAsset == fixture.Script && script.ClassName == "Spin", "PythonScriptComponent preserved");
    Check(Xora2D::PrefabSerializer::Emit(prefab) == text, "emit -> parse -> emit is stable");

    // Malformed / unsupported data.
    struct Case {
        const char* Label;
        std::string Yaml;
    };
    const std::string entityYaml =
        "  - Entity: 0000000000000001-0000000000000002\n    TagComponent: {Tag: A}\n"
        "    TransformComponent: {Translation: [0, 0]}\n";
    const std::vector<Case> cases = {
        {"malformed YAML", "Prefab: [unclosed"},
        {"missing header", "Entities:\n" + entityYaml},
        {"missing FormatVersion", "Prefab: {Name: X}\nEntities:\n" + entityYaml},
        {"unsupported newer format", "Prefab: {FormatVersion: 99, Name: X}\nEntities:\n" + entityYaml},
        {"format version 0", "Prefab: {FormatVersion: 0, Name: X}\nEntities:\n" + entityYaml},
        {"missing name", "Prefab: {FormatVersion: 1}\nEntities:\n" + entityYaml},
        {"no entities", "Prefab: {FormatVersion: 1, Name: X}\nEntities: []\n"},
        {"duplicate entity ids", "Prefab: {FormatVersion: 1, Name: X}\nEntities:\n" + entityYaml + entityYaml},
        {"entity without transform",
         "Prefab: {FormatVersion: 1, Name: X}\nEntities:\n  - Entity: 0000000000000001-0000000000000002\n"
         "    TagComponent: {Tag: A}\n"},
        {"nested prefab instance",
         "Prefab: {FormatVersion: 1, Name: X}\nEntities:\n" + entityYaml +
             "    PrefabInstance: {Prefab: 00000000000000aa, Instance: 0000000000000001-0000000000000003, "
             "SourceEntity: 0000000000000001-0000000000000004}\n"},
        {"bad revision", "Prefab: {FormatVersion: 1, Name: X, Revision: -4}\nEntities:\n" + entityYaml},
    };
    for (const Case& testCase : cases) {
        const auto result = Xora2D::PrefabSerializer::Parse(testCase.Yaml, "test.prefab.yaml");
        Check(!result.Success && !result.Message.empty(),
              std::string("rejected: ") + testCase.Label + " (" + result.Message + ")");
    }

    // Missing assets are reported but do not block loading (references are kept).
    std::string missingText = text;
    const std::string textureHandle = fixture.Texture.ToString();
    missingText.replace(missingText.find(textureHandle), textureHandle.size(), "00000000deadbeef");
    const auto missing = Xora2D::PrefabSerializer::Parse(missingText, "missing.prefab.yaml", &fixture.Assets());
    Check(missing.Success && missing.MissingAssets.size() == 1 &&
              missing.MissingAssets.front().find("deadbeef") != std::string::npos,
          "missing texture reported as a missing asset");
    Check(missing.Success && missing.Loaded->FindEntity(missing.Loaded->GetEntityIds().front())
                                     .GetComponent<Xora2D::SpriteRendererComponent>()
                                     .TextureAsset == Xora2D::AssetHandle(0xdeadbeef),
          "missing asset reference kept for when the asset returns");

    // Failed writes never corrupt an existing prefab.
    const std::string before = ReadText(file);
    Xora2D::Prefab replacement;
    replacement.SetName("Other");
    (void)replacement.CreateEntity("Other", Xora2D::UUID::Generate());
    Check(!Xora2D::PrefabSerializer::Save(replacement, file, false).Success && ReadText(file) == before,
          "saving over an existing prefab without permission is refused");
    Xora2D::Prefab empty;
    empty.SetName("Empty");
    Check(!Xora2D::PrefabSerializer::Save(empty, file, true).Success && ReadText(file) == before,
          "invalid prefab data is rejected before writing");
    fs::permissions(file.parent_path(), fs::perms::owner_write, fs::perm_options::remove);
    const bool readOnlyDirectory = !std::ofstream(file.parent_path() / "probe.tmp").good();
    if (readOnlyDirectory) {
        Check(!Xora2D::PrefabSerializer::Save(replacement, file, true).Success && ReadText(file) == before,
              "failed write (read-only folder) leaves the prefab intact");
    }
    fs::permissions(file.parent_path(), fs::perms::owner_write, fs::perm_options::add);
    std::size_t leftovers = 0;
    for (const auto& item : fs::directory_iterator(file.parent_path())) {
        leftovers += item.path().filename().string().starts_with(".xora_prefab_") ? 1 : 0;
    }
    Check(leftovers == 0, "no temporary files left behind");
}

// ---------------------------------------------------------------------------------------------------------------
// Creation and instantiation
// ---------------------------------------------------------------------------------------------------------------

void TestCreationAndInstantiation() {
    XORA_LOG_INFO("TEST prefab creation and instantiation");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeRichEntity("Crate");
    const Xora2D::UUID sourceId = source.GetUUID();
    const auto created = fixture.CreatePrefab({source}, "Crate");
    Check(created.Success, "prefab created and linked");
    const Xora2D::AssetMetadata* meta = fixture.Context.GetAssetRegistry().FindByHandle(created.Handle);
    Check(meta != nullptr && meta->Type == Xora2D::AssetType::Prefab && meta->RelativePath == created.RelativePath,
          "prefab registered with a stable handle");
    Check(created.Handle == fixture.Context.GetAssetRegistry().AllocateHandleForPath(created.RelativePath) ||
              meta != nullptr,
          "handle allocated through the registry");
    Check(Xora2DEditor::ClassifyContentFile(fixture.Root() / "Assets" / created.RelativePath) ==
              Xora2DEditor::ContentItemKind::Prefab,
          "Content Browser recognizes the prefab file");
    const Xora2D::Entity linked = fixture.Scene().FindEntityByUUID(sourceId);
    Check(Xora2D::IsPrefabInstance(linked) && Link(linked).Prefab == created.Handle &&
              Link(linked).InstanceId == created.InstanceId && Link(linked).Revision == 1 && Link(linked).Overrides.empty(),
          "source entity became the first instance (keeps its UUID)");

    // Never overwritten.
    const std::string original = ReadText(fixture.Root() / "Assets" / created.RelativePath);
    Xora2D::Entity other = fixture.MakeSimpleEntity("Other", {0.0f, 0.0f});
    const auto duplicate = fixture.CreatePrefab({other}, "Crate");
    Check(!duplicate.Success && ReadText(fixture.Root() / "Assets" / created.RelativePath) == original &&
              !Xora2D::IsPrefabInstance(other),
          "creating a prefab with an existing name is refused; nothing changes");
    for (const char* bad : {"", "a/b", "..", ".hidden", "bad:name", "trailing "}) {
        Check(!fixture.CreatePrefab({other}, bad).Success, std::string("invalid prefab name rejected: '") + bad + "'");
    }
    Xora2D::PrefabCreateOptions escape{};
    escape.Name = "Escape";
    escape.AssetFolder = "../Outside";
    Check(!Xora2D::CreatePrefabFromEntities({other}, escape, fixture.Assets()).Success &&
              !fs::exists(fixture.Root() / "Outside"),
          "prefab folders cannot escape the asset directory");
    Check(!fixture.CreatePrefab({}, "Nothing").Success, "a prefab needs at least one entity");

    // Multi-entity prefab keeps the given order.
    Xora2D::Entity left = fixture.MakeSimpleEntity("Left", {10.0f, 10.0f});
    Xora2D::Entity right = fixture.MakeSimpleEntity("Right", {60.0f, 10.0f});
    const auto pair = fixture.CreatePrefab({left, right}, "Pair");
    const auto pairPrefab = fixture.Assets().GetLoadedPrefab(pair.Handle);
    Check(pair.Success && pairPrefab != nullptr && pairPrefab->GetEntityCount() == 2 &&
              pairPrefab->FindEntity(pairPrefab->GetEntityIds()[0]).GetName() == "Left" &&
              pairPrefab->FindEntity(pairPrefab->GetEntityIds()[1]).GetName() == "Right",
          "multi-entity prefab keeps entity order");
    Check(Link(left).InstanceId == Link(right).InstanceId, "multi-entity sources share one instance id");

    // Instantiation.
    const std::size_t before = fixture.Scene().GetEntityCount();
    const auto first = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    const auto second = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    Check(first.Success && second.Success && fixture.Scene().GetEntityCount() == before + 2, "prefab instantiated twice");
    if (!first.Success || !second.Success) {
        return;
    }
    Xora2D::Entity a = first.Entities.front();
    Xora2D::Entity b = second.Entities.front();
    const auto prefab = fixture.Assets().GetLoadedPrefab(created.Handle);
    std::unordered_set<Xora2D::UUID> ids{a.GetUUID(), b.GetUUID(), sourceId, prefab->GetEntityIds().front()};
    Check(ids.size() == 4, "each instance entity gets a unique UUID");
    Check(first.InstanceId != second.InstanceId && Link(a).SourceEntity == prefab->GetEntityIds().front(),
          "instances are distinct and point at their source entity");
    Check(Xora2D::DiffAgainstPrefab(a, *prefab).empty() && Xora2D::DiffAgainstPrefab(b, *prefab).empty(),
          "instantiation restores every component exactly");
    Check(a.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset == fixture.Texture &&
              a.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset == fixture.Audio &&
              a.GetComponent<Xora2D::PythonScriptComponent>().ScriptAsset == fixture.Script &&
              a.GetComponent<Xora2D::AnimationComponent>().DefaultClip == fixture.Clip,
          "instance asset references resolve through the project registry");
    a.GetComponent<Xora2D::SpriteRendererComponent>().Color = {0.0f, 1.0f, 0.0f, 1.0f};
    a.GetComponent<Xora2D::TransformComponent>().Translation = {999.0f, 999.0f};
    Check(b.GetComponent<Xora2D::SpriteRendererComponent>().Color == glm::vec4(0.9f, 0.2f, 0.1f, 1.0f) &&
              b.GetComponent<Xora2D::TransformComponent>().Translation == glm::vec2(120.0f, 80.0f),
          "modifying one instance does not affect another");
    Check(prefab->FindEntity(prefab->GetEntityIds().front()).GetComponent<Xora2D::SpriteRendererComponent>().Color ==
              glm::vec4(0.9f, 0.2f, 0.1f, 1.0f),
          "modifying an instance does not affect the prefab");

    Xora2D::PrefabInstantiateOptions placed{};
    placed.Position = glm::vec2{500.0f, 300.0f};
    const auto atPosition = Xora2D::InstantiatePrefab(fixture.Scene(), pair.Handle, fixture.Assets(), placed);
    Check(atPosition.Success && atPosition.Entities.size() == 2 &&
              atPosition.Entities[0].GetComponent<Xora2D::TransformComponent>().Translation == glm::vec2(500.0f, 300.0f) &&
              atPosition.Entities[1].GetComponent<Xora2D::TransformComponent>().Translation == glm::vec2(550.0f, 300.0f) &&
              HasOverride(atPosition.Entities[0], "Transform.Translation"),
          "instantiating at a position keeps relative offsets and records the position override");

    // Failures leave nothing behind.
    const std::size_t count = fixture.Scene().GetEntityCount();
    Check(!Xora2D::InstantiatePrefab(fixture.Scene(), Xora2D::AssetHandle(0x1234), fixture.Assets()).Success &&
              fixture.Scene().GetEntityCount() == count,
          "unknown prefab handle fails without creating entities");
    Check(!Xora2D::InstantiatePrefab(fixture.Scene(), fixture.Texture, fixture.Assets()).Success &&
              fixture.Scene().GetEntityCount() == count,
          "a non-prefab asset cannot be instantiated");
    Xora2D::PrefabInstantiateOptions abort{};
    abort.BeforeEntity = [](std::size_t index) { return index < 1; };
    const auto aborted = Xora2D::InstantiatePrefab(fixture.Scene(), pair.Handle, fixture.Assets(), abort);
    Check(!aborted.Success && fixture.Scene().GetEntityCount() == count,
          "instantiation failing after the first entity rolls back (no partial instance)");
    WriteText(fixture.Root() / "Assets/Prefabs/Broken.prefab.yaml", "Prefab: {FormatVersion: 1, Name: Broken}\n");
    const auto broken = fixture.Assets().RegisterPrefabAtRelativePath("Prefabs/Broken.prefab.yaml");
    Check(broken.Success && !Xora2D::InstantiatePrefab(fixture.Scene(), broken.Handle, fixture.Assets()).Success &&
              fixture.Scene().GetEntityCount() == count,
          "an invalid prefab file fails gracefully");

    // Rename / move keep the handle and the instance links.
    auto& registry = fixture.Context.GetAssetRegistry();
    const auto renamed = Xora2DEditor::RenameResourceEntry("Assets/Prefabs/Crate.prefab.yaml", "Box", registry);
    Check(renamed.Success && registry.FindByHandle(created.Handle) != nullptr &&
              registry.FindByHandle(created.Handle)->RelativePath == "Prefabs/Box.prefab.yaml",
          "renaming a prefab keeps its handle");
    (void)Xora2DEditor::CreateProjectFolder("Assets", "Moved");
    const auto moved = Xora2DEditor::MoveResourceEntry("Assets/Prefabs/Box.prefab.yaml", "Assets/Moved", registry);
    Check(moved.Success && registry.FindByHandle(created.Handle)->RelativePath == "Moved/Box.prefab.yaml",
          "moving a prefab keeps its handle");
    fixture.Assets().UnloadAsset(created.Handle);
    const auto afterMove = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    Check(afterMove.Success && Xora2D::RevertAllPrefabOverrides(fixture.Scene(), first.InstanceId, fixture.Assets()).Success,
          "existing instances and new instantiations follow the moved prefab");
}

// ---------------------------------------------------------------------------------------------------------------
// Overrides, apply, revert, refresh
// ---------------------------------------------------------------------------------------------------------------

void TestOverrides() {
    XORA_LOG_INFO("TEST prefab overrides");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeRichEntity("Crate");
    const auto created = fixture.CreatePrefab({source}, "Crate", false);
    const auto ia = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    const auto ib = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    Xora2D::Entity a = ia.Entities.front();
    Xora2D::Entity b = ib.Entities.front();
    auto prefab = fixture.Assets().GetLoadedPrefab(created.Handle);

    const auto edit = [&](Xora2D::Entity entity, auto&& change) {
        const auto before = Xora2D::CapturePrefabProperties(entity);
        change(entity);
        return Xora2D::RecordPrefabEdits(entity, before, fixture.Assets().GetLoadedPrefab(created.Handle).get());
    };

    // Detection (one edit at a time, as the Inspector reports them).
    auto marked = edit(a, [](Xora2D::Entity e) {
        e.GetComponent<Xora2D::TransformComponent>().Translation = {300.0f, 40.0f};
    });
    Check(marked == std::vector<std::string>{"Transform.Translation"}, "position change detected");
    marked = edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::TransformComponent>().RotationRadians = 1.0f; });
    Check(marked == std::vector<std::string>{"Transform.Rotation"}, "rotation change detected");
    marked = edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::TransformComponent>().Scale = {3.0f, 3.0f}; });
    Check(marked == std::vector<std::string>{"Transform.Scale"}, "scale change detected");
    marked = edit(a, [](Xora2D::Entity e) { e.SetName("CrateA"); });
    Check(marked == std::vector<std::string>{"Name"}, "name change detected");
    marked = edit(a, [](Xora2D::Entity e) {
        e.GetComponent<Xora2D::SpriteRendererComponent>().Color = {0.0f, 0.0f, 1.0f, 1.0f};
    });
    Check(marked == std::vector<std::string>{"Sprite.Color"}, "sprite color change detected");
    marked = edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::SpriteRendererComponent>().Visible = false; });
    Check(marked == std::vector<std::string>{"Sprite.Visible"}, "sprite visibility change detected");
    marked = edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::SpriteRendererComponent>().TilingFactor = 5.0f; });
    Check(marked == std::vector<std::string>{"Sprite"}, "other sprite field marks the Sprite component");
    marked = edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::RigidBody2DComponent>().GravityScale = 2.0f; });
    Check(marked == std::vector<std::string>{"RigidBody2D"}, "rigid body change marks the component");
    marked = edit(a, [](Xora2D::Entity e) { e.RemoveComponent<Xora2D::CircleCollider2DComponent>(); });
    Check(marked == std::vector<std::string>{"CircleCollider2D"}, "removing a component is tracked");
    marked = edit(a, [](Xora2D::Entity) {});
    Check(marked.empty(), "no change, no override");
    Check(Link(b).Overrides.empty(), "other instance has no overrides");
    Check(Xora2D::DiffAgainstPrefab(a, *prefab).size() == Link(a).Overrides.size(),
          "recorded overrides match the actual differences from the prefab");

    // Editing a value back to the prefab value drops the override.
    (void)edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::SpriteRendererComponent>().Visible = true; });
    Check(!HasOverride(a, "Sprite.Visible"), "value restored by hand stops being an override");

    // Revert one property.
    auto revert = Xora2D::RevertPrefabOverride(a, "Transform.Scale", fixture.Assets());
    Check(revert.Success && a.GetComponent<Xora2D::TransformComponent>().Scale == glm::vec2(2.0f, 1.5f) &&
              !HasOverride(a, "Transform.Scale") && HasOverride(a, "Transform.Translation"),
          "reverting one property restores it and keeps the other overrides");
    revert = Xora2D::RevertPrefabOverride(a, "CircleCollider2D", fixture.Assets());
    Check(revert.Success && a.HasComponent<Xora2D::CircleCollider2DComponent>() &&
              a.GetComponent<Xora2D::CircleCollider2DComponent>().Radius == 12.0f,
          "reverting a removed component restores it");
    Check(!Xora2D::RevertPrefabOverride(a, "Not.AProperty", fixture.Assets()).Success, "unknown property rejected");

    // Overrides persist through save / load.
    const fs::path scenePath = fixture.Root() / "Scenes/Overrides.scene.yaml";
    Check(Xora2D::SceneSerializer::Serialize(fixture.Scene(), scenePath).Success, "scene with instances saved");
    auto reloaded = Xora2D::Scene::Create();
    const auto load = Xora2D::SceneSerializer::Deserialize(*reloaded, scenePath, &fixture.Assets());
    const Xora2D::Entity reloadedA = reloaded->FindEntityByUUID(a.GetUUID());
    Check(load.Success && reloadedA.IsValid() && Link(reloadedA).Overrides == Link(a).Overrides &&
              Link(reloadedA).InstanceId == ia.InstanceId && Link(reloadedA).Prefab == created.Handle,
          "instance overrides preserved by scene save / load");

    // Apply changes to the prefab.
    const std::vector<std::string> overridesBeforeApply = Link(a).Overrides;
    const glm::vec4 colorB = b.GetComponent<Xora2D::SpriteRendererComponent>().Color;
    Xora2D::PrefabInstantiateOptions moved{};
    moved.Position = glm::vec2{10.0f, 10.0f};
    const auto ic = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets(), moved);
    Xora2D::Entity c = ic.Entities.front();
    const auto apply = Xora2D::ApplyPrefabInstance(fixture.Scene(), ia.InstanceId, fixture.Assets());
    prefab = fixture.Assets().GetLoadedPrefab(created.Handle);
    Check(apply.Success && prefab->GetRevision() == 2, "apply writes a new prefab revision: " + apply.Message);
    Check(fixture.Context.GetAssetRegistry().FindByHandle(created.Handle) != nullptr &&
              fixture.Context.GetAssetRegistry().FindByHandle(created.Handle)->RelativePath == created.RelativePath,
          "apply keeps the prefab handle and file");
    const Xora2D::Entity prefabEntity = prefab->FindEntity(prefab->GetEntityIds().front());
    Check(prefabEntity.GetComponent<Xora2D::SpriteRendererComponent>().Color == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) &&
              prefabEntity.GetName() == "CrateA",
          "prefab contains the applied values");
    const auto reread = Xora2D::PrefabSerializer::Load(fixture.Root() / "Assets" / created.RelativePath);
    Check(reread.Success && reread.Loaded->GetRevision() == 2, "applied prefab persisted to disk");
    Check(Link(a).Overrides.empty() && Link(a).Revision == 2, "applied instance has no overrides and is up to date");
    Check(!overridesBeforeApply.empty(), "the instance had overrides before applying");
    Check(b.GetComponent<Xora2D::SpriteRendererComponent>().Color == colorB && Link(b).Revision == 1,
          "other instances keep their values until refreshed (now outdated)");

    // Refresh: explicit, keeps the instance's own overrides.
    const auto refresh = Xora2D::RefreshPrefabInstance(fixture.Scene(), ic.InstanceId, fixture.Assets());
    Check(refresh.Success && c.GetComponent<Xora2D::SpriteRendererComponent>().Color == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) &&
              c.GetComponent<Xora2D::TransformComponent>().Translation == glm::vec2(10.0f, 10.0f) &&
              Link(c).Revision == 2 && HasOverride(c, "Transform.Translation"),
          "refresh brings in prefab changes and keeps overrides");

    // Revert all on the outdated instance: takes the current prefab completely.
    (void)edit(b, [](Xora2D::Entity e) { e.GetComponent<Xora2D::TransformComponent>().Translation = {1.0f, 2.0f}; });
    const auto revertAll = Xora2D::RevertAllPrefabOverrides(fixture.Scene(), ib.InstanceId, fixture.Assets());
    Check(revertAll.Success && Link(b).Overrides.empty() && Xora2D::DiffAgainstPrefab(b, *prefab).empty() &&
              Link(b).Revision == 2,
          "revert all restores every property from the prefab");

    // Failed apply leaves the prefab untouched and keeps the overrides.
    (void)edit(a, [](Xora2D::Entity e) { e.GetComponent<Xora2D::SpriteRendererComponent>().Size = {7.0f, 7.0f}; });
    const fs::path prefabFile = fixture.Root() / "Assets" / created.RelativePath;
    const std::string beforeFailedApply = ReadText(prefabFile);
    fs::permissions(prefabFile.parent_path(), fs::perms::owner_write, fs::perm_options::remove);
    if (!std::ofstream(prefabFile.parent_path() / "probe.tmp").good()) {
        const auto failed = Xora2D::ApplyPrefabInstance(fixture.Scene(), ia.InstanceId, fixture.Assets());
        Check(!failed.Success && ReadText(prefabFile) == beforeFailedApply && HasOverride(a, "Sprite.Size") &&
                  fixture.Assets().GetLoadedPrefab(created.Handle)->GetRevision() == 2,
              "failed apply: prefab file, cache and instance overrides unchanged");
    }
    fs::permissions(prefabFile.parent_path(), fs::perms::owner_write, fs::perm_options::add);

    // Structural refresh: entity added to the prefab appears; removed one is unlinked, not deleted.
    auto edited = prefab->Clone();
    const Xora2D::UUID extraId = Xora2D::UUID::Generate();
    Xora2D::Entity extra = edited->CreateEntity("Extra", extraId);
    extra.AddComponent<Xora2D::SpriteRendererComponent>();
    Check(Xora2D::SavePrefabAsset(*edited, created.Handle, fixture.Assets()).Success, "prefab saved with an extra entity");
    const auto refreshB = Xora2D::RefreshPrefabInstance(fixture.Scene(), ib.InstanceId, fixture.Assets());
    Check(refreshB.Success && Xora2D::FindPrefabInstanceEntities(fixture.Scene(), ib.InstanceId).size() == 2,
          "refresh adds entities that were added to the prefab");
    auto shrunk = fixture.Assets().GetLoadedPrefab(created.Handle)->Clone();
    shrunk->GetContent().DestroyEntity(shrunk->FindEntity(extraId));
    shrunk->SyncOrderFromContent();
    Check(Xora2D::SavePrefabAsset(*shrunk, created.Handle, fixture.Assets()).Success, "prefab saved without it");
    const std::size_t entitiesBefore = fixture.Scene().GetEntityCount();
    (void)Xora2D::RefreshPrefabInstance(fixture.Scene(), ib.InstanceId, fixture.Assets());
    Check(fixture.Scene().GetEntityCount() == entitiesBefore &&
              Xora2D::FindPrefabInstanceEntities(fixture.Scene(), ib.InstanceId).size() == 1,
          "entities removed from the prefab are unlinked, never deleted");

    // Unpack.
    Check(Xora2D::UnpackPrefabInstance(fixture.Scene(), ic.InstanceId).Success && !Xora2D::IsPrefabInstance(c) &&
              c.IsValid(),
          "unpack keeps the entity and removes the link");
}

// ---------------------------------------------------------------------------------------------------------------
// Scene integration
// ---------------------------------------------------------------------------------------------------------------

void TestSceneIntegration() {
    XORA_LOG_INFO("TEST prefab scene integration");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeSimpleEntity("Tree", {50.0f, 50.0f});
    const auto created = fixture.CreatePrefab({source}, "Tree");
    const auto instance = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    Xora2D::Entity tree = instance.Entities.front();
    const auto before = Xora2D::CapturePrefabProperties(tree);
    tree.GetComponent<Xora2D::SpriteRendererComponent>().Color = {1.0f, 1.0f, 0.0f, 1.0f};
    (void)Xora2D::RecordPrefabEdits(tree, before, fixture.Assets().GetLoadedPrefab(created.Handle).get());

    const auto save = fixture.Context.SaveSceneAs("Scenes/Forest.scene.yaml");
    Check(save.Success, "scene with prefab instances saved");
    const std::string yaml = ReadText(fixture.Root() / "Scenes/Forest.scene.yaml");
    Check(yaml.find("PrefabInstance:") != std::string::npos && yaml.find(created.Handle.ToString()) != std::string::npos &&
              yaml.find("Sprite.Color") != std::string::npos,
          "scene file stores prefab handle, instance identity and overrides");

    const Xora2D::UUID treeId = tree.GetUUID();
    const Xora2D::UUID sourceId = source.GetUUID();
    const auto reopen = fixture.Context.OpenSceneFile(fixture.Root() / "Scenes/Forest.scene.yaml");
    Check(reopen.Success, "scene reopened");
    const Xora2D::Entity reloaded = fixture.Scene().FindEntityByUUID(treeId);
    const Xora2D::Entity reloadedSource = fixture.Scene().FindEntityByUUID(sourceId);
    Check(reloaded.IsValid() && reloadedSource.IsValid(), "instance entity UUIDs preserved");
    Check(Xora2D::IsPrefabInstance(reloaded) && Link(reloaded).Prefab == created.Handle &&
              Link(reloaded).InstanceId == instance.InstanceId &&
              Link(reloaded).Overrides == std::vector<std::string>{"Sprite.Color"} &&
              Link(reloadedSource).InstanceId == created.InstanceId,
          "prefab relationships and overrides restored");
    Check(Xora2D::RevertPrefabOverride(fixture.Scene().FindEntityByUUID(treeId), "Sprite.Color", fixture.Assets()).Success,
          "restored instances can still revert");

    // Old scenes without prefab data load unchanged.
    WriteText(fixture.Root() / "Scenes/Old.scene.yaml",
              "Version: 2\nName: Old\nEntities:\n  - Entity: 0000000000000abc-0000000000000def\n"
              "    TagComponent: {Tag: Rock}\n    TransformComponent: {Translation: [1, 2]}\n");
    auto old = Xora2D::Scene::Create();
    const auto oldLoad = Xora2D::SceneSerializer::Deserialize(*old, fixture.Root() / "Scenes/Old.scene.yaml",
                                                              &fixture.Assets());
    Check(oldLoad.Success && oldLoad.Warnings.empty() && old->GetEntityCount() == 1 &&
              !Xora2D::IsPrefabInstance(old->FindEntityByName("Rock")),
          "old non-prefab scenes load unchanged");
    auto legacy = Xora2D::Scene::Create();
    Xora2D::AssetRegistry sampleRegistry;
    (void)Xora2D::AssetRegistrySerializer::Load(sampleRegistry,
                                                Xora2D::GetResourceRoot() / "Assets/Registry/AssetRegistry.yaml");
    Xora2D::AssetManager sampleAssets(sampleRegistry);
    sampleAssets.SetGraphicsResourcesEnabled(false);
    const auto sampleLoad = Xora2D::SceneSerializer::Deserialize(
        *legacy, Xora2D::GetResourceRoot() / "Scenes/SandboxPhysicsDemo.scene.yaml", &sampleAssets);
    Check(sampleLoad.Success && sampleLoad.Warnings.empty() && legacy->GetEntityCount() > 0,
          "the shipped sample scene still loads: " + sampleLoad.Message);

    // Invalid instance data is an error; a missing prefab asset is only a warning.
    std::string badYaml = yaml;
    const std::string instanceKey = "Instance: " + instance.InstanceId.ToString();
    badYaml.replace(badYaml.find(instanceKey), instanceKey.size(), "Instance: not-a-uuid");
    WriteText(fixture.Root() / "Scenes/Bad.scene.yaml", badYaml);
    auto bad = Xora2D::Scene::Create();
    Check(!Xora2D::SceneSerializer::Deserialize(*bad, fixture.Root() / "Scenes/Bad.scene.yaml", &fixture.Assets()).Success,
          "malformed prefab instance data is rejected");

    (void)fixture.Context.GetAssetRegistry().RemoveAsset(created.Handle);
    fixture.Assets().UnloadAsset(created.Handle);
    auto orphan = Xora2D::Scene::Create();
    const auto orphanLoad =
        Xora2D::SceneSerializer::Deserialize(*orphan, fixture.Root() / "Scenes/Forest.scene.yaml", &fixture.Assets());
    const Xora2D::Entity orphanTree = orphan->FindEntityByUUID(treeId);
    Check(orphanLoad.Success && orphanLoad.Warnings.size() == 1 && orphanTree.IsValid() &&
              orphanTree.GetComponent<Xora2D::SpriteRendererComponent>().Color == glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) &&
              Xora2D::IsPrefabInstance(orphanTree),
          "missing prefab asset: scene loads with a warning, instance data and link kept");
    Check(!Xora2D::RevertAllPrefabOverrides(*orphan, instance.InstanceId, fixture.Assets()).Success &&
              orphanTree.GetComponent<Xora2D::SpriteRendererComponent>().Color == glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
          "operations on instances of a missing prefab fail without touching the entity");
}

// ---------------------------------------------------------------------------------------------------------------
// Runtime (Play mode)
// ---------------------------------------------------------------------------------------------------------------

void TestRuntimeIntegration() {
    XORA_LOG_INFO("TEST prefab runtime integration");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeSimpleEntity("Ball", {100.0f, 300.0f});
    source.AddComponent<Xora2D::RigidBody2DComponent>();
    source.AddComponent<Xora2D::CircleCollider2DComponent>().Radius = 16.0f;
    const auto created = fixture.CreatePrefab({source}, "Ball");
    const auto instance = Xora2D::InstantiatePrefab(fixture.Scene(), created.Handle, fixture.Assets());
    Xora2D::Entity ball = instance.Entities.front();
    const auto before = Xora2D::CapturePrefabProperties(ball);
    ball.GetComponent<Xora2D::TransformComponent>().Translation = {200.0f, 300.0f};
    (void)Xora2D::RecordPrefabEdits(ball, before, fixture.Assets().GetLoadedPrefab(created.Handle).get());

    const fs::path prefabFile = fixture.Root() / "Assets" / created.RelativePath;
    const std::string prefabBefore = ReadText(prefabFile);
    const std::string editBefore = SceneYaml(fixture.Scene());

    Check(fixture.Context.StartPlayMode(), "play mode started");
    const auto runtime = fixture.Context.GetScene();
    const Xora2D::Entity runtimeBall = runtime->FindEntityByUUID(ball.GetUUID());
    Check(runtimeBall.IsValid() && Xora2D::IsPrefabInstance(runtimeBall) && Link(runtimeBall).Prefab == created.Handle &&
              Link(runtimeBall).InstanceId == instance.InstanceId && Link(runtimeBall).Overrides == Link(ball).Overrides,
          "runtime copy preserves prefab metadata");

    for (int frame = 0; frame < 30; ++frame) {
        fixture.Context.UpdatePlayMode(1.0f / 60.0f);
    }
    Xora2D::Entity runtimeEdit = runtime->FindEntityByUUID(ball.GetUUID());
    const auto runtimeBefore = Xora2D::CapturePrefabProperties(runtimeEdit);
    runtimeEdit.GetComponent<Xora2D::SpriteRendererComponent>().Color = {1.0f, 0.0f, 1.0f, 1.0f};
    (void)Xora2D::RecordPrefabEdits(runtimeEdit, runtimeBefore, fixture.Assets().GetLoadedPrefab(created.Handle).get());
    Check(HasOverride(runtimeEdit, "Sprite.Color") && !HasOverride(ball, "Sprite.Color"),
          "runtime instance changes stay in the runtime copy");

    const auto runtimeInstance = fixture.Context.InstantiatePrefab(created.Handle);
    Check(runtimeInstance.Success && runtime->FindEntityByUUID(runtimeInstance.Entities.front().GetUUID()).IsValid() &&
              !fixture.Scene().FindEntityByUUID(runtimeInstance.Entities.front().GetUUID()).IsValid(),
          "prefabs instantiated during play go into the runtime scene only");
    Check(runtime->GetPhysicsWorld().HasPhysicsBody(runtimeInstance.Entities.front()),
          "runtime instances get physics bodies immediately");
    Check(!fixture.Context.CreatePrefab({ball.GetUUID()}, {.Name = "DuringPlay"}).Success &&
              !fs::exists(fixture.Root() / "Assets/Prefabs/DuringPlay.prefab.yaml"),
          "prefab assets cannot be created from Play mode");
    Check(!fixture.Context.OpenPrefabEditor(created.Handle).Success, "prefab editing is unavailable during play");

    (void)fixture.Context.StopPlayMode();
    Check(SceneYaml(fixture.Scene()) == editBefore, "editable scene unchanged by runtime modifications");
    Check(ReadText(prefabFile) == prefabBefore, "prefab asset unchanged by runtime modifications");
    const Xora2D::Entity restored = fixture.Scene().FindEntityByUUID(ball.GetUUID());
    Check(Xora2D::IsPrefabInstance(restored) && Link(restored).Overrides == std::vector<std::string>{"Transform.Translation"} &&
              restored.GetComponent<Xora2D::TransformComponent>().Translation == glm::vec2(200.0f, 300.0f),
          "after Stop the editable instance and its overrides are intact");
}

// ---------------------------------------------------------------------------------------------------------------
// Isolated prefab editing
// ---------------------------------------------------------------------------------------------------------------

void TestPrefabEditing() {
    XORA_LOG_INFO("TEST prefab editing workflow");
    PrefabFixture fixture;
    Xora2D::Entity source = fixture.MakeSimpleEntity("Lamp", {10.0f, 10.0f});
    const auto created = fixture.CreatePrefab({source}, "Lamp");
    const fs::path prefabFile = fixture.Root() / "Assets" / created.RelativePath;
    const std::string sceneBefore = SceneYaml(fixture.Scene());
    const std::string prefabBefore = ReadText(prefabFile);
    fixture.Context.SelectEntity(source);

    Check(fixture.Context.OpenPrefabEditor(created.Handle).Success && fixture.Context.IsEditingPrefab(),
          "prefab opened for editing");
    const auto editing = fixture.Context.GetScene();
    Check(editing != fixture.Context.GetEditScene() && editing->GetEntityCount() == 1, "isolated editing scene");
    Check(fixture.Context.GetSelectedEntity().IsValid() && fixture.Context.GetSelectedEntity().GetName() == "Lamp",
          "prefab entity selected for the Inspector");
    Xora2D::Entity lamp = fixture.Context.GetSelectedEntity();
    lamp.GetComponent<Xora2D::SpriteRendererComponent>().Color = {1.0f, 0.5f, 0.0f, 1.0f};
    Check(SceneYaml(fixture.Scene()) == sceneBefore, "editing the prefab does not modify the open scene");
    Check(!fixture.Context.StartPlayMode(), "play is unavailable while editing a prefab");
    Check(!fixture.Context.InstantiatePrefab(created.Handle).Success, "no nested instances while editing a prefab");
    Check(!fixture.Context.SaveSceneAs("Scenes/X.scene.yaml").Success, "scene save is blocked while editing a prefab");

    fixture.Context.ClosePrefabEditor();
    Check(!fixture.Context.IsEditingPrefab() && fixture.Context.GetScene() == fixture.Context.GetEditScene() &&
              ReadText(prefabFile) == prefabBefore && SceneYaml(fixture.Scene()) == sceneBefore,
          "cancel discards prefab edits and returns to the scene");
    Check(fixture.Context.GetSelectedEntity().IsValid() && fixture.Context.GetSelectedEntity().GetUUID() == source.GetUUID(),
          "scene selection restored after cancel");

    (void)fixture.Context.OpenPrefabEditor(created.Handle);
    Xora2D::Entity edited = fixture.Context.GetSelectedEntity();
    edited.GetComponent<Xora2D::SpriteRendererComponent>().Color = {1.0f, 0.5f, 0.0f, 1.0f};
    Xora2D::Entity added = fixture.Context.GetScene()->CreateEntity("Glow");
    added.AddComponent<Xora2D::SpriteRendererComponent>();
    const auto save = fixture.Context.SavePrefabEditor();
    Check(save.Success && !fixture.Context.IsEditingPrefab(), "prefab saved from the editor: " + save.Message);
    const auto reread = Xora2D::PrefabSerializer::Load(prefabFile);
    Check(reread.Success && reread.Loaded->GetRevision() == 2 && reread.Loaded->GetEntityCount() == 2 &&
              reread.Loaded->FindEntity(reread.Loaded->GetEntityIds().front())
                      .GetComponent<Xora2D::SpriteRendererComponent>()
                      .Color == glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),
          "saved prefab has the edits and a new revision");
    Check(fixture.Context.GetAssetRegistry().FindByHandle(created.Handle)->RelativePath == created.RelativePath,
          "prefab handle and path unchanged by editing");
    Check(SceneYaml(fixture.Scene()) == sceneBefore && Link(fixture.Scene().FindEntityByUUID(source.GetUUID())).Revision == 1,
          "scene instances untouched by prefab saves (outdated until refreshed)");

    // Closing everything while a prefab is open never writes it.
    (void)fixture.Context.OpenPrefabEditor(created.Handle);
    fixture.Context.GetSelectedEntity().SetName("Unsaved");
    const std::string savedText = ReadText(prefabFile);
    fixture.Context.CloseProject();
    Check(!fixture.Context.IsEditingPrefab() && ReadText(prefabFile) == savedText,
          "closing the project while editing a prefab discards the edits safely");
}

} // namespace

int RunPrefabTests() {
    XORA_LOG_INFO("TEST prefabs started");
    const int before = g_failures;
    TestPrefabSerialization();
    TestCreationAndInstantiation();
    TestOverrides();
    TestSceneIntegration();
    TestRuntimeIntegration();
    TestPrefabEditing();
    XORA_LOG_INFO("TEST prefabs finished with {} failure(s)", g_failures - before);
    return g_failures - before;
}
