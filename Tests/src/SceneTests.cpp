#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/GraphicsDevice.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scene/SceneCamera.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

int g_sceneFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_sceneFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

bool Near(float value, float expected) {
    return std::abs(value - expected) < 0.001f;
}

void TestEntityAndComponents() {
    XORA_LOG_INFO("TEST entity and components started");
    auto scene = Xora2D::Scene::Create();
    CHECK(scene->GetEntityCount() == 0, "empty scene has zero entities");

    Xora2D::Entity player = scene->CreateEntity("Player");
    CHECK(player.IsValid(), "created entity is valid");
    CHECK(player.GetName() == "Player", "entity name matches");
    CHECK(player.GetUUID().IsValid(), "entity uuid is valid");

  player.AddComponent<Xora2D::SpriteRendererComponent>();
    auto& sprite = player.GetComponent<Xora2D::SpriteRendererComponent>();
    sprite.Color = {1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(player.HasComponent<Xora2D::SpriteRendererComponent>(), "sprite component exists");

    auto& transform = player.GetComponent<Xora2D::TransformComponent>();
    transform.Translation = {10.0f, 20.0f};
    transform.RotationRadians = 1.0f;
    transform.Scale = {2.0f, 2.0f};
    transform.RecalculateLocalMatrix();
    const glm::mat4 matrix = transform.GetLocalMatrix();
    CHECK(matrix[3][0] != 0.0f || matrix[3][1] != 0.0f, "transform matrix changes after TRS");

    Xora2D::Entity copy = player;
    CHECK(copy.GetUUID() == player.GetUUID(), "entity copies share identity");

    Xora2D::Entity byName = scene->FindEntityByName("Player");
    CHECK(byName.IsValid(), "entity lookup by name works");
    CHECK(byName.GetUUID() == player.GetUUID(), "lookup by name returns same entity");

    Xora2D::Entity byUuid = scene->FindEntityByUUID(player.GetUUID());
    CHECK(byUuid.IsValid(), "entity lookup by uuid works");

    Xora2D::Entity enemy = scene->CreateEntity("Enemy");
    scene->DestroyEntity(enemy);
    CHECK(!enemy.IsValid(), "destroyed entity is invalid");

    bool threwOnDestroyed = false;
    try {
        (void)enemy.GetComponent<Xora2D::TransformComponent>();
    } catch (const std::exception&) {
        threwOnDestroyed = true;
    }
    CHECK(threwOnDestroyed, "stale entity cannot access components");

    CHECK(scene->GetEntityCount() == 1, "scene keeps remaining entities");

    player.RemoveComponent<Xora2D::SpriteRendererComponent>();
    CHECK(!player.HasComponent<Xora2D::SpriteRendererComponent>(), "component removal works");
}

void TestSceneCameraAndRender() {
    XORA_LOG_INFO("TEST scene render integration started");

    auto scene = Xora2D::Scene::Create();
    scene->SetViewportSize(800.0f, 600.0f);

    Xora2D::OrthographicCamera camera;
    CHECK(Xora2D::SceneCamera::BuildPrimaryCamera(*scene, camera) == Xora2D::SceneCamera::Result::NoPrimaryCamera,
          "scene without camera does not pick an arbitrary camera");

    Xora2D::Entity cameraEntity = scene->CreateEntity("Camera");
    cameraEntity.AddComponent<Xora2D::CameraComponent>().Primary = true;
    auto& cameraTransform = cameraEntity.GetComponent<Xora2D::TransformComponent>();
    cameraTransform.Translation = {400.0f, 300.0f};
    cameraTransform.RecalculateLocalMatrix();

    CHECK(Xora2D::SceneCamera::BuildPrimaryCamera(*scene, camera) == Xora2D::SceneCamera::Result::Ok,
          "primary camera resolves");

    Xora2D::Entity colorSprite = scene->CreateEntity("ColorSprite");
    colorSprite.AddComponent<Xora2D::SpriteRendererComponent>();
    auto& color = colorSprite.GetComponent<Xora2D::SpriteRendererComponent>();
    color.Color = {0.5f, 0.5f, 1.0f, 1.0f};
    color.Size = {100.0f, 100.0f};
    auto& colorTransform = colorSprite.GetComponent<Xora2D::TransformComponent>();
    colorTransform.Translation = {200.0f, 200.0f};
    colorTransform.RecalculateLocalMatrix();

    Xora2D::Entity textureSprite = scene->CreateEntity("TextureSprite");
    textureSprite.AddComponent<Xora2D::SpriteRendererComponent>();
    auto& textured = textureSprite.GetComponent<Xora2D::SpriteRendererComponent>();
    textured.Texture =
        Xora2D::Texture2D::CreateFromFile(Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png").string());
    textured.Size = {64.0f, 64.0f};
    auto& textureTransform = textureSprite.GetComponent<Xora2D::TransformComponent>();
    textureTransform.Translation = {300.0f, 300.0f};
    textureTransform.RecalculateLocalMatrix();

    Xora2D::WindowProperties properties;
    properties.enableOpenGL = true;
    properties.width = 320;
    properties.height = 240;
    Xora2D::Window window(properties);
    std::unique_ptr<Xora2D::GraphicsContext> context = window.CreateGraphicsContext();
    context->MakeCurrent();
    Xora2D::GraphicsDevice device(*context);

    Xora2D::Renderer2D renderer;
    renderer.Init();
    renderer.ResetStats();
    scene->OnRender(renderer);

    const auto& stats = renderer.GetStats();
    XORA_LOG_INFO("  scene stats: drawCalls={} quads={}", stats.drawCalls, stats.quadCount);
    CHECK(stats.drawCalls == 2, "scene uses separate color and textured batches");
    CHECK(stats.quadCount == 2, "scene renders both sprite entities");
    CHECK(Near(camera.GetPosition().x, 400.0f), "camera uses entity transform position");
    renderer.Shutdown();
}

} // namespace

int RunSceneTests() {
    const int before = g_sceneFailures;
    TestEntityAndComponents();
    TestSceneCameraAndRender();
    const int failed = g_sceneFailures - before;
    if (failed == 0) {
        XORA_LOG_INFO("Scene tests passed");
    } else {
        XORA_LOG_ERROR("Scene tests failed ({} checks)", failed);
    }
    return failed;
}