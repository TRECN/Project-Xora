#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Serialization/SceneSerializer.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_serializationFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_serializationFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

std::filesystem::path TempScenePath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("xora2d_scene_" + name + ".yaml");
}

void RemoveIfExists(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
}

} // namespace

int RunSerializationTests() {
    const int before = g_serializationFailures;
    XORA_LOG_INFO("TEST scene serialization started");
    Xora2D::UUID::ClearRegistry();

    const std::filesystem::path emptyPath = TempScenePath("empty");
    RemoveIfExists(emptyPath);

    {
        auto scene = Xora2D::Scene::Create();
        scene->SetSceneName("EmptyDemo");
        const Xora2D::SceneSerializeResult saved = Xora2D::SceneSerializer::Serialize(*scene, emptyPath);
        CHECK(saved.Success, "save empty scene succeeds");
        auto loaded = Xora2D::Scene::Create();
        const Xora2D::SceneSerializeResult loadedResult = Xora2D::SceneSerializer::Deserialize(*loaded, emptyPath);
        CHECK(loadedResult.Success, "load empty scene succeeds");
        CHECK(loaded->GetEntityCount() == 0, "empty scene stays empty");
        CHECK(loaded->GetSceneName() == "EmptyDemo", "scene name round-trips");
    }

    const std::filesystem::path oneEntityPath = TempScenePath("one");
    RemoveIfExists(oneEntityPath);
    Xora2D::UUID persistedUuid;
    {
        auto scene = Xora2D::Scene::Create();
        Xora2D::Entity entity = scene->CreateEntity("Hero");
        persistedUuid = entity.GetUUID();
        entity.GetComponent<Xora2D::TransformComponent>().Translation = {12.0f, 34.0f};
        entity.GetComponent<Xora2D::TransformComponent>().RotationRadians = 0.25f;
        entity.GetComponent<Xora2D::TransformComponent>().Scale = {2.0f, 2.0f};
        entity.GetComponent<Xora2D::TransformComponent>().RecalculateLocalMatrix();
        CHECK(Xora2D::SceneSerializer::Serialize(*scene, oneEntityPath).Success, "save one entity");
        auto loaded = Xora2D::Scene::Create();
        CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, oneEntityPath).Success, "load one entity");
        CHECK(loaded->GetEntityCount() == 1, "one entity count preserved");
        Xora2D::Entity loadedEntity = loaded->FindEntityByUUID(persistedUuid);
        CHECK(loadedEntity.IsValid(), "UUID preserved on reload");
        CHECK(loadedEntity.GetName() == "Hero", "tag preserved");
        const auto& transform = loadedEntity.GetComponent<Xora2D::TransformComponent>();
        CHECK(std::abs(transform.Translation.x - 12.0f) < 0.001f, "transform translation X preserved");
        CHECK(std::abs(transform.Translation.y - 34.0f) < 0.001f, "transform translation Y preserved");
    }

    const std::filesystem::path multiPath = TempScenePath("multi");
    RemoveIfExists(multiPath);
    {
        auto scene = Xora2D::Scene::Create();
        Xora2D::Entity spriteEntity = scene->CreateEntity("Sprite");
        auto& sprite = spriteEntity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.1f, 0.2f, 0.3f, 0.9f};
        sprite.Size = {64.0f, 32.0f};
        sprite.TilingFactor = 2.0f;
        sprite.Visible = false;

        Xora2D::Entity cameraEntity = scene->CreateEntity("Camera");
        auto& camera = cameraEntity.AddComponent<Xora2D::CameraComponent>();
        camera.Enabled = true;
        camera.Primary = true;
        camera.Zoom = 1.5f;

        Xora2D::Entity physicsEntity = scene->CreateEntity("Crate");
        auto& body = physicsEntity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Dynamic;
        body.FixedRotation = true;
        body.LinearDamping = 0.25f;
        auto& box = physicsEntity.AddComponent<Xora2D::BoxCollider2DComponent>();
        box.Size = {40.0f, 40.0f};
        box.Restitution = 0.35f;
        box.IsSensor = false;
        auto& circle = physicsEntity.AddComponent<Xora2D::CircleCollider2DComponent>();
        circle.Radius = 15.0f;
        circle.IsSensor = true;

        CHECK(Xora2D::SceneSerializer::Serialize(*scene, multiPath).Success, "save multi-component scene");

        auto loaded = Xora2D::Scene::Create();
        CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, multiPath).Success, "load multi-component scene");
        CHECK(loaded->GetEntityCount() == 3, "multiple entities preserved");

        const Xora2D::Entity loadedSprite = loaded->FindEntityByName("Sprite");
        const auto& loadedSpriteComponent = loadedSprite.GetComponent<Xora2D::SpriteRendererComponent>();
        CHECK(std::abs(loadedSpriteComponent.Color.r - 0.1f) < 0.001f, "sprite color preserved");
        CHECK(loadedSpriteComponent.Visible == false, "sprite visibility preserved");

        const Xora2D::Entity loadedCamera = loaded->FindEntityByName("Camera");
        CHECK(loadedCamera.GetComponent<Xora2D::CameraComponent>().Zoom == 1.5f, "camera zoom preserved");

        const Xora2D::Entity loadedCrate = loaded->FindEntityByName("Crate");
        CHECK(loadedCrate.GetComponent<Xora2D::RigidBody2DComponent>().FixedRotation, "rigid body settings preserved");
        CHECK(loadedCrate.GetComponent<Xora2D::BoxCollider2DComponent>().Restitution == 0.35f,
              "box collider preserved");
        CHECK(loadedCrate.HasComponent<Xora2D::CircleCollider2DComponent>(), "circle collider present after load");
        CHECK(loadedCrate.GetComponent<Xora2D::CircleCollider2DComponent>().IsSensor,
              "circle sensor flag preserved");

        loaded->OnUpdate(1.0f / 60.0f);
        CHECK(loaded->GetPhysicsWorld().HasPhysicsBody(loadedCrate), "physics body created after load");
    }

    const std::filesystem::path duplicatePath = TempScenePath("duplicate");
    RemoveIfExists(duplicatePath);
    WriteTextFile(duplicatePath,
                  "Version: 1\nName: Dup\nEntities:\n  - Entity: abcd1234-5678abcd\n    TagComponent:\n      Tag: A\n"
                  "    TransformComponent:\n      Translation: [0, 0]\n  - Entity: abcd1234-5678abcd\n    "
                  "TagComponent:\n      Tag: B\n    TransformComponent:\n      Translation: [1, 1]\n");
    {
        auto scene = Xora2D::Scene::Create();
        const auto result = Xora2D::SceneSerializer::Deserialize(*scene, duplicatePath);
        CHECK(!result.Success, "reject duplicate UUIDs");
    }

    const std::filesystem::path malformedPath = TempScenePath("malformed");
    RemoveIfExists(malformedPath);
    WriteTextFile(malformedPath, "{{invalid yaml: [");
    {
        auto scene = Xora2D::Scene::Create();
        scene->CreateEntity("Survivor");
        const std::size_t beforeCount = scene->GetEntityCount();
        const auto result = Xora2D::SceneSerializer::Deserialize(*scene, malformedPath);
        CHECK(!result.Success, "reject malformed YAML");
        CHECK(scene->GetEntityCount() == beforeCount, "failed parse leaves scene unchanged");
    }

    const std::filesystem::path missingFieldPath = TempScenePath("missing");
    RemoveIfExists(missingFieldPath);
    WriteTextFile(missingFieldPath,
                  "Version: 1\nEntities:\n  - Entity: f-1\n    TagComponent:\n      Tag: NoTransform\n");
    {
        auto scene = Xora2D::Scene::Create();
        const std::size_t beforeCount = scene->GetEntityCount();
        const auto result = Xora2D::SceneSerializer::Deserialize(*scene, missingFieldPath);
        CHECK(!result.Success, "reject missing required TransformComponent");
        CHECK(scene->GetEntityCount() == beforeCount, "missing field leaves scene unchanged");
    }

    const std::filesystem::path badEnumPath = TempScenePath("badenum");
    RemoveIfExists(badEnumPath);
    WriteTextFile(badEnumPath,
                  "Version: 1\nEntities:\n  - Entity: e-1\n    TagComponent:\n      Tag: Body\n    "
                  "TransformComponent:\n      Translation: [0, 0]\n    RigidBody2DComponent:\n      Type: "
                  "Impossible\n");
    {
        auto scene = Xora2D::Scene::Create();
        CHECK(!Xora2D::SceneSerializer::Deserialize(*scene, badEnumPath).Success, "reject invalid rigid body enum");
    }

    const std::filesystem::path badZoomPath = TempScenePath("badzoom");
    RemoveIfExists(badZoomPath);
    WriteTextFile(badZoomPath,
                  "Version: 1\nEntities:\n  - Entity: c-1\n    TagComponent:\n      Tag: Cam\n    "
                  "TransformComponent:\n      Translation: [0, 0]\n    CameraComponent:\n      Zoom: -1\n");
    {
        auto scene = Xora2D::Scene::Create();
        CHECK(!Xora2D::SceneSerializer::Deserialize(*scene, badZoomPath).Success, "reject invalid camera zoom");
    }

    const std::filesystem::path missingTexturePath = TempScenePath("notexture");
    RemoveIfExists(missingTexturePath);
    WriteTextFile(missingTexturePath,
                  "Version: 1\nEntities:\n  - Entity: t-1\n    TagComponent:\n      Tag: Tex\n    "
                  "TransformComponent:\n      Translation: [0, 0]\n    SpriteRendererComponent:\n      TexturePath: "
                  "Textures/DoesNotExist.png\n");
    {
        auto scene = Xora2D::Scene::Create();
        const std::size_t beforeCount = scene->GetEntityCount();
        CHECK(!Xora2D::SceneSerializer::Deserialize(*scene, missingTexturePath).Success,
              "reject missing texture asset");
        CHECK(scene->GetEntityCount() == beforeCount, "missing texture leaves scene unchanged");
    }

    const std::filesystem::path cyclePath = TempScenePath("cycle");
    RemoveIfExists(cyclePath);
    {
        auto scene = Xora2D::Scene::Create();
        for (int index = 0; index < 5; ++index) {
            scene->CreateEntity("Entity" + std::to_string(index));
        }
        CHECK(scene->GetEntityCount() == 5, "setup cycle entity count");
        for (int cycle = 0; cycle < 3; ++cycle) {
            CHECK(Xora2D::SceneSerializer::Serialize(*scene, cyclePath).Success, "repeated save succeeds");
            auto loaded = Xora2D::Scene::Create();
            CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, cyclePath).Success, "repeated load succeeds");
            CHECK(loaded->GetEntityCount() == 5, "repeated load preserves entity count");
            scene = loaded;
        }
    }

    const std::filesystem::path physicsReplacePath = TempScenePath("physreplace");
    RemoveIfExists(physicsReplacePath);
    {
        auto scene = Xora2D::Scene::Create();
        Xora2D::Entity entity = scene->CreateEntity("Dynamic");
        entity.AddComponent<Xora2D::RigidBody2DComponent>().Type = Xora2D::RigidBodyType2D::Dynamic;
        entity.AddComponent<Xora2D::BoxCollider2DComponent>().Size = {40.0f, 40.0f};
        entity.GetComponent<Xora2D::TransformComponent>().Translation = {100.0f, 200.0f};
        scene->OnUpdate(1.0f / 60.0f);
        CHECK(scene->GetPhysicsWorld().HasPhysicsBody(entity), "physics body exists before scene replace load");

        CHECK(Xora2D::SceneSerializer::Serialize(*scene, physicsReplacePath).Success, "save scene with physics");
        CHECK(Xora2D::SceneSerializer::Deserialize(*scene, physicsReplacePath).Success, "load replaces scene in place");
        Xora2D::Entity reloaded = scene->FindEntityByName("Dynamic");
        CHECK(reloaded.IsValid(), "entity survives in-place load");
        scene->OnUpdate(1.0f / 60.0f);
        CHECK(scene->GetPhysicsWorld().HasPhysicsBody(reloaded), "physics body recreated after load");
    }

    CHECK(Xora2D::UUID::IsRegistered(persistedUuid), "loaded UUID stays registered");
    const Xora2D::UUID fresh = Xora2D::UUID::Generate();
    CHECK(fresh != persistedUuid, "newly generated UUID differs from a loaded UUID");
    CHECK(Xora2D::UUID::IsRegistered(fresh), "generated UUID is tracked in registry");

    RemoveIfExists(emptyPath);
    RemoveIfExists(oneEntityPath);
    RemoveIfExists(multiPath);
    RemoveIfExists(duplicatePath);
    RemoveIfExists(malformedPath);
    RemoveIfExists(missingFieldPath);
    RemoveIfExists(badEnumPath);
    RemoveIfExists(badZoomPath);
    RemoveIfExists(missingTexturePath);
    RemoveIfExists(cyclePath);
    RemoveIfExists(physicsReplacePath);

    const int failed = g_serializationFailures - before;
    if (failed == 0) {
        XORA_LOG_INFO("Serialization tests passed");
    } else {
        XORA_LOG_ERROR("Serialization tests failed ({} checks)", failed);
    }
    return failed;
}